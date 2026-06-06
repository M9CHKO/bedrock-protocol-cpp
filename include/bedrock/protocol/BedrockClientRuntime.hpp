#pragma once

#include <bedrock/protocol/BedrockProtocolClient.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bedrock {

class BedrockClientRuntime {
public:
    using Handler = PacketDispatcher::Handler;

    explicit BedrockClientRuntime(BedrockProtocolClientOptions options = {});

    void on(const std::string& packetName, Handler handler);
    void onId(uint32_t packetId, Handler handler);
    void onAny(Handler handler);

    void handle(const GamePacket& packet);
    void handle(const DecodedBatch& batch);
    void handle(const std::vector<GamePacket>& packets);

    void writeRaw(uint32_t packetId, const std::string& name, std::vector<uint8_t> raw);

    void writeNetworkSettingsRequest(uint32_t protocol);
    void writeClientToServerHandshake();

    void writeResourcePackHaveAllPacks();
    void writeResourcePackCompleted();

    void writeClientCacheStatus(bool supported);
    void writeRequestChunkRadius(int32_t radius);
    void writeSetLocalPlayerAsInitialized(int64_t runtimeEntityId = -1);

    std::vector<OutgoingGamePacket> drainOutgoing();
    std::vector<std::vector<uint8_t>> drainOutgoingRaw();

    bool seenStartGame() const;
    bool sentPostStartGameInit() const;
    size_t queuedOutgoingCount() const;

    void clearOutgoing();

private:
    BedrockProtocolClient protocol_;
    std::vector<OutgoingGamePacket> manualOutgoing_;

    void queueManual(uint32_t packetId, const std::string& name, std::vector<uint8_t> raw);

    static void writeU16LE(std::vector<uint8_t>& out, uint16_t value);
    static void writeI64Var(std::vector<uint8_t>& out, int64_t value);
};

} // namespace bedrock
