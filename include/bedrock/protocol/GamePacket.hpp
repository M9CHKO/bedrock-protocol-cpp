#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bedrock {

enum class PacketId : uint32_t {
    Login = 1,
    PlayStatus = 2,
    ServerToClientHandshake = 3,
    ClientToServerHandshake = 4,

    ResourcePacksInfo = 6,
    ResourcePackStack = 7,
    ResourcePackClientResponse = 8,

    Text = 9,
    StartGame = 11,

    LevelChunk = 58,

    RequestChunkRadius = 69,
    ChunkRadiusUpdated = 70,

    SetLocalPlayerAsInitialized = 113,
    ClientCacheBlobStatus = 121,
    ClientCacheStatus = 129,

    RequestNetworkSettings = 193
};

struct GamePacket {
    uint32_t packetId = 0;
    uint8_t senderSubId = 0;
    uint8_t targetSubId = 0;

    std::string name;

    // Body без packet header.
    std::vector<uint8_t> payload;

    // Полный Minecraft game packet: header varuint + payload.
    std::vector<uint8_t> fullPacket;
};

struct DecodedBatch {
    std::vector<GamePacket> packets;
};

} // namespace bedrock
