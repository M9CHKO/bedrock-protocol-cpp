#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bedrock {

struct RakNetServerOptions {
    std::string host = "0.0.0.0";
    uint16_t port = 19132;
    int maxPlayers = 3;
    int protocolVersion = 11;
    uint64_t serverGuid = 0;
    std::string advertisement;
};

struct RakNetServerPeer {
    std::string address;
    uint16_t port = 0;
    uint64_t clientGuid = 0;
    int mtu = 1400;
};

class RakNetServer {
public:
    using OpenConnectionHandler = std::function<void(const RakNetServerPeer&)>;
    using RawPacketHandler = std::function<void(const RakNetServerPeer&, const std::vector<uint8_t>&)>;
    using EncapsulatedHandler = std::function<void(const RakNetServerPeer&, const std::vector<uint8_t>&)>;

    explicit RakNetServer(RakNetServerOptions options = {});
    ~RakNetServer();

    RakNetServer(const RakNetServer&) = delete;
    RakNetServer& operator=(const RakNetServer&) = delete;

    RakNetServer(RakNetServer&& other) noexcept;
    RakNetServer& operator=(RakNetServer&& other) noexcept;

    void listen();
    void close();

    bool listening() const {
        return running_;
    }

    uint16_t boundPort() const {
        return boundPort_;
    }

    const RakNetServerOptions& options() const {
        return options_;
    }

    void setAdvertisement(std::string advertisement) {
        options_.advertisement = std::move(advertisement);
    }

    void onOpenConnection(OpenConnectionHandler handler) {
        openConnectionHandler_ = std::move(handler);
    }

    void onRawPacket(RawPacketHandler handler) {
        rawPacketHandler_ = std::move(handler);
    }

    void onEncapsulated(EncapsulatedHandler handler) {
        encapsulatedHandler_ = std::move(handler);
    }

    void sendReliable(const RakNetServerPeer& peer, const std::vector<uint8_t>& payload);

private:
    struct PeerState {
        struct SplitAccumulator {
            uint32_t count = 0;
            std::vector<std::vector<uint8_t>> parts;
            std::vector<bool> received;
        };

        RakNetServerPeer peer;
        std::array<uint8_t, 128> endpoint {};
        int endpointLen = 0;
        uint32_t outgoingSequence = 0;
        uint32_t reliableIndex = 0;
        uint32_t orderedIndex = 0;
        uint16_t outgoingSplitId = 1;
        std::unordered_map<uint16_t, SplitAccumulator> splits;
    };

    RakNetServerOptions options_;
    int socket_ = -1;
    uint16_t boundPort_ = 0;
    std::atomic<bool> running_ { false };
    std::thread thread_;
    OpenConnectionHandler openConnectionHandler_;
    RawPacketHandler rawPacketHandler_;
    EncapsulatedHandler encapsulatedHandler_;
    std::mutex peersMutex_;
    std::unordered_map<std::string, PeerState> peers_;

    void runLoop();
    void handlePacket(const std::vector<uint8_t>& packet, const void* sender, int senderLen);
    void sendTo(const void* target, int targetLen, const std::vector<uint8_t>& packet);
    void sendReliableOrdered(const RakNetServerPeer& peer, const void* target, int targetLen, const std::vector<uint8_t>& payload);
};

} // namespace bedrock
