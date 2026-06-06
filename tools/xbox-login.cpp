#include <bedrock/auth/XboxProfileCache.hpp>
#include <bedrock/auth/XboxTokenCache.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

static void sanitizeStdbufEnvironment() {
    // If the parent process was started through stdbuf, Android/Termux passes
    // LD_PRELOAD to child processes. curl cannot load that library from its
    // linker namespace, so Xbox login fails before doing any network request.
    unsetenv("LD_PRELOAD");
    unsetenv("_STDBUF_O");
    unsetenv("_STDBUF_E");
    unsetenv("_STDBUF_I");
}


static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string execRead(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string out;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");

    while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();

    int rc = pclose(pipe);
    if (rc != 0) throw std::runtime_error("command failed: " + cmd);

    return out;
}

static std::string jsonString(const std::string& json, const std::string& key) {
    std::string marker = "\"" + key + "\"";
    auto p = json.find(marker);
    if (p == std::string::npos) return "";

    p = json.find(':', p);
    if (p == std::string::npos) return "";

    p = json.find('"', p);
    if (p == std::string::npos) return "";

    std::string out;
    bool esc = false;

    for (++p; p < json.size(); ++p) {
        char c = json[p];
        if (esc) {
            out += c;
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        out += c;
    }

    return out;
}

static int jsonInt(const std::string& json, const std::string& key, int fallback) {
    std::string marker = "\"" + key + "\"";
    auto p = json.find(marker);
    if (p == std::string::npos) return fallback;

    p = json.find(':', p);
    if (p == std::string::npos) return fallback;
    ++p;

    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;

    auto e = p;
    while (e < json.size() && std::isdigit(static_cast<unsigned char>(json[e]))) ++e;

    if (e == p) return fallback;
    return std::stoi(json.substr(p, e - p));
}

static std::string extractDisplayClaim(const std::string& json, const std::string& key) {
    auto xui = json.find("\"xui\"");
    if (xui == std::string::npos) return "";
    return jsonString(json.substr(xui), key);
}

static std::string extractUhs(const std::string& json) {
    return extractDisplayClaim(json, "uhs");
}

static std::string curlPostForm(
    const std::string& url,
    const std::string& data
) {
    std::string cmd =
        "curl -sS -X POST "
        "-H 'Content-Type: application/x-www-form-urlencoded' "
        "--data " + shellQuote(data) + " " + shellQuote(url);

    return execRead(cmd);
}

static std::string curlPostJson(
    const std::string& url,
    const std::string& json
) {
    auto tmp = std::filesystem::temp_directory_path() / "bedrock_xbox_post.json";
    {
        std::ofstream out(tmp);
        out << json;
    }

    std::string cmd =
        "curl -sS -X POST "
        "-H 'Content-Type: application/json' "
        "--data @" + shellQuote(tmp.string()) + " " + shellQuote(url);

    auto result = execRead(cmd);
    std::filesystem::remove(tmp);
    return result;
}

int main(int argc, char** argv) {
    sanitizeStdbufEnvironment();
    if (argc < 2 || std::string(argv[1]).empty()) {
        std::cerr << "Usage: " << argv[0] << " <profile> [xboxClientId]\n";
        return 2;
    }
    std::string profile = argv[1];

    std::string clientId;
    if (argc > 2) clientId = argv[2];
    else if (const char* env = std::getenv("BEDROCK_XBOX_CLIENT_ID")) clientId = env;
    if (clientId.empty()) {
        // Public Xbox/Minecraft OAuth client id commonly used by Bedrock tooling.
        clientId = "00000000402b5328";
    }

    bedrock::XboxProfileCache profiles;
    profiles.ensureProfileDir(profile);
    auto paths = profiles.pathsFor(profile);

    std::cout << "[XBOX] profile=" << profile << "\n";
    std::cout << "[XBOX] cache=internal hidden\n";

    std::string deviceResp = curlPostForm(
        "https://login.live.com/oauth20_connect.srf",
        "client_id=" + clientId +
        "&scope=service::user.auth.xboxlive.com::MBI_SSL"
        "&response_type=device_code"
    );

    std::string deviceCode = jsonString(deviceResp, "device_code");
    std::string userCode = jsonString(deviceResp, "user_code");
    std::string verificationUri = jsonString(deviceResp, "verification_uri");
    std::string message = jsonString(deviceResp, "message");
    int interval = jsonInt(deviceResp, "interval", 5);

    if (deviceCode.empty()) {
        std::cerr << "[ERROR] failed to get device code\n" << deviceResp << "\n";
        return 1;
    }

    std::cout << "\n[XBOX] Open: " << verificationUri << "\n";
    std::cout << "[XBOX] Code: " << userCode << "\n";
    if (!message.empty()) std::cout << "[XBOX] " << message << "\n";
    std::cout << "\n[XBOX] waiting for browser login...\n";

    std::string msAccess;
    std::string msRefresh;
    std::string msExpires;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));

        std::string tokenResp = curlPostForm(
            "https://login.live.com/oauth20_token.srf?client_id=" + clientId,
            "grant_type=urn:ietf:params:oauth:grant-type:device_code"
            "&client_id=" + clientId +
            "&device_code=" + deviceCode
        );

        std::string err = jsonString(tokenResp, "error");
        if (err == "authorization_pending") {
            std::cout << ".";
            std::cout.flush();
            continue;
        }
        if (err == "slow_down") {
            interval += 5;
            continue;
        }
        if (!err.empty()) {
            std::cerr << "\n[ERROR] Microsoft token error: " << err << "\n";
            std::cerr << tokenResp << "\n";
            return 1;
        }

        msAccess = jsonString(tokenResp, "access_token");
        msRefresh = jsonString(tokenResp, "refresh_token");
        int expiresIn = jsonInt(tokenResp, "expires_in", 0);
        msExpires = "expires_in:" + std::to_string(expiresIn);

        break;
    }

    std::cout << "\n[XBOX] Microsoft token ok\n";

    std::string xblJson =
        "{"
        "\"Properties\":{"
        "\"AuthMethod\":\"RPS\","
        "\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RpsTicket\":\"t=" + msAccess + "\""
        "},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\","
        "\"TokenType\":\"JWT\""
        "}";

    std::string xblResp = curlPostJson(
        "https://user.auth.xboxlive.com/user/authenticate",
        xblJson
    );

    std::string xblToken = jsonString(xblResp, "Token");
    if (xblToken.empty()) {
        std::cerr << "[ERROR] XBL user token failed\n" << xblResp << "\n";
        return 1;
    }

    std::cout << "[XBOX] user token ok\n";

    std::string xstsJson =
        "{"
        "\"Properties\":{"
        "\"SandboxId\":\"RETAIL\","
        "\"UserTokens\":[\"" + xblToken + "\"]"
        "},"
        "\"RelyingParty\":\"rp://api.minecraftservices.com/\","
        "\"TokenType\":\"JWT\""
        "}";

    std::string xstsResp = curlPostJson(
        "https://xsts.auth.xboxlive.com/xsts/authorize",
        xstsJson
    );

    std::string xstsToken = jsonString(xstsResp, "Token");
    std::string uhs = extractUhs(xstsResp);
    std::string xuid = extractDisplayClaim(xstsResp, "xid");
    std::string gamertag = extractDisplayClaim(xstsResp, "gtg");

    if (xstsToken.empty()) {
        std::cerr << "[ERROR] XSTS failed\n" << xstsResp << "\n";
        return 1;
    }

    std::cout << "[XBOX] xsts ok\n";

    bedrock::XboxTokenCacheData data;
    data.profileName = profile;
    data.gamertag = gamertag.empty() ? profile : gamertag;
    data.xuid = xuid;
    data.microsoftAccessToken = msAccess;
    data.microsoftRefreshToken = msRefresh;
    data.microsoftExpiresAt = msExpires;
    data.xboxUserToken = xblToken;
    data.xstsToken = xstsToken;
    data.xstsUserHash = uhs;
    data.xstsExpiresAt = "";

    bedrock::XboxTokenCacheFile::save(paths.authJson, data);

    std::cout << "[XBOX] saved auth cache\n";
    std::cout << "[XBOX] tokens are hidden from log\n";

    return 0;
}
