#include <bedrock/bedrock.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct RelayTestSettings {
    std::string version = "1.20.40";

    std::string listenHost = "0.0.0.0";
    uint16_t listenPort = 19132;
    std::string motd = "Bedrock Protocol C++ Relay";
    int maxPlayers = 3;

    std::string upstreamHost = "cpe.ign.gg";
    uint16_t upstreamPort = 19132;
    std::string upstreamUsername = "RelayBot";
    std::string upstreamProfile = "RelayBot";
    bool upstreamOffline = false;
    bool interactiveAuth = true;

    bool printPackets = true;
    bool clientCacheEnabled = false;
    int32_t chunkRadius = 20;
};

RelayTestSettings relaySettings() {
    RelayTestSettings settings;

    // Change these values and rebuild/run relay-test-server.
    settings.version = "1.20.40";
    settings.listenHost = "0.0.0.0";
    settings.listenPort = 19132;
    settings.upstreamHost = "cpe.ign.gg";
    settings.upstreamPort = 19132;
    settings.upstreamUsername = "StewedV";
    settings.upstreamProfile = "StewedV";
    settings.upstreamOffline = false;
    settings.interactiveAuth = true;

    return settings;
}

bool isDownstreamHandshakePacket(const std::string& name) {
    return name == "request_network_settings" ||
        name == "login" ||
        name == "client_to_server_handshake" ||
        name == "resource_pack_client_response" ||
        name == "client_cache_status" ||
        name == "request_chunk_radius" ||
        name == "set_local_player_as_initialized";
}

bool isPlayStatusLoginSuccess(const bedrock::VersionedGamePacket& packet) {
    return packet.name == "play_status" && !packet.payload.empty() && packet.payload[0] == 0x00;
}

const char* statusName(bedrock::BedrockNetworkClientStatus status) {
    switch (status) {
        case bedrock::BedrockNetworkClientStatus::Disconnected: return "disconnected";
        case bedrock::BedrockNetworkClientStatus::Connecting: return "connecting";
        case bedrock::BedrockNetworkClientStatus::Authenticating: return "authenticating";
        case bedrock::BedrockNetworkClientStatus::Initializing: return "initializing";
        case bedrock::BedrockNetworkClientStatus::Initialized: return "initialized";
    }
    return "unknown";
}

} // namespace

int main() {
    const auto settings = relaySettings();

    auto server = bedrock::createServer({
        .host = settings.listenHost,
        .port = settings.listenPort,
        .version = settings.version,
        .motd = settings.motd,
        .maxPlayers = settings.maxPlayers
    });

    std::mutex relayMutex;
    std::optional<bedrock::BedrockServerConnection> downstream;
    std::vector<bedrock::VersionedGamePacket> pendingServerbound;
    std::unique_ptr<bedrock::BedrockNetworkClient> upstream;
    std::thread upstreamThread;
    std::atomic<bool> upstreamStarted {false};
    std::atomic<bool> upstreamReady {false};

    auto startUpstream = [&]() {
        bool expected = false;
        if (!upstreamStarted.compare_exchange_strong(expected, true)) {
            return;
        }

        bedrock::BedrockNetworkClientOptions options;
        options.host = settings.upstreamHost;
        options.port = settings.upstreamPort;
        options.username = settings.upstreamUsername;
        options.profile = settings.upstreamProfile;
        options.version = settings.version;
        options.offline = settings.upstreamOffline;
        options.interactiveAuth = settings.interactiveAuth;
        options.autoResourcePackResponses = true;
        options.autoInitPlayer = true;
        options.clientCacheEnabled = settings.clientCacheEnabled;
        options.trackWorld = true;
        options.chunkRadius = settings.chunkRadius;

        upstream = std::make_unique<bedrock::BedrockNetworkClient>(std::move(options));

        upstream->onStatus([](bedrock::BedrockNetworkClientStatus status) {
            std::cout << "[upstream] status " << statusName(status) << "\n";
        });

        upstream->onError([](const std::string& message) {
            std::cerr << "[upstream] error " << message << "\n";
        });

        upstream->onClose([](const std::string& reason) {
            std::cout << "[upstream] closed " << reason << "\n";
        });

        upstream->onAny([&](const bedrock::BedrockNetworkClientPacketEvent& event) {
            if (settings.printPackets) {
                std::cout << "[upstream -> client] " << event.packet.name << "\n";
            }

            if (isPlayStatusLoginSuccess(event.packet)) {
                return;
            }

            std::lock_guard<std::mutex> lock(relayMutex);
            if (!downstream.has_value()) {
                return;
            }

            server.sendPacket(
                *downstream,
                event.packet,
                bedrock::VersionedMcpeCompression::DeflateRaw
            );
        });

        upstream->onJoin([&]() {
            upstreamReady.store(true);
            std::vector<bedrock::VersionedGamePacket> queued;
            {
                std::lock_guard<std::mutex> lock(relayMutex);
                queued = std::move(pendingServerbound);
                pendingServerbound.clear();
            }
            for (const auto& packet : queued) {
                upstream->sendPacket(packet);
            }
        });

        upstreamThread = std::thread([&]() {
            upstream->run();
        });
    };

    server.onConnect([](const bedrock::BedrockServerConnection& connection) {
        std::cout << "[downstream] connect " << connection.address << ":" << connection.port << "\n";
    });

    server.onJoin([&](const bedrock::BedrockServerConnection& connection) {
        {
            std::lock_guard<std::mutex> lock(relayMutex);
            downstream = connection;
        }
        std::cout << "[downstream] joined " << connection.address << ":" << connection.port << "\n";
        startUpstream();
    });

    server.onAny([&](const bedrock::BedrockServerPacketEvent& event) {
        if (settings.printPackets) {
            std::cout << "[client -> upstream] " << event.packet.name << "\n";
        }

        if (isDownstreamHandshakePacket(event.packet.name)) {
            return;
        }

        if (!upstream || !upstreamReady.load()) {
            std::lock_guard<std::mutex> lock(relayMutex);
            pendingServerbound.push_back(event.packet);
            return;
        }

        upstream->sendPacket(event.packet);
    });

    std::cout << "Relay listener: " << settings.listenHost << ":" << settings.listenPort << "\n";
    std::cout << "Upstream: " << settings.upstreamHost << ":" << settings.upstreamPort
              << " version=" << settings.version << "\n";
    std::cout << "Join this relay from Minecraft, then watch packet logs here.\n";

    server.listen();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (upstreamThread.joinable()) {
        upstreamThread.join();
    }
}
