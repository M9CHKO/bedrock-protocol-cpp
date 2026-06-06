#pragma once

#include <bedrock/protocol/BatchCodec.hpp>
#include <bedrock/protocol/GamePacket.hpp>
#include <bedrock/protocol/PacketDispatcher.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bedrock {

struct OutgoingGamePacket {
    uint32_t packetId{};
    std::string name;
    std::vector<uint8_t> raw;
};

struct BedrockProtocolClientOptions {
    bool autoResourcePackResponse = true;
    bool autoPostStartGameInit = true;
    bool clientCacheSupported = false;
    int32_t chunkRadius = 20;
};

class BedrockProtocolClient {
public:
    using Handler = PacketDispatcher::Handler;

    explicit BedrockProtocolClient(BedrockProtocolClientOptions options = {});

    void on(const std::string& packetName, Handler handler);
    void onId(uint32_t packetId, Handler handler);
    void onAny(Handler handler);

    void handle(const GamePacket& packet);
    void handle(const DecodedBatch& batch);
    void handle(const std::vector<GamePacket>& packets);

    std::vector<OutgoingGamePacket> drainOutgoing();

    bool seenStartGame() const;
    bool sentPostStartGameInit() const;
    size_t queuedOutgoingCount() const;

    void clearHandlers();
    void clearOutgoing();

private:
    BedrockProtocolClientOptions options_;
    PacketDispatcher dispatcher_;
    std::vector<OutgoingGamePacket> outgoing_;

    bool seenStartGame_ = false;
    bool sentPostStartGameInit_ = false;

    void autoRespond(const GamePacket& packet);

    void queueRaw(uint32_t packetId, const std::string& name, std::vector<uint8_t> raw);

    void queueResourcePackHaveAllPacks();
    void queueResourcePackCompleted();
    void queuePostStartGameInit();

    static std::vector<uint8_t> makeClientCacheStatus(bool supported);
    static std::vector<uint8_t> makeRequestChunkRadius(int32_t radius);
    static std::vector<uint8_t> makeSetLocalPlayerAsInitializedMinusOne();
};

} // namespace bedrock
