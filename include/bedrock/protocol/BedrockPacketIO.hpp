#pragma once

#include <bedrock/protocol/BatchCodec.hpp>
#include <bedrock/protocol/GamePacket.hpp>
#include <bedrock/protocol/PacketFactory.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bedrock {

class BedrockPacketIO {
public:
    // Берёт уже готовые GamePacket bytes:
    // [packetId + body]
    // и делает:
    // 0xfe + compressionHeader + framedPackets
    static std::vector<uint8_t> makeMcpeBatch(
        const std::vector<std::vector<uint8_t>>& fullPackets,
        CompressionMode compressionMode = CompressionMode::AlwaysDeflate,
        size_t compressionThreshold = 256
    );

    // Обратная операция:
    // 0xfe + compressionHeader + framedPackets -> DecodedBatch
    static DecodedBatch decodeUnencryptedMcpe(
        const std::vector<uint8_t>& mcpePayload
    );

    // Готовые пакеты, как bedrock-protocol write(...)
    static std::vector<uint8_t> makeClientToServerHandshakeMcpe();

    static std::vector<uint8_t> makeResourcePackHaveAllPacksMcpe();

    static std::vector<uint8_t> makeResourcePackCompletedMcpe();

    static std::vector<uint8_t> makeClientCacheStatusMcpe(bool enabled);

    static std::vector<uint8_t> makeRequestChunkRadiusMcpe(int32_t radius);

    static std::vector<uint8_t> makeSetLocalPlayerInitializedMcpe();

    // Post StartGame набор:
    // client_cache_status
    // request_chunk_radius
    // set_local_player_as_initialized
    static std::vector<std::vector<uint8_t>> makePostStartGameInitPackets(
        int32_t chunkRadius = 20,
        bool clientCacheEnabled = false
    );

    static std::vector<uint8_t> makePostStartGameInitMcpe(
        int32_t chunkRadius = 20,
        bool clientCacheEnabled = false
    );

    static std::string describePacket(const GamePacket& packet);
};

} // namespace bedrock
