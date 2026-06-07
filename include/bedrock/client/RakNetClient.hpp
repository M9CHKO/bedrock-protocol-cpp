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

struct RakNetClientOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 19132;
    int mtu = 1400;
    int protocolVersion = 11;
    uint64_t clientGuid = 0;
    int timeoutMs = 3000;
};

class RakNetClient {
public:
    using ConnectedHandler = std::function<void()>;
    using CloseHandler = std::function<void(const std::string&)>;
    using EncapsulatedHandler = std::function<void(const std::vector<uint8_t>&)>;

    explicit RakNetClient(RakNetClientOptions options = {});
    ~RakNetClient();

    RakNetClient(const RakNetClient&) = delete;
    RakNetClient& operator=(const RakNetClient&) = delete;

    RakNetClient(RakNetClient&& other) noexcept;
    RakNetClient& operator=(RakNetClient&& other) noexcept;

    bool connect();
    void close(const std::string& reason = "closed");
    void sendReliable(const std::vector<uint8_t>& payload);

    bool connected() const {
        return connected_;
    }

    uint16_t localPort() const {
        return localPort_;
    }

    int mtu() const {
        return mtu_;
    }

    std::string error() const {
        return error_;
    }

    void onConnected(ConnectedHandler handler) {
        connectedHandler_ = std::move(handler);
    }

    void onClose(CloseHandler handler) {
        closeHandler_ = std::move(handler);
    }

    void onEncapsulated(EncapsulatedHandler handler) {
        encapsulatedHandler_ = std::move(handler);
    }

private:
    struct SplitAccumulator {
        uint32_t count = 0;
        std::vector<std::vector<uint8_t>> parts;
        std::vector<bool> received;
    };

    RakNetClientOptions options_;
    int socket_ = -1;
    uint16_t localPort_ = 0;
    int mtu_ = 1400;
    std::array<uint8_t, 128> target_ {};
    int targetLen_ = 0;
    std::atomic<bool> running_ { false };
    std::atomic<bool> connected_ { false };
    std::thread thread_;
    std::string error_;

    ConnectedHandler connectedHandler_;
    CloseHandler closeHandler_;
    EncapsulatedHandler encapsulatedHandler_;

    std::mutex stateMutex_;
    uint32_t outgoingSequence_ = 0;
    uint32_t reliableIndex_ = 0;
    uint32_t orderedIndex_ = 0;
    uint16_t outgoingSplitId_ = 1;
    std::unordered_map<uint16_t, SplitAccumulator> splits_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> sentReliableDatagrams_;

    void runLoop();
    void handlePacket(const std::vector<uint8_t>& packet);
    void sendToTarget(const std::vector<uint8_t>& packet);
    void sendConnectionRequest();
    void sendConnectedPong(int64_t pingTime);
};

} // namespace bedrock
