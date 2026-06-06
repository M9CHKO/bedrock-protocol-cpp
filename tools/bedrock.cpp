#include <bedrock/auth/XboxProfileCache.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/RakNetPing.hpp>


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
#include <process.h>
#else
#include <unistd.h>
#endif

static void sanitizeStdbufEnvironment() {
    // If the parent process was started through stdbuf, Android/Termux passes
    // LD_PRELOAD to child processes. curl cannot load that library from its
    // linker namespace, so Xbox login fails before doing any network request.
#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
    _putenv_s("LD_PRELOAD", "");
    _putenv_s("_STDBUF_O", "");
    _putenv_s("_STDBUF_E", "");
    _putenv_s("_STDBUF_I", "");
#else
    unsetenv("LD_PRELOAD");
    unsetenv("_STDBUF_O");
    unsetenv("_STDBUF_E");
    unsetenv("_STDBUF_I");
#endif
}

static std::filesystem::path g_tempLoginPacket;
static std::filesystem::path g_activeLock;
static FILE* g_sessionPipe = nullptr;

static void cleanupTempLoginPacket() {
    if (!g_tempLoginPacket.empty()) {
        std::error_code ec;
        std::filesystem::remove(g_tempLoginPacket, ec);
    }
    if (!g_activeLock.empty()) {
        std::error_code ec;
        std::filesystem::remove(g_activeLock, ec);
    }
}

static void signalCleanupHandler(int sig) {
    cleanupTempLoginPacket();
    std::_Exit(128 + sig);
}

static void installCleanupHandlers() {
    std::atexit(cleanupTempLoginPacket);
    std::signal(SIGINT, signalCleanupHandler);
    std::signal(SIGTERM, signalCleanupHandler);
#ifdef SIGHUP
    std::signal(SIGHUP, signalCleanupHandler);
#endif
}

