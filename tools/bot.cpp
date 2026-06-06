#include <bedrock/api/Client.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

static std::string nowString() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::string s = std::ctime(&t);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static void printVersions() {
    std::cerr << "[VERSION] available Bedrock versions:";
    for (const auto& v : bedrock::ProtocolDefinition::versions()) std::cerr << " " << v;
    std::cerr << "\n";
}

static bool looksLikeVersion(const std::string& s) {
    return !s.empty() && std::isdigit(static_cast<unsigned char>(s[0]));
}

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage:\n  " << argv[0] << " <version> <host> <port> <profile> [--hold] [--packet-dump] [--username name]\n";
        std::cout << "Example:\n  " << argv[0] << " <version> cpe.ign.gg 19132 StewedV --hold --packet-dump\n";
        printVersions();
        return 0;
    }

    bedrock::api::ClientOptions options;
    int first = 1;
    if (argc > 1 && looksLikeVersion(argv[1])) {
        options.version = argv[1];
        first = 2;
    }
    if (options.version.empty()) { auto vs = bedrock::ProtocolDefinition::versions(); if (!vs.empty()) options.version = vs.back(); }
    if (!bedrock::ProtocolDefinition::supportsVersion(options.version)) {
        std::cerr << "[VERSION] unsupported Bedrock version: " << options.version << "\n";
        printVersions();
        return 2;
    }
    options.host = argc > first ? argv[first] : "localhost";
    options.port = argc > first + 1 ? static_cast<uint16_t>(std::stoi(argv[first + 1])) : 19132;
    options.profile = argc > first + 2 ? argv[first + 2] : options.username;
    options.username = options.profile;

    for (int i = first + 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--packet-dump") {
            options.packetDump = true;
        } else if (arg == "--offline") {
            options.offline = true;
        } else if (arg == "--quiet") {
            options.quiet = true;
        } else if (arg == "--trace" || arg == "--debug") {
            options.quiet = false;
        } else if (arg == "--username" && i + 1 < argc) {
            options.username = argv[++i];
        } else if (arg.rfind("--username=", 0) == 0) {
            options.username = arg.substr(std::string("--username=").size());
        } else if (arg == "--hold") {
            // accepted for bedrock-protocol-like command compatibility
        } else if (!arg.empty() && arg[0] != '-') {
            options.username = arg;
        }
    }

    std::string exePath = argv[0];
    auto slash = exePath.find_last_of('/');
    options.executableDir = (slash == std::string::npos) ? "." : exePath.substr(0, slash);

    auto client = bedrock::api::createClient(options);

    client.onAny([](const bedrock::api::Packet& packet) {
        std::cout << "[bot] packet " << packet.name
                  << " id=" << packet.id
                  << " ok=" << (packet.ok ? "true" : "false")
                  << " fields=" << packet.fields.size()
                  << "\n";
    });

    client.on("start_game", [](const bedrock::api::Packet& packet) {
        std::cout << "[bot] joined world at x=" << packet.get("x")
                  << " z=" << packet.get("z") << "\n";
    });

    client.on("player_list", [](const bedrock::api::Packet& packet) {
        if (packet.has("username")) {
            std::cout << "[bot] player: " << packet.get("username")
                      << " uuid=" << packet.get("uuid") << "\n";
        }
    });

    client.on("level_chunk", [](const bedrock::api::Packet& packet) {
        std::cout << "[bot] chunk x=" << packet.get("x")
                  << " z=" << packet.get("z") << "\n";
    });

    client.onText([&](const bedrock::api::TextPacket& text) {
        if (text.sourceName != options.username) {
            std::cout << "[bot] chat: " << text.sourceName
                      << " said: " << text.message
                      << " on " << nowString() << "\n";

            client.queue("text", {
                {"type", "chat"},
                {"source_name", options.username},
                {"message", text.sourceName + " said: " + text.message + " on " + nowString()},
                {"xuid", ""},
                {"platform_chat_id", ""},
                {"filtered_message", ""}
            });

            std::cout << "[bot] queued text echo (outbound encoder TODO)\n";
        }
    });

    std::cout << "[bot] connecting " << options.host << ":" << options.port
              << " version=" << options.version
              << " profile=" << options.profile << " username=" << options.username << "\n";

    return client.run();
}
