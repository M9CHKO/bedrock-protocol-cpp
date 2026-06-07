#include <bedrock/world/BedrockChunk.hpp>

#include <iostream>
#include <vector>

static void writeVarUInt(std::vector<uint8_t>& out, uint32_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7f);
        value >>= 7u;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
}

static void writeZigZagVarInt(std::vector<uint8_t>& out, int32_t value) {
    const uint32_t encoded =
        (static_cast<uint32_t>(value) << 1u) ^
        static_cast<uint32_t>(value >> 31);
    writeVarUInt(out, encoded);
}

int main() {
    const int32_t stateId = 42;

    std::vector<uint8_t> encoded;
    encoded.push_back(9); // subChunkVersion
    encoded.push_back(1); // storage count
    encoded.push_back(0); // y
    encoded.push_back(1); // palette type: runtime ids + bitsPerBlock 0
    writeZigZagVarInt(encoded, stateId);

    auto sub = bedrock::BedrockSubChunk::decode(
        bedrock::ChunkStorageType::Runtime,
        encoded
    );

    if (sub.getBlockStateId(0, 0, 0) != stateId) {
        std::cerr << "[CHUNK-SMOKE] single-state block mismatch\n";
        return 1;
    }
    if (sub.palette(0).size() != 1) {
        std::cerr << "[CHUNK-SMOKE] single-state palette size mismatch\n";
        return 1;
    }

    auto dumped = sub.encode(bedrock::ChunkStorageType::Runtime);
    if (dumped != encoded) {
        std::cerr << "[CHUNK-SMOKE] single-state roundtrip mismatch\n";
        return 1;
    }

    bedrock::BedrockChunkColumn column(3, 5);
    column.setBlockStateId({.x = 17, .y = 32, .z = -1}, 77);
    if (column.getBlockStateId({.x = 1, .y = 32, .z = 15}) != 77) {
        std::cerr << "[CHUNK-SMOKE] column local coordinate mismatch\n";
        return 1;
    }

    bedrock::BedrockBiomeSection biome(0);
    biome.setBiomeId(1, 2, 3, 7);
    bedrock::BinaryStream biomeStream;
    biome.exportTo(bedrock::ChunkStorageType::Runtime, biomeStream);
    bedrock::BinaryStream biomeRead(biomeStream.buffer());
    bedrock::BedrockBiomeSection decodedBiome(0);
    decodedBiome.read(bedrock::ChunkStorageType::Runtime, biomeRead);
    if (decodedBiome.getBiomeId(1, 2, 3) != 7) {
        std::cerr << "[CHUNK-SMOKE] biome section roundtrip mismatch\n";
        return 1;
    }

    bedrock::BedrockChunkColumn netColumn(0, 0);
    netColumn.setBounds(-4, 20);
    netColumn.setBlockStateId({.x = 4, .y = -16, .z = 5}, 123);
    netColumn.setBiomeId({.x = 4, .y = -16, .z = 5}, 9);
    auto payload = netColumn.networkEncodeNoCache();

    bedrock::BedrockChunkColumn decodedColumn(0, 0);
    decodedColumn.setBounds(-4, 20);
    decodedColumn.networkDecodeNoCache(payload, 1);
    if (decodedColumn.getBlockStateId({.x = 4, .y = -16, .z = 5}) != 123) {
        std::cerr << "[CHUNK-SMOKE] network no-cache block mismatch\n";
        return 1;
    }
    if (decodedColumn.getBiomeId({.x = 4, .y = -16, .z = 5}) != 9) {
        std::cerr << "[CHUNK-SMOKE] network no-cache biome mismatch\n";
        return 1;
    }

    auto levelPacket = bedrock::BedrockLevelChunkCodec::encodeNoCacheColumn(netColumn, 0);
    auto packetPayload = bedrock::BedrockLevelChunkCodec::encodePacketPayload(levelPacket);
    auto decodedPacket = bedrock::BedrockLevelChunkCodec::decodePacketPayload(packetPayload);
    auto decodedPacketColumn = bedrock::BedrockLevelChunkCodec::decodeNoCacheColumn(decodedPacket);
    if (decodedPacket.x != 0 || decodedPacket.z != 0 || decodedPacket.cacheEnabled) {
        std::cerr << "[CHUNK-SMOKE] level_chunk packet metadata mismatch\n";
        return 1;
    }
    if (decodedPacketColumn.getBlockStateId({.x = 4, .y = -16, .z = 5}) != 123) {
        std::cerr << "[CHUNK-SMOKE] level_chunk packet column mismatch\n";
        return 1;
    }

    bedrock::BedrockWorld world;
    world.setLoadedColumn(0, 0, netColumn);
    if (world.getBlockStateId({.x = 4, .y = -16, .z = 5}) != 123) {
        std::cerr << "[CHUNK-SMOKE] world block lookup mismatch\n";
        return 1;
    }
    bedrock::BedrockChunkColumn negativeColumn(-1, -1);
    negativeColumn.setBounds(-4, 20);
    negativeColumn.setBlockStateId({.x = -1, .y = 0, .z = -1}, 88);
    world.setLoadedColumn(-1, -1, negativeColumn);
    if (world.getBlockStateId({.x = -1, .y = 0, .z = -1}) != 88) {
        std::cerr << "[CHUNK-SMOKE] world negative coordinate mismatch\n";
        return 1;
    }

    bedrock::BedrockClientCacheBlobStatus cacheStatus;
    cacheStatus.missing = {0x1122334455667788ull};
    cacheStatus.have = {0x8877665544332211ull};
    auto cacheStatusPayload = bedrock::BedrockLevelChunkCodec::encodeClientCacheBlobStatusPayload(cacheStatus);
    if (cacheStatusPayload.empty()) {
        std::cerr << "[CHUNK-SMOKE] cache status payload empty\n";
        return 1;
    }

    std::vector<uint8_t> missPayload;
    writeVarUInt(missPayload, 1);
    const uint64_t hash = 0x0102030405060708ull;
    for (int i = 0; i < 8; ++i) {
        missPayload.push_back(static_cast<uint8_t>((hash >> (8 * i)) & 0xff));
    }
    writeVarUInt(missPayload, 3);
    missPayload.push_back(1);
    missPayload.push_back(2);
    missPayload.push_back(3);
    auto blobs = bedrock::BedrockLevelChunkCodec::decodeClientCacheMissResponsePayload(missPayload);
    if (blobs.size() != 1 || blobs[0].hash != hash || blobs[0].payload.size() != 3) {
        std::cerr << "[CHUNK-SMOKE] cache miss response mismatch\n";
        return 1;
    }

    std::cout << "[CHUNK-SMOKE] ok\n";
    return 0;
}