static std::string sanitizePathPart(const std::string& input) {
    std::string out;
    for (char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    return out.empty() ? "bot" : out;
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

static bool processAlive(long long pid) {
    if (pid <= 0) return false;
#ifdef __linux__
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#else
    return false;
#endif
}

static long long currentPid() {
#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

static long long readPidFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    long long pid = 0;
    in >> pid;
    return pid;
}

static void prepareSingleRunLock(const std::filesystem::path& lockPath) {
    std::error_code ec;
    std::filesystem::create_directories(lockPath.parent_path(), ec);
    if (std::filesystem::exists(lockPath)) {
        long long oldPid = readPidFile(lockPath);
        if (processAlive(oldPid)) {
            std::cerr << "[ERROR] this profile/version is already running in another process pid=" << oldPid << "\n";
            std::cerr << "[NEXT] stop the previous bot or use another profile name/account.\n";
            std::exit(6);
        }
        std::cout << "[BOT] previous run was not closed cleanly; waiting for server/session cleanup...\n";
        std::this_thread::sleep_for(std::chrono::seconds(6));
        std::filesystem::remove(lockPath, ec);
    }
    std::ofstream out(lockPath);
    out << currentPid() << "\n";
    g_activeLock = lockPath;
}

static void cleanupOldRunPackets(const std::filesystem::path& runsRoot) {
    std::error_code ec;
    if (!std::filesystem::exists(runsRoot, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(runsRoot, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().filename() == "real_login_packet.bin") {
            std::filesystem::remove(it->path(), ec);
        }
    }
}

static std::string shellQuote(const std::string& s) {
#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

static std::string executablePath(const std::string& exeDir, const std::string& name) {
    std::filesystem::path path = std::filesystem::path(exeDir) / name;
    if (std::filesystem::exists(path)) return path.string();

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
    std::filesystem::path exePath = std::filesystem::path(exeDir) / (name + ".exe");
    if (std::filesystem::exists(exePath)) return exePath.string();
#endif

    return path.string();
}

static std::string latestSupportedVersion() {
    auto versions = bedrock::ProtocolDefinition::versions();
    return versions.empty() ? std::string() : versions.back();
}

static std::string g_logMode = "debug";
static bool g_packetDumpMode = false;
static bool g_insidePacketObject = false;

static bool shouldPrintBotLogLine(const std::string& mode, const std::string& line) {
    if (mode == "trace") {
        return true;
    }

    if (mode == "quiet") {
        return line.rfind("[XBOX]", 0) == 0 ||
               line.rfind("[BOT]", 0) == 0 ||
               line.rfind("[EVENT]", 0) == 0 ||
               line.rfind("[API]", 0) == 0 ||
               line.rfind("[PROTODEF]", 0) == 0 ||
               line.rfind("[PROTODEF_MARK]", 0) == 0 ||
               line.rfind("[HOLD]", 0) == 0 ||
               line.rfind("[WAIT]", 0) == 0 ||
               line.rfind("[FAIL]", 0) == 0 ||
               line.rfind("[DISCONNECT]", 0) == 0 ||
               line.rfind("[ERROR]", 0) == 0 ||
               line.rfind("[WARN]", 0) == 0;
    }

    return line.rfind("[XBOX]", 0) == 0 ||
           line.rfind("[BOT]", 0) == 0 ||
           line.rfind("[CONNECT]", 0) == 0 ||
           line.rfind("[NETWORK]", 0) == 0 ||
           line.rfind("[LOGIN]", 0) == 0 ||
           line.rfind("[PACKS]", 0) == 0 ||
           line.rfind("[JOIN]", 0) == 0 ||
           line.rfind("[INIT]", 0) == 0 ||
           line.rfind("[EVENT]", 0) == 0 ||
           line.rfind("[API]", 0) == 0 ||
           line.rfind("[PROTODEF]", 0) == 0 ||
           line.rfind("[PROTODEF_MARK]", 0) == 0 ||
           line.rfind("[HOLD]", 0) == 0 ||
           line.rfind("[SUMMARY]", 0) == 0 ||
           line.rfind("[WAIT]", 0) == 0 ||
           line.rfind("[FAIL]", 0) == 0 ||
           line.rfind("[DISCONNECT]", 0) == 0 ||
           line.rfind("[ERROR]", 0) == 0 ||
           line.rfind("[WARN]", 0) == 0;
}

static void printCleanLine(const std::string& line) {
    if (g_packetDumpMode) {
        if (line == "{") {
            g_insidePacketObject = true;
            std::cout << line << "\n";
            return;
        }

        if (g_insidePacketObject) {
            std::cout << line << "\n";
            if (line == "}") {
                g_insidePacketObject = false;
            }
            return;
        }
    }

    if (shouldPrintBotLogLine(g_logMode, line)) {
        std::cout << line << "\n";
    }
}

static void printVersions() {
    std::cerr << "[VERSION] available Bedrock versions:";
    for (const auto& v : bedrock::ProtocolDefinition::versions()) {
        std::cerr << " " << v;
    }
    std::cerr << "\n";
}

static bool isHelp(const std::string& s) {
    return s == "--help" || s == "-h" || s == "help";
}

static bool isVersions(const std::string& s) {
    return s == "--versions" || s == "versions" || s == "--list-versions";
}

static bool looksLikeVersion(const std::string& s) {
    return !s.empty() && std::isdigit(static_cast<unsigned char>(s[0]));
}

int main(int argc, char** argv) {
    sanitizeStdbufEnvironment();
    installCleanupHandlers();
    if (argc > 1 && isVersions(argv[1])) {
        printVersions();
        return 0;
    }

    if (argc == 1 || (argc > 1 && isHelp(argv[1]))) {
        std::cout << "Usage:\n";
        std::cout << "  " << argv[0] << " <version> <host> <port> <profile> [--hold] [--offline] [--packet-dump] [--packet-json] [--trace|--debug|--quiet] [--regen-login]\n";
        std::cout << "Example:\n";
        std::cout << "  " << argv[0] << " <version> cpe.ign.gg 19132 StewedV --hold --packet-dump\n";
        printVersions();
        return 0;
    }

    std::string version = latestSupportedVersion();
    int first = 1;
    if (argc > 1 && looksLikeVersion(argv[1])) {
        version = argv[1];
        first = 2;
    }

    if (!bedrock::ProtocolDefinition::supportsVersion(version)) {
        std::cerr << "[VERSION] unsupported Bedrock version: " << version << "\n";
        printVersions();
        std::cerr << "[USAGE] " << argv[0] << " <version> <host> <port> <profile> [flags]\n";
        return 2;
    }

    auto proto = bedrock::ProtocolDefinition::forVersion(version);
    std::string host = argc > first ? argv[first] : "cpe.ign.gg";
    std::string port = argc > first + 1 ? argv[first + 1] : "19132";
    std::string profile = argc > first + 2 ? argv[first + 2] : "Bot";
    std::string hold = "--hold";
    bool packetDump = false;
    bool packetJson = false;
    bool regenLogin = false;
    bool offline = false;

    for (int i = first + 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--packet-dump") {
            packetDump = true;
            g_packetDumpMode = true;
        } else if (arg == "--packet-json" || arg == "--api-json") {
            packetJson = true;
        } else if (arg == "--quiet") {
            g_logMode = "quiet";
        } else if (arg == "--trace") {
            g_logMode = "trace";
        } else if (arg == "--debug") {
            g_logMode = "debug";
        } else if (arg == "--regen-login" || arg == "--regen") {
            regenLogin = true;
        } else if (arg == "--offline") {
            offline = true;
        } else if (arg == "--hold" || arg == "hold" || arg == "forever") {
            hold = "--hold";
        } else if (!arg.empty() && arg[0] != '-') {
            hold = arg;
        }
    }

    std::string exePath = argv[0];
    auto slash = exePath.find_last_of('/');
    std::string exeDir = (slash == std::string::npos) ? "." : exePath.substr(0, slash);

    bedrock::XboxProfileCache cache;
    cache.ensureProfileVersionDir(profile, version);
    auto paths = cache.pathsForVersion(profile, version);

    const auto profileRunRoot = paths.versionDir / sanitizePathPart(profile);
    cleanupOldRunPackets(profileRunRoot);
    prepareSingleRunLock(profileRunRoot / "active.lock");

    const auto runId = std::to_string(static_cast<long long>(::getpid()));
    const auto runDir = profileRunRoot / ("run_" + runId);
    std::filesystem::create_directories(runDir);
    std::filesystem::path loginPacket = runDir / "real_login_packet.bin";
    g_tempLoginPacket = loginPacket;

    if (!offline && !std::filesystem::exists(paths.authJson)) {
        std::cout << "[XBOX] auth cache not found for profile=" << profile << "\n";
        std::cout << "[XBOX] starting device login...\n";

        std::ostringstream loginCmd;
        loginCmd << shellQuote(executablePath(exeDir, "xbox-login")) << " " << shellQuote(profile);

        int loginRc = std::system(loginCmd.str().c_str());
        if (loginRc != 0) {
            std::cerr << "[ERROR] xbox login failed\n";
            return 1;
        }
    } else if (offline) {
        std::cout << "[XBOX] offline mode; skipping Xbox authentication\n";
    } else {
        std::cout << "[XBOX] auth cache found for profile=" << profile << "\n";
    }

    std::cout << "[LOGINPKT] generating one-run login packet...\n";

    std::ostringstream genCmd;
    genCmd << shellQuote(executablePath(exeDir, "login")) << " "
           << shellQuote(profile) << " "
           << proto.protocolVersion() << " "
           << shellQuote(version) << " "
           << shellQuote(host + ":" + port) << " "
           << "--out " << shellQuote(loginPacket.string());

    if (offline) {
        genCmd << " --offline";
    }

    int genRc = std::system(genCmd.str().c_str());
    if (genRc != 0) {
        std::cerr << "[ERROR] failed to generate login packet\n";
        cleanupTempLoginPacket();
        return 1;
    }

    if (!std::filesystem::exists(loginPacket)) {
        std::cerr << "[ERROR] login packet was not created\n";
        cleanupTempLoginPacket();
        return 1;
    }

    auto privateKeyPath = paths.profileDir / "client_private_key.pem";
    if (std::filesystem::exists(privateKeyPath)) {
        std::cout << "[XBOX] client private key ready\n";
    } else {
        std::cerr << "[ERROR] internal client private key missing for profile=" << profile << "\n";
        std::cerr << "[NEXT] run bot again; it should regenerate login data automatically\n";
        return 1;
    }

    try {
        auto pong = bedrock::RakNetPinger::ping(host, static_cast<uint16_t>(std::stoi(port)), 1800);
        if (pong.ok) {
            std::cout << "[SERVER] motd=" << pong.motd
                      << " version=" << pong.gameVersion
                      << " protocol=" << pong.protocolVersion
                      << " players=" << pong.onlinePlayers << "/" << pong.maxPlayers << "\n";
            if (pong.protocolVersion > 0 && static_cast<int>(proto.protocolVersion()) < pong.protocolVersion) {
                std::cout << "[WARN] selected protocol " << proto.protocolVersion()
                          << " is lower than the server advertised protocol " << pong.protocolVersion
                          << ". This server may reject old versions before sending any game packet.\n";
            }
        } else if (!pong.error.empty()) {
            std::cout << "[WARN] server ping failed: " << pong.error << "\n";
        }
    } catch (...) {
        std::cout << "[WARN] server ping failed before connect\n";
    }

    std::cout << "[BOT] version=" << version << " protocol=" << proto.protocolVersion() << "\n";
    std::cout << "[BOT] profile=" << profile << "\n";
    if (offline) std::cout << "[BOT] offline=true\n";
    std::cout << "[BOT] host=" << host << ":" << port << "\n";
    std::cout << "[BOT] cache=internal hidden per-profile/per-version/per-run\n";
    std::cout << "[BOT] connection will stay alive until Ctrl+C\n\n";

    std::ostringstream cmd;
    cmd << shellQuote(executablePath(exeDir, "bedrock-session")) << " "
        << shellQuote(host) << " "
        << shellQuote(port) << " "
        << "1400 " << proto.protocolVersion() << " fe-len 193 250 "
        << shellQuote(loginPacket.string()) << " "
        << shellQuote(hold);

    if (packetDump) {
        cmd << " --packet-dump";
    }
    if (packetJson) {
        cmd << " --packet-json";
    }
    cmd << " --version " << shellQuote(version);
    cmd << " --private-key " << shellQuote(privateKeyPath.string());

    FILE* pipe = popen(cmd.str().c_str(), "r");
    g_sessionPipe = pipe;
    if (!pipe) {
        std::cerr << "[ERROR] failed to start bedrock-session\n";
        return 1;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        printCleanLine(line);
    }

    int rc = pclose(pipe);
    g_sessionPipe = nullptr;
    cleanupTempLoginPacket();
    if (rc != 0) {
        std::cerr << "[ERROR] session ended with error. If no start_game packet appeared, server likely rejected this protocol/version or account/session.\n";
    }
    return rc == 0 ? 0 : 1;
}
