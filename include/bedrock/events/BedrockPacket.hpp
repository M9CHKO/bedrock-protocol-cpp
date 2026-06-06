#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bedrock {

struct BedrockPacketData {
    std::string name;
    std::map<std::string, std::string> params;
};

struct BedrockPacketMetadata {
    std::size_t size = 0;
};

struct BedrockPacket {
    BedrockPacketData data;
    BedrockPacketMetadata metadata;

    // payload without packet header
    std::vector<uint8_t> buffer;

    // full game packet including header
    std::vector<uint8_t> fullBuffer;

    uint32_t packetId = 0;
    std::string decodeError;
};

using BedrockPacketHandler = std::function<void(const BedrockPacket&)>;

} // namespace bedrock
