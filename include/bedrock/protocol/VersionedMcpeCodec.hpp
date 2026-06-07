#pragma once

#include <bedrock/protocol/VersionedBatchCodec.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bedrock {

enum class VersionedMcpeCompression : uint8_t {
    DeflateRaw = 0x00,
    Uncompressed = 0xff
};

struct VersionedMcpePayload {
    uint8_t compressionHeader = 0xff;
    std::vector<uint8_t> compressionPacket;
    std::vector<uint8_t> framedBatch;
    VersionedPacketBatch batch;
};

class VersionedMcpeCodec {
public:
    explicit VersionedMcpeCodec(VersionedBatchCodec batchCodec);

    static VersionedMcpeCodec forVersion(const std::string& minecraftVersion);

    const VersionedBatchCodec& batchCodec() const;
    const VersionedPacketCodec& packetCodec() const;
    const ProtocolDefinition& definition() const;

    VersionedMcpePayload decodeMcpePayload(const std::vector<uint8_t>& mcpePayload) const;
    VersionedMcpePayload decodeCompressionPacket(const std::vector<uint8_t>& compressionPacket) const;

    std::vector<uint8_t> encodeMcpePayload(
        const std::vector<VersionedGamePacket>& packets,
        VersionedMcpeCompression compression = VersionedMcpeCompression::DeflateRaw
    ) const;

    std::vector<uint8_t> encodeMcpePayloadByNames(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& packets,
        VersionedMcpeCompression compression = VersionedMcpeCompression::DeflateRaw
    ) const;

    std::vector<uint8_t> encodeCompressionPacket(
        const std::vector<VersionedGamePacket>& packets,
        VersionedMcpeCompression compression = VersionedMcpeCompression::DeflateRaw
    ) const;

    std::vector<uint8_t> encodeCompressionPacketByNames(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& packets,
        VersionedMcpeCompression compression = VersionedMcpeCompression::DeflateRaw
    ) const;

private:
    VersionedBatchCodec batchCodec_;
    bool compressorInPacketHeader_ = true;

    static std::vector<uint8_t> deflateRaw(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> inflateRaw(const std::vector<uint8_t>& input);
    static bool versionAtLeast(const std::string& version, int major, int minor, int patch);
};

} // namespace bedrock
