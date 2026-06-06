#include <bedrock/protocol/VersionedMcpeCodec.hpp>

#include <zlib.h>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace bedrock {

VersionedMcpeCodec::VersionedMcpeCodec(VersionedBatchCodec batchCodec)
    : batchCodec_(std::move(batchCodec)) {}

VersionedMcpeCodec VersionedMcpeCodec::forVersion(const std::string& minecraftVersion) {
    return VersionedMcpeCodec(VersionedBatchCodec::forVersion(minecraftVersion));
}

const VersionedBatchCodec& VersionedMcpeCodec::batchCodec() const {
    return batchCodec_;
}

const VersionedPacketCodec& VersionedMcpeCodec::packetCodec() const {
    return batchCodec_.packetCodec();
}

const ProtocolDefinition& VersionedMcpeCodec::definition() const {
    return batchCodec_.definition();
}

VersionedMcpePayload VersionedMcpeCodec::decodeMcpePayload(const std::vector<uint8_t>& mcpePayload) const {
    if (mcpePayload.empty()) {
        throw std::runtime_error("empty mcpe payload");
    }

    if (mcpePayload[0] != 0xfe) {
        throw std::runtime_error("mcpe payload missing 0xfe prefix");
    }

    if (mcpePayload.size() < 2) {
        throw std::runtime_error("mcpe payload missing compression header");
    }

    std::vector<uint8_t> compressionPacket(mcpePayload.begin() + 1, mcpePayload.end());
    return decodeCompressionPacket(compressionPacket);
}

VersionedMcpePayload VersionedMcpeCodec::decodeCompressionPacket(
    const std::vector<uint8_t>& compressionPacket
) const {
    if (compressionPacket.empty()) {
        throw std::runtime_error("empty compression packet");
    }

    VersionedMcpePayload payload;
    payload.compressionHeader = compressionPacket[0];
    payload.compressionPacket = compressionPacket;

    std::vector<uint8_t> body(compressionPacket.begin() + 1, compressionPacket.end());

    if (payload.compressionHeader == static_cast<uint8_t>(VersionedMcpeCompression::Uncompressed)) {
        payload.framedBatch = std::move(body);
    } else if (payload.compressionHeader == static_cast<uint8_t>(VersionedMcpeCompression::DeflateRaw)) {
        payload.framedBatch = inflateRaw(body);
    } else {
        throw std::runtime_error("unknown compression header: " + std::to_string(payload.compressionHeader));
    }

    payload.batch = batchCodec_.decodeFramedBatch(payload.framedBatch);
    return payload;
}

std::vector<uint8_t> VersionedMcpeCodec::encodeMcpePayload(
    const std::vector<VersionedGamePacket>& packets,
    VersionedMcpeCompression compression
) const {
    auto compressionPacket = encodeCompressionPacket(packets, compression);

    std::vector<uint8_t> out;
    out.reserve(1 + compressionPacket.size());
    out.push_back(0xfe);
    out.insert(out.end(), compressionPacket.begin(), compressionPacket.end());
    return out;
}

std::vector<uint8_t> VersionedMcpeCodec::encodeMcpePayloadByNames(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& packets,
    VersionedMcpeCompression compression
) const {
    auto compressionPacket = encodeCompressionPacketByNames(packets, compression);

    std::vector<uint8_t> out;
    out.reserve(1 + compressionPacket.size());
    out.push_back(0xfe);
    out.insert(out.end(), compressionPacket.begin(), compressionPacket.end());
    return out;
}

std::vector<uint8_t> VersionedMcpeCodec::encodeCompressionPacket(
    const std::vector<VersionedGamePacket>& packets,
    VersionedMcpeCompression compression
) const {
    auto framedBatch = batchCodec_.encodeFramedBatch(packets);

    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(compression));

    if (compression == VersionedMcpeCompression::Uncompressed) {
        out.insert(out.end(), framedBatch.begin(), framedBatch.end());
        return out;
    }

    if (compression == VersionedMcpeCompression::DeflateRaw) {
        auto compressed = deflateRaw(framedBatch);
        out.insert(out.end(), compressed.begin(), compressed.end());
        return out;
    }

    throw std::runtime_error("unsupported compression mode");
}

std::vector<uint8_t> VersionedMcpeCodec::encodeCompressionPacketByNames(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& packets,
    VersionedMcpeCompression compression
) const {
    std::vector<VersionedGamePacket> made;
    made.reserve(packets.size());

    for (const auto& item : packets) {
        made.push_back(packetCodec().makePacketByName(item.first, item.second));
    }

    return encodeCompressionPacket(made, compression);
}

std::vector<uint8_t> VersionedMcpeCodec::deflateRaw(const std::vector<uint8_t>& input) {
    z_stream stream{};

    int init = deflateInit2(
        &stream,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY
    );

    if (init != Z_OK) {
        throw std::runtime_error("deflateInit2 failed");
    }

    uint8_t dummy = 0;
    stream.next_in = input.empty()
        ? reinterpret_cast<Bytef*>(&dummy)
        : const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::vector<uint8_t> output;
    uint8_t buffer[32768];

    while (true) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);

        int ret = deflate(&stream, Z_FINISH);

        if (ret != Z_OK && ret != Z_STREAM_END) {
            deflateEnd(&stream);
            throw std::runtime_error("deflate failed");
        }

        std::size_t produced = sizeof(buffer) - stream.avail_out;
        output.insert(output.end(), buffer, buffer + produced);

        if (ret == Z_STREAM_END) {
            break;
        }
    }

    deflateEnd(&stream);
    return output;
}

std::vector<uint8_t> VersionedMcpeCodec::inflateRaw(const std::vector<uint8_t>& input) {
    z_stream stream{};

    int init = inflateInit2(&stream, -MAX_WBITS);
    if (init != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }

    uint8_t dummy = 0;
    stream.next_in = input.empty()
        ? reinterpret_cast<Bytef*>(&dummy)
        : const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::vector<uint8_t> output;
    uint8_t buffer[32768];

    while (true) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);

        int ret = inflate(&stream, Z_NO_FLUSH);
        std::size_t produced = sizeof(buffer) - stream.avail_out;

        output.insert(output.end(), buffer, buffer + produced);

        if (ret == Z_STREAM_END) {
            break;
        }

        if (ret != Z_OK) {
            inflateEnd(&stream);
            throw std::runtime_error("inflate failed");
        }

        if (stream.avail_in == 0 && produced == 0) {
            inflateEnd(&stream);
            throw std::runtime_error("incomplete deflate stream");
        }
    }

    inflateEnd(&stream);
    return output;
}

} // namespace bedrock
