#include <bedrock/client/BedrockNetworkClient.hpp>
#include <bedrock/relay/BedrockLiveRelay.hpp>
#include <bedrock/server/BedrockServer.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

static bool checkVersion(const std::string& version) {
    std::atomic<bool> downstreamJoined {false};
    std::atomic<bool> upstreamReady {false};
    std::atomic<bool> gotRelayStatus {false};
    std::atomic<bool> gotError {false};

    auto upstreamServer = bedrock::createServer({
        .host = "127.0.0.1",
        .port = 0,
        .version = version,
        .motd = "Live Relay Smoke Upstream",
        .maxPlayers = 3
    });
    upstreamServer.listen();

    bedrock::BedrockLiveRelayOptions relayOptions;
    relayOptions.server.host = "127.0.0.1";
    relayOptions.server.port = 0;
    relayOptions.server.version = version;
    relayOptions.server.motd = "Live Relay Smoke";
    relayOptions.server.maxPlayers = 3;
    relayOptions.upstream.host = "127.0.0.1";
    relayOptions.upstream.port = upstreamServer.boundPort();
    relayOptions.upstream.username = "RelaySmokeUp";
    relayOptions.upstream.version = version;
    relayOptions.upstream.offline = true;
    relayOptions.upstream.connectTimeoutMs = 1000;

    auto relay = bedrock::createRelayServer(std::move(relayOptions));
    relay.onJoin([&](const bedrock::BedrockServerConnection&) {
        downstreamJoined = true;
    });
    relay.onStatus([&](const bedrock::BedrockLiveRelayStatus& status) {
        gotRelayStatus = true;
        if (status.upstreamReady) {
            upstreamReady = true;
        }
    });
    relay.onError([&](const std::string& error) {
        gotError = true;
        std::cerr << "[LIVE-RELAY-SMOKE] " << version << " relay error: " << error << "\n";
    });
    relay.listen();

    auto downstreamClient = bedrock::createNetworkClient({
        .host = "127.0.0.1",
        .port = relay.boundPort(),
        .username = "RelaySmokeDown",
        .version = version,
        .offline = true,
        .connectTimeoutMs = 1000
    });
    downstreamClient.onError([&](const std::string& error) {
        gotError = true;
        std::cerr << "[LIVE-RELAY-SMOKE] " << version << " downstream error: " << error << "\n";
    });

    if (!downstreamClient.connect()) {
        std::cerr << "[LIVE-RELAY-SMOKE] " << version << " downstream connect failed\n";
        relay.close();
        upstreamServer.close();
        return false;
    }

    for (int i = 0; i < 150 &&
         (!downstreamJoined.load() || !upstreamReady.load() || !gotRelayStatus.load()) &&
         !gotError.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    downstreamClient.close();
    relay.close();
    upstreamServer.close();

    if (!downstreamJoined.load()) {
        std::cerr << "[LIVE-RELAY-SMOKE] " << version << " downstream did not join relay\n";
        return false;
    }
    if (!upstreamReady.load()) {
        std::cerr << "[LIVE-RELAY-SMOKE] " << version << " upstream did not become ready\n";
        return false;
    }
    if (gotError.load()) {
        return false;
    }

    std::cout << "[LIVE-RELAY-SMOKE] " << version << " ok\n";
    return true;
}

int main() {
    bool ok = true;
    ok = checkVersion("1.20.40") && ok;
    ok = checkVersion("1.20.50") && ok;
    ok = checkVersion("1.21.100") && ok;
    return ok ? 0 : 1;
}
