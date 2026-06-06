#include <bedrock/LoginPacket.hpp>
#include <bedrock/BedrockKeyExchange.hpp>
#include <bedrock/auth/BedrockAuthJwt.hpp>
#include <bedrock/auth/BedrockClientDataBuilder.hpp>
#include <bedrock/auth/XboxProfileCache.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/auth/XboxTokenCache.hpp>
#include <bedrock/world/MinecraftDataPathResolver.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

static std::string extractDisplayClaim(const std::string& json, const std::string& key) {
    auto xui = json.find("\"xui\"");
    if (xui == std::string::npos) return "";
    return jsonString(json.substr(xui), key);
}

static std::vector<std::string> jsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> out;

    std::string marker = "\"" + key + "\"";
    auto p = json.find(marker);
    if (p == std::string::npos) return out;

    p = json.find('[', p);
    if (p == std::string::npos) return out;

    ++p;

    while (p < json.size()) {
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;

        if (p >= json.size() || json[p] == ']') break;

        if (json[p] != '"') {
            ++p;
            continue;
        }

        std::string value;
        bool esc = false;

        for (++p; p < json.size(); ++p) {
            char c = json[p];

            if (esc) {
                value += c;
                esc = false;
                continue;
            }

            if (c == '\\') {
                esc = true;
                continue;
            }

            if (c == '"') {
                ++p;
                break;
            }

            value += c;
        }

        out.push_back(value);

        while (p < json.size() && json[p] != ',' && json[p] != ']') ++p;
        if (p < json.size() && json[p] == ',') ++p;
    }

    return out;
}


static void replaceJsonStringValue(std::string& json, const std::string& key, const std::string& value);
static std::string escapeJson(const std::string& s);

static bool jsonHasKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

static std::vector<int> parseVersionParts(const std::string& version) {
    std::vector<int> out;
    std::string cur;
    for (char c : version) {
        if (c == '.') {
            out.push_back(cur.empty() ? 0 : std::stoi(cur));
            cur.clear();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            cur += c;
        }
    }
    out.push_back(cur.empty() ? 0 : std::stoi(cur));
    while (out.size() < 3) out.push_back(0);
    return out;
}

static bool versionAtLeast(const std::string& version, int a, int b, int c) {
    auto v = parseVersionParts(version);
    if (v[0] != a) return v[0] > a;
    if (v[1] != b) return v[1] > b;
    return v[2] >= c;
}

static bool removeJsonKeyValue(std::string& json, const std::string& key) {
    std::string marker = "\"" + key + "\"";
    auto keyPos = json.find(marker);
    if (keyPos == std::string::npos) return false;

    auto valueStart = json.find(':', keyPos);
    if (valueStart == std::string::npos) return false;
    ++valueStart;

    bool inStr = false;
    bool esc = false;
    int depthObj = 0;
    int depthArr = 0;
    std::size_t end = valueStart;

    for (; end < json.size(); ++end) {
        char ch = json[end];
        if (inStr) {
            if (esc) esc = false;
            else if (ch == '\\') esc = true;
            else if (ch == '"') inStr = false;
            continue;
        }
        if (ch == '"') { inStr = true; continue; }
        if (ch == '{') { ++depthObj; continue; }
        if (ch == '}') {
            if (depthObj == 0 && depthArr == 0) break;
            --depthObj;
            continue;
        }
        if (ch == '[') { ++depthArr; continue; }
        if (ch == ']') { --depthArr; continue; }
        if (ch == ',' && depthObj == 0 && depthArr == 0) break;
    }

    std::size_t eraseBegin = keyPos;
    std::size_t eraseEnd = end;
    if (eraseEnd < json.size() && json[eraseEnd] == ',') {
        ++eraseEnd;
    } else {
        while (eraseBegin > 0 && std::isspace(static_cast<unsigned char>(json[eraseBegin - 1]))) --eraseBegin;
        if (eraseBegin > 0 && json[eraseBegin - 1] == ',') --eraseBegin;
    }
    json.erase(eraseBegin, eraseEnd - eraseBegin);
    return true;
}

static bool replaceJsonIntValue(std::string& json, const std::string& key, long long value) {
    std::string marker = "\"" + key + "\"";
    auto p = json.find(marker);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
    auto e = p;
    while (e < json.size() && (json[e] == '-' || std::isdigit(static_cast<unsigned char>(json[e])))) ++e;
    json.replace(p, e - p, std::to_string(value));
    return true;
}

