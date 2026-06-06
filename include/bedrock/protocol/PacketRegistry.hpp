#pragma once

#include <cstdint>
#include <string>

namespace bedrock {

class PacketRegistry {
public:
    static std::string nameOf(uint32_t packetId);
};

} // namespace bedrock
