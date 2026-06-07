#include <bedrock/client/BedrockNetworkClient.hpp>
#include <bedrock/server/BedrockServer.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

static bool checkVersion(const std::string& version) {
    std::atomic<bool> joined {false};
    std::atomic<bool> gotPlayStatus {false};
    std::atomic<bool> gotError {false};

    auto server = bedrock::createServer({
        .host = "127.0.0.1",
        .port = 0,
        .version = version,
        .motd = "Network Client Smoke",
        .maxPlayers = 3
    });
    server.listen();

    auto client = bedrock::createNetworkClient({
        .host = "127.0.0.1",
        .port = server.boundPort(),
        .username = "CppSmoke",
        .version = version,
        .offline = true,
        .connectTimeoutMs = 1000
    });

    client.onJoin([&]() {
        joined = true;
    });

    client.on("play_status", [&](const bedrock::BedrockNetworkClientPacketEvent&) {
        gotPlayStatus = true;
    });

    client.onError([&](const std::string& error) {
        gotError = true;
        std::cerr << "[NETWORK-CLIENT-SMOKE] " << version << " error: " << error << "\n";
    });

    if (!client.connect()) {
        std::cerr << "[NETWORK-CLIENT-SMOKE] " << version << " connect failed\n";
        server.close();
        return false;
    }

    for (int i = 0; i < 100 && (!joined.load() || !gotPlayStatus.load()) && !gotError.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    client.close();
    server.close();

    if (!joined.load()) {
        std::cerr << "[NETWORK-CLIENT-SMOKE] " << version << " did not join\n";
        return false;
    }
    if (!gotPlayStatus.load()) {
        std::cerr << "[NETWORK-CLIENT-SMOKE] " << version << " did not receive play_status\n";
        return false;
    }
    if (gotError.load()) {
        return false;
    }

    std::cout << "[NETWORK-CLIENT-SMOKE] " << version << " ok\n";
    return true;
}

int main() {
    bool ok = true;
    ok = checkVersion("1.20.40") && ok;
    ok = checkVersion("1.20.50") && ok;
    ok = checkVersion("1.21.100") && ok;
    return ok ? 0 : 1;
}