static void addJsonRawBeforeKey(
    std::string& json,
    const std::string& beforeKey,
    const std::string& key,
    const std::string& rawValue
) {
    if (jsonHasKey(json, key)) return;
    std::string marker = "\"" + beforeKey + "\"";
    auto p = json.find(marker);
    std::string field = "\"" + key + "\":" + rawValue;
    if (p != std::string::npos) json.insert(p, field + ",");
    else {
        auto brace = json.rfind('}');
        if (brace != std::string::npos) {
            auto beforeBrace = brace;
            while (beforeBrace > 0 && std::isspace(static_cast<unsigned char>(json[beforeBrace - 1]))) {
                --beforeBrace;
            }
            const bool emptyObject = beforeBrace > 0 && json[beforeBrace - 1] == '{';
            json.insert(brace, (emptyObject ? "" : ",") + field);
        }
    }
}

static void addJsonBoolBeforeKey(std::string& json, const std::string& beforeKey, const std::string& key, bool value) {
    addJsonRawBeforeKey(json, beforeKey, key, value ? "true" : "false");
}

static void setJsonStringValue(
    std::string& json,
    const std::string& beforeKey,
    const std::string& key,
    const std::string& value
) {
    if (jsonHasKey(json, key)) {
        replaceJsonStringValue(json, key, value);
    } else {
        addJsonRawBeforeKey(json, beforeKey, key, "\"" + escapeJson(value) + "\"");
    }
}

static void setJsonIntValue(
    std::string& json,
    const std::string& beforeKey,
    const std::string& key,
    long long value
) {
    if (!replaceJsonIntValue(json, key, value)) {
        addJsonRawBeforeKey(json, beforeKey, key, std::to_string(value));
    }
}

static void setJsonBoolValue(
    std::string& json,
    const std::string& beforeKey,
    const std::string& key,
    bool value
) {
    removeJsonKeyValue(json, key);
    addJsonRawBeforeKey(json, beforeKey, key, value ? "true" : "false");
}

static std::string makeHex16() {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string s;
    s.reserve(16);
    for (int i = 0; i < 16; ++i) s.push_back(hex[rd() & 15]);
    return s;
}

static void normalizeClientDataForVersion(std::string& payload, const std::string& version) {
    // Match bedrock-protocol's per-version ClientData shape. Some servers
    // silently reject old protocols when new ClientData keys are present.
    if (!versionAtLeast(version, 1, 19, 20)) removeJsonKeyValue(payload, "TrustedSkin");
    if (!versionAtLeast(version, 1, 19, 62)) removeJsonKeyValue(payload, "OverrideSkin");
    if (!versionAtLeast(version, 1, 19, 80)) removeJsonKeyValue(payload, "CompatibleWithClientSideChunkGen");
    if (!versionAtLeast(version, 1, 21, 42)) {
        removeJsonKeyValue(payload, "MaxViewDistance");
        removeJsonKeyValue(payload, "MemoryTier");
        removeJsonKeyValue(payload, "PlatformType");
    }
    if (!versionAtLeast(version, 1, 21, 90)) addJsonBoolBeforeKey(payload, "UIProfile", "ThirdPartyNameOnly", false);
    else removeJsonKeyValue(payload, "ThirdPartyNameOnly");

    const auto nowMs = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    replaceJsonIntValue(payload, "ClientRandomId", nowMs);
    replaceJsonStringValue(payload, "PlayFabId", makeHex16());
}

