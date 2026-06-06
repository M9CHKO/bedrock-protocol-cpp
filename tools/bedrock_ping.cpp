#include "bedrock/RakNetPing.hpp"

#include <cstdint>
#include <iostream>
#include <string>

static void printUsage(const char* argv0) {
    std::cout << "Usage:\n";
    std::cout << "  " << argv0 << " <host> [port]\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << argv0 << " cpe.ign.gg 19132\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string host = argv[1];
    uint16_t port = 19132;

    if (argc >= 3) {
        int p = std::stoi(argv[2]);

        if (p <= 0 || p > 65535) {
            std::cerr << "Invalid port: " << p << "\n";
            return 1;
        }

        port = static_cast<uint16_t>(p);
    }

    std::cout << "[PING] Sending RakNet unconnected ping to "
              << host
              << ":"
              << port
              << "\n";

    auto info = bedrock::RakNetPinger::ping(host, port, 4000);

    if (!info.ok) {
        std::cerr << "[FAIL] " << info.error << "\n";
        return 2;
    }

    std::cout << "\n=== RakNet Pong ===\n";
    std::cout << "Host: " << info.host << "\n";
    std::cout << "Port: " << info.port << "\n";
    std::cout << "Server GUID: " << info.serverGuid << "\n";

    std::cout << "\n=== Raw MOTD ===\n";
    std::cout << info.rawMotd << "\n";

    std::cout << "\n=== Parsed ===\n";
    std::cout << "Edition: " << info.edition << "\n";
    std::cout << "MOTD: " << info.motd << "\n";
    std::cout << "Sub MOTD: " << info.subMotd << "\n";
    std::cout << "Protocol Version: " << info.protocolVersion << "\n";
    std::cout << "Game Version: " << info.gameVersion << "\n";
    std::cout << "Players: " << info.onlinePlayers << "/" << info.maxPlayers << "\n";
    std::cout << "Game Mode: " << info.gameMode << "\n";
    std::cout << "Game Mode Numeric: " << info.gameModeNumeric << "\n";
    std::cout << "IPv4 Port: " << info.ipv4Port << "\n";
    std::cout << "IPv6 Port: " << info.ipv6Port << "\n";

    std::cout << "\n[OK] RakNet unconnected ping works\n";
    return 0;
}
