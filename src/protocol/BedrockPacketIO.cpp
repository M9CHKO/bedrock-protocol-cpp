#include <bedrock/protocol/BedrockPacketIO.hpp>

#include <sstream>

namespace bedrock {

std::vector<uint8_t> BedrockPacketIO::makeMcpeBatch(
    const std::vector<std::vector<uint8_t>>& fullPackets,
    CompressionMode compressionMode,
    size_t compressionThreshold
) {
    auto framed = BatchCodec::encodeFramedBatch(fullPackets);

    auto compressionPacket = BatchCodec::encodeCompressionPacket(
        framed,
        compressionMode,
        compressionThreshold
    );

    return BatchCodec::wrapMcpe(compressionPacket);
}

DecodedBatch BedrockPacketIO::decodeUnencryptedMcpe(
    const std::vector<uint8_t>& mcpePayload
) {
    auto compressionPacket = BatchCodec::unwrapMcpe(mcpePayload);
    auto framed = BatchCodec::decodeCompressionPacket(compressionPacket);
    return BatchCodec::decodeFramedBatch(framed);
}

std::vector<uint8_t> BedrockPacketIO::makeClientToServerHandshakeMcpe() {
    return makeMcpeBatch({
        PacketFactory::clientToServerHandshake()
    });
}

std::vector<uint8_t> BedrockPacketIO::makeResourcePackHaveAllPacksMcpe() {
    return makeMcpeBatch({
        PacketFactory::resourcePackClientResponse(
            ResourcePackResponseStatus::HaveAllPacks
        )
    });
}

std::vector<uint8_t> BedrockPacketIO::makeResourcePackCompletedMcpe() {
    return makeMcpeBatch({
        PacketFactory::resourcePackClientResponse(
            ResourcePackResponseStatus::Completed
        )
    });
}

std::vector<uint8_t> BedrockPacketIO::makeClientCacheStatusMcpe(bool enabled) {
    return makeMcpeBatch({
        PacketFactory::clientCacheStatus(enabled)
    });
}

std::vector<uint8_t> BedrockPacketIO::makeRequestChunkRadiusMcpe(int32_t radius) {
    return makeMcpeBatch({
        PacketFactory::requestChunkRadius(radius)
    });
}

std::vector<uint8_t> BedrockPacketIO::makeSetLocalPlayerInitializedMcpe() {
    return makeMcpeBatch({
        PacketFactory::setLocalPlayerInitializedMinusOne()
    });
}

std::vector<std::vector<uint8_t>> BedrockPacketIO::makePostStartGameInitPackets(
    int32_t chunkRadius,
    bool clientCacheEnabled
) {
    return {
        PacketFactory::clientCacheStatus(clientCacheEnabled),
        PacketFactory::requestChunkRadius(chunkRadius),
        PacketFactory::setLocalPlayerInitializedMinusOne()
    };
}

std::vector<uint8_t> BedrockPacketIO::makePostStartGameInitMcpe(
    int32_t chunkRadius,
    bool clientCacheEnabled
) {
    return makeMcpeBatch(
        makePostStartGameInitPackets(chunkRadius, clientCacheEnabled)
    );
}

std::string BedrockPacketIO::describePacket(const GamePacket& packet) {
    std::ostringstream ss;

    ss << "packetId="
       << packet.packetId
       << " name="
       << packet.name
       << " payload="
       << packet.payload.size();

    return ss.str();
}

} // namespace bedrock