static void applyRuntimeClientDataFields(
    std::string& payload,
    const std::string& displayName,
    const std::string& version,
    const std::string& selfSignedUuid,
    const std::string& serverAddress
) {
    const std::string beforeKey = "SkinAnimationData";

    setJsonStringValue(payload, beforeKey, "ThirdPartyName", displayName);
    setJsonStringValue(payload, beforeKey, "GameVersion", version);
    setJsonStringValue(payload, beforeKey, "DeviceId", selfSignedUuid);
    setJsonStringValue(payload, beforeKey, "SelfSignedId", selfSignedUuid);
    setJsonStringValue(payload, beforeKey, "ServerAddress", serverAddress);
    setJsonStringValue(payload, beforeKey, "DeviceModel", "PrismarineJS");
    setJsonStringValue(payload, beforeKey, "LanguageCode", "en_GB");
    setJsonStringValue(payload, beforeKey, "PlatformOfflineId", "");
    setJsonStringValue(payload, beforeKey, "PlatformOnlineId", "");
    setJsonStringValue(payload, beforeKey, "PlayFabId", makeHex16());

    const auto nowMs = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    setJsonIntValue(payload, beforeKey, "ClientRandomId", nowMs);
    setJsonIntValue(payload, beforeKey, "CurrentInputMode", 1);
    setJsonIntValue(payload, beforeKey, "DefaultInputMode", 1);
    setJsonIntValue(payload, beforeKey, "DeviceOS", 7);
    setJsonIntValue(payload, beforeKey, "GraphicsMode", 1);
    setJsonIntValue(payload, beforeKey, "GuiScale", -1);
    setJsonIntValue(payload, beforeKey, "UIProfile", 0);

    setJsonBoolValue(payload, beforeKey, "IsEditorMode", false);
    if (versionAtLeast(version, 1, 19, 20)) setJsonBoolValue(payload, beforeKey, "TrustedSkin", false);
    if (versionAtLeast(version, 1, 19, 62)) setJsonBoolValue(payload, beforeKey, "OverrideSkin", false);
    if (versionAtLeast(version, 1, 19, 80)) setJsonBoolValue(payload, beforeKey, "CompatibleWithClientSideChunkGen", false);
    if (versionAtLeast(version, 1, 21, 42)) {
        setJsonIntValue(payload, beforeKey, "MaxViewDistance", 0);
        setJsonIntValue(payload, beforeKey, "MemoryTier", 0);
        setJsonIntValue(payload, beforeKey, "PlatformType", 0);
    }

    normalizeClientDataForVersion(payload, version);
}


static std::string curlPostJson(
    const std::string& url,
    const std::string& json
) {
    auto tmp = std::filesystem::temp_directory_path() / "bedrock_xsts_post.json";
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

static std::string curlPostJsonAuth(
    const std::string& url,
    const std::string& auth,
    const std::string& json
) {
    auto tmp = std::filesystem::temp_directory_path() / "bedrock_multiplayer_auth_post.json";
    {
        std::ofstream out(tmp);
        out << json;
    }

    std::string cmd =
        "curl -sS -X POST "
        "-H 'Content-Type: application/json' "
        "-H " + shellQuote("Authorization: " + auth) + " "
        "--data @" + shellQuote(tmp.string()) + " " + shellQuote(url);

    auto result = execRead(cmd);
    std::filesystem::remove(tmp);
    return result;
}



static std::string jwtHeaderString(const std::string& jwt, const std::string& key) {
    try {
        auto header = bedrock::BedrockKeyExchange::extractJwtHeaderJson(jwt);
        return jsonString(header, key);
    } catch (...) {
        return "";
    }
}


static std::string extractChainExtraDataString(
    const std::vector<std::string>& chain,
    const std::string& key
) {
    for (const auto& jwt : chain) {
        try {
            auto payload = bedrock::BedrockKeyExchange::extractJwtPayloadJson(jwt);

            auto extra = payload.find("\"extraData\"");
            if (extra == std::string::npos) {
                continue;
            }

            auto value = jsonString(payload.substr(extra), key);
            if (!value.empty()) {
                return value;
            }
        } catch (...) {
        }
    }

    return "";
}


static std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}



static std::string readTextIfExists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return "";
    std::ifstream in(path);
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

static std::string readVersionedSteveClientData(
    const std::string& version,
    const std::vector<std::filesystem::path>& minecraftDataRoots,
    std::string& clientPayloadSource
) {
    for (const auto& root : minecraftDataRoots) {
        const auto dataPaths = root / "dataPaths.json";
        if (!std::filesystem::exists(dataPaths)) {
            continue;
        }

        try {
            bedrock::MinecraftDataPathResolver resolver(dataPaths);
            auto paths = resolver.findBedrock(version);
            if (!paths || paths->steve.empty()) {
                continue;
            }

            const auto steveJson = root / "bedrock" / paths->steve / "steve.json";
            auto payload = readTextIfExists(steveJson);
            if (!payload.empty()) {
                clientPayloadSource = steveJson.string();
                return payload;
            }
        } catch (const std::exception&) {
        }
    }

    return "";
}

