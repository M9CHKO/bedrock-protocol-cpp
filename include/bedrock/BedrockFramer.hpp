#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>

namespace bedrock {

class BedrockFramerError : public std::runtime_error {
public:
    explicit BedrockFramerError(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct BedrockFramerSettings {
    bool compressionReady = false;
    bool compressorInHeader = true;

    // From NetworkSettings:
    // threshold=256
    // algorithm=0 deflate
    uint16_t compressionThreshold = 256;
    uint16_t compressionAlgorithm = 0;
};

class BedrockFramer {
public:
    static constexpr uint8_t BATCH_HEADER = 0xfe;
    static constexpr uint8_t COMPRESSION_DEFLATE = 0x00;
    static constexpr uint8_t COMPRESSION_SNAPPY = 0x01;
    static constexpr uint8_t COMPRESSION_NONE = 0xff;

    static std::vector<uint8_t> encodeBatch(
        const std::vector<std::vector<uint8_t>>& packets,
        const BedrockFramerSettings& settings
    );

    static std::vector<std::vector<uint8_t>> decodeBatch(
        const std::vector<uint8_t>& batch,
        const BedrockFramerSettings& settings
    );

    static std::vector<uint8_t> framePackets(
        const std::vector<std::vector<uint8_t>>& packets
    );

    static std::vector<std::vector<uint8_t>> unframePackets(
        const std::vector<uint8_t>& framed
    );

    static std::vector<uint8_t> deflateRaw(
        const std::vector<uint8_t>& input
    );

    static std::vector<uint8_t> inflateRaw(
        const std::vector<uint8_t>& input
    );

private:
    static void writeVarUInt(std::vector<uint8_t>& out, uint32_t v);
    static uint32_t readVarUInt(const std::vector<uint8_t>& data, size_t& off);
};

} // namespace bedrock