static void replaceJsonStringValue(std::string& json, const std::string& key, const std::string& value) {
    auto k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return;

    auto c = json.find(':', k);
    if (c == std::string::npos) return;

    auto q1 = json.find('"', c);
    if (q1 == std::string::npos) return;

    bool esc = false;
    for (auto q2 = q1 + 1; q2 < json.size(); ++q2) {
        char ch = json[q2];

        if (esc) {
            esc = false;
            continue;
        }

        if (ch == '\\') {
            esc = true;
            continue;
        }

        if (ch == '"') {
            json.replace(q1 + 1, q2 - q1 - 1, escapeJson(value));
            return;
        }
    }
}


static std::string readOrCreateUuid(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        std::string uuid;
        std::getline(in, uuid);
        if (!uuid.empty()) return uuid;
    }

    std::random_device rd;
    std::mt19937_64 gen(rd());

    uint8_t b[16];
    for (auto& x : b) {
        x = static_cast<uint8_t>(gen() & 0xff);
    }

    b[6] = static_cast<uint8_t>((b[6] & 0x0f) | 0x40); // version 4
    b[8] = static_cast<uint8_t>((b[8] & 0x3f) | 0x80); // variant

    const char* hex = "0123456789abcdef";
    std::string out;

    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex[b[i] >> 4]);
        out.push_back(hex[b[i] & 0x0f]);
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << out << "\n";

    return out;
}


static void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to write: " + path.string());

    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

int main(int argc, char** argv) {
    sanitizeStdbufEnvironment();
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <profile> <protocol> <version> [serverAddress] [--offline] [--out <login_packet.bin>]\n";
        return 2;
    }

    std::string profile = argv[1];
    uint32_t protocol = static_cast<uint32_t>(std::stoul(argv[2]));
    std::string version = argv[3];
    std::string serverAddress;
    std::filesystem::path outLoginPacketPath;
    bool offline = false;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outLoginPacketPath = argv[++i];
        } else if (arg.rfind("--out=", 0) == 0) {
            outLoginPacketPath = arg.substr(6);
        } else if (arg == "--offline") {
            offline = true;
        } else if (!arg.empty() && arg[0] != '-') {
            serverAddress = arg;
        }
    }

    if (profile.empty()) {
        std::cerr << "[ERROR] profile is empty\n";
        return 2;
    }
    if (protocol == 0 || version.empty() || !bedrock::ProtocolDefinition::supportsVersion(version)) {
        std::cerr << "[ERROR] unsupported or empty Bedrock version/protocol: version=" << version
                  << " protocol=" << protocol << "\n";
        return 2;
    }

    bedrock::XboxProfileCache profiles;
    profiles.ensureProfileVersionDir(profile, version);
    auto paths = profiles.pathsForVersion(profile, version);

    auto privatePemPath = paths.profileDir / "client_private_key.pem";
    auto publicB64Path = paths.profileDir / "client_public_key.der.b64";

    auto keys = bedrock::BedrockAuthJwt::loadOrCreateKeyPair(
        privatePemPath,
        publicB64Path
    );

    bedrock::XboxTokenCacheData cache;
    if (!offline && !std::filesystem::exists(paths.authJson)) {
        std::cerr << "[ERROR] auth cache missing for profile=" << profile << "\n";
        std::cerr << "[NEXT] run xbox login for this profile first\n";
        return 1;
    }

    if (!offline) {
        cache = bedrock::XboxTokenCacheFile::load(paths.authJson);
    }

    if (!offline && (cache.xstsUserHash.empty() || cache.xstsToken.empty())) {
        std::cerr << "[ERROR] auth cache missing xsts token/user hash\n";
        std::cerr << "[NEXT] run: ./build/xbox-login " << profile << "\n";
        return 1;
    }

    std::cout << "[LOGINPKT] profile=" << profile << "\n";
    std::cout << "[LOGINPKT] keypair ready\n";

    if (!offline && cache.xboxUserToken.empty()) {
        std::cerr << "[ERROR] auth cache missing xboxUserToken\n";
        std::cerr << "[NEXT] run: ./build/xbox-login " << profile << "\n";
        return 1;
    }

    std::vector<std::string> chain;
    std::string chainIdentity;
    std::string chainDisplayName;
    std::string chainXuid;

    std::string selfSignedUuid = readOrCreateUuid(paths.identityUuidTxt);

    if (offline) {
        chainIdentity = selfSignedUuid;
        chainDisplayName = profile;
        chainXuid = "0";
        std::cout << "[LOGINPKT] offline auth mode enabled\n";
    } else {
        std::cout << "[LOGINPKT] requesting Bedrock XSTS token...\n";

        std::string xstsReq =
            "{"
            "\"Properties\":{"
            "\"SandboxId\":\"RETAIL\","
            "\"UserTokens\":[\"" + escapeJson(cache.xboxUserToken) + "\"]"
            "},"
            "\"RelyingParty\":\"https://multiplayer.minecraft.net/\","
            "\"TokenType\":\"JWT\""
            "}";

        std::string xstsResp = curlPostJson(
            "https://xsts.auth.xboxlive.com/xsts/authorize",
            xstsReq
        );

        std::string bedrockXstsToken = jsonString(xstsResp, "Token");
        std::string bedrockUhs = extractDisplayClaim(xstsResp, "uhs");

        if (bedrockXstsToken.empty() || bedrockUhs.empty()) {
            std::cerr << "[ERROR] Bedrock XSTS failed\n";
            std::cerr << xstsResp << "\n";
            return 1;
        }

        std::cout << "[LOGINPKT] Bedrock XSTS ok\n";

        std::string authHeader = "XBL3.0 x=" + bedrockUhs + ";" + bedrockXstsToken;

        std::string authReq =
            "{\"identityPublicKey\":\"" + escapeJson(keys.publicKeyDerBase64) + "\"}";

        std::string authResp = curlPostJsonAuth(
            "https://multiplayer.minecraft.net/authentication",
            authHeader,
            authReq
        );

        chain = jsonStringArray(authResp, "chain");

        if (chain.empty()) {
            std::cerr << "[ERROR] multiplayer authentication did not return chain\n";
            std::cerr << authResp << "\n";
            return 1;
        }

        std::cout << "[LOGINPKT] chain received count=" << chain.size() << "\n";

        chainIdentity = extractChainExtraDataString(chain, "identity");
        chainDisplayName = extractChainExtraDataString(chain, "displayName");
        chainXuid = extractChainExtraDataString(chain, "XUID");
    }

    std::string displayName =
        !chainDisplayName.empty() ? chainDisplayName :
        !cache.minecraftName.empty() ? cache.minecraftName :
        !cache.gamertag.empty() ? cache.gamertag :
        profile;

    std::string xuid =
        !chainXuid.empty() ? chainXuid :
        cache.xuid;

    if (chainIdentity.empty()) {
        std::cerr << "[ERROR] Bedrock chain has no extraData.identity\n";
        return 1;
    }

    std::cout << "[LOGINPKT] chain identity=" << chainIdentity << "\n";
    std::cout << "[LOGINPKT] displayName=" << displayName << "\n";

    // ClientData is built fresh for every run and every requested version.
    // Do NOT reuse a profile-cached client_data_template.json: it may contain
    // GameVersion/Device/skin fields from an older run and can make the server
    // silently reject versions below/above the cached one.
    std::string clientPayload;
    std::string clientPayloadSource;

    const auto exeDir = std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();

    std::vector<std::filesystem::path> minecraftDataRoots;
    if (const char* env = std::getenv("BEDROCK_DATA_DIR")) {
        if (*env) {
            const auto dataDir = std::filesystem::path(env);
            minecraftDataRoots.push_back(dataDir / "minecraft-data");
            minecraftDataRoots.push_back(dataDir);
        }
    }
    minecraftDataRoots.push_back(exeDir / "data" / "minecraft-data");
    minecraftDataRoots.push_back(exeDir.parent_path() / "data" / "minecraft-data");
    minecraftDataRoots.push_back(exeDir.parent_path() / "share" / "bedrock_protocol" / "data" / "minecraft-data");
    minecraftDataRoots.push_back(exeDir.parent_path() / "share" / "bedrock_cpp" / "data" / "minecraft-data");
    minecraftDataRoots.push_back(std::filesystem::current_path() / "data" / "minecraft-data");

    clientPayload = readVersionedSteveClientData(version, minecraftDataRoots, clientPayloadSource);

    std::vector<std::filesystem::path> fallbackTemplates;
    if (const char* env = std::getenv("BEDROCK_CLIENT_DATA_TEMPLATE")) {
        if (*env) fallbackTemplates.emplace_back(env);
    }
    if (const char* env = std::getenv("BEDROCK_DATA_DIR")) {
        if (*env) fallbackTemplates.emplace_back(std::filesystem::path(env) / "client_data_template.json");
    }

    fallbackTemplates.push_back(exeDir / "data" / "client_data_template.json");
    fallbackTemplates.push_back(exeDir.parent_path() / "data" / "client_data_template.json");
    fallbackTemplates.push_back(exeDir.parent_path() / "share" / "bedrock_protocol" / "data" / "client_data_template.json");
    fallbackTemplates.push_back(exeDir.parent_path() / "share" / "bedrock_cpp" / "data" / "client_data_template.json");
    fallbackTemplates.push_back(std::filesystem::current_path() / "data" / "client_data_template.json");

    if (clientPayload.empty()) {
        for (const auto& fallback : fallbackTemplates) {
            clientPayload = readTextIfExists(fallback);
            if (!clientPayload.empty()) {
                clientPayloadSource = fallback.string();
                break;
            }
        }
    }

    if (!clientPayload.empty()) {
        applyRuntimeClientDataFields(
            clientPayload,
            displayName,
            version,
            selfSignedUuid,
            serverAddress
        );

        std::cout << "[LOGINPKT] using version-normalized clientData version=" << version
                  << " name=" << displayName
                  << " source=" << clientPayloadSource
                  << " size=" << clientPayload.size() << "\n";
    } else {
        bedrock::BedrockClientDataOptions clientDataOptions;
        clientDataOptions.displayName = displayName;
        clientDataOptions.xuid = xuid;
        clientDataOptions.gameVersion = version;
        clientDataOptions.deviceId = selfSignedUuid;
        clientDataOptions.serverAddress = serverAddress;

        clientPayload =
            bedrock::BedrockClientDataBuilder::buildClassicSkinClientData(clientDataOptions);
        normalizeClientDataForVersion(clientPayload, version);

        std::cout << "[LOGINPKT] generated version-normalized fallback clientData version=" << version
                  << " name=" << displayName
                  << " size=" << clientPayload.size() << "\n";
    }

    auto clientJwt = bedrock::BedrockAuthJwt::signEs384Jwt(
        keys.privateKeyPem,
        keys.publicKeyDerBase64,
        clientPayload
    );

    const auto nowSec = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::string clientToRootPayload;
    if (offline) {
        clientToRootPayload =
            "{"
            "\"extraData\":{"
            "\"displayName\":\"" + escapeJson(displayName) + "\","
            "\"identity\":\"" + escapeJson(chainIdentity) + "\","
            "\"titleId\":\"89692877\","
            "\"XUID\":\"0\""
            "},"
            "\"certificateAuthority\":true,"
            "\"identityPublicKey\":\"" + escapeJson(keys.publicKeyDerBase64) + "\","
            "\"iat\":" + std::to_string(nowSec) +
            "}";
    } else {
        std::string rootPublicKey = jwtHeaderString(chain.front(), "x5u");

        if (rootPublicKey.empty()) {
            std::cerr << "[ERROR] Bedrock chain first JWT has no header.x5u\n";
            return 1;
        }

        clientToRootPayload =
            "{"
            "\"identityPublicKey\":\"" + escapeJson(rootPublicKey) + "\","
            "\"certificateAuthority\":true,"
            "\"iat\":" + std::to_string(nowSec) +
            "}";
    }

    std::string clientToRootJwt = bedrock::BedrockAuthJwt::signEs384Jwt(
        keys.privateKeyPem,
        keys.publicKeyDerBase64,
        clientToRootPayload
    );

    chain.insert(chain.begin(), clientToRootJwt);

    std::string certificateJson = "{\"chain\":[";
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (i) certificateJson += ",";
        certificateJson += "\"" + escapeJson(chain[i]) + "\"";
    }
    certificateJson += "]}";

    std::string identityJson;
    if (versionAtLeast(version, 1, 21, 90)) {
        identityJson =
            "{"
            "\"Certificate\":\"" + escapeJson(certificateJson) + "\","
            "\"AuthenticationType\":" + std::string(offline ? "2" : "0") + ","
            "\"Token\":\"\""
            "}";
    } else {
        identityJson = certificateJson;
    }

    auto loginPacket = bedrock::LoginPacketCodec::encode(
        protocol,
        identityJson,
        clientJwt
    );

    const auto finalLoginPacketPath = outLoginPacketPath.empty() ? paths.loginPacketBin : outLoginPacketPath;
    writeBinaryFile(finalLoginPacketPath, loginPacket);

    std::cout << "[LOGINPKT] saved temporary login packet\n";
    std::cout << "[LOGINPKT] size=" << loginPacket.size() << "\n";
    std::cout << "[LOGINPKT] tokens hidden from log\n";

    return 0;
}
