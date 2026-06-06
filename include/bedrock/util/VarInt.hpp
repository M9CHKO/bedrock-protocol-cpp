#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bedrock {

class VarInt {
public:
    static uint32_t readUnsignedVarInt(const std::vector<uint8_t>& data, size_t& offset);
    static uint64_t readUnsignedVarLong(const std::vector<uint8_t>& data, size_t& offset);

    static void writeUnsignedVarInt(std::vector<uint8_t>& out, uint32_t value);
    static void writeUnsignedVarLong(std::vector<uint8_t>& out, uint64_t value);

    static void writeBe32(std::vector<uint8_t>& out, uint32_t value);
    static void writeLe16(std::vector<uint8_t>& out, uint16_t value);
};

} // namespace bedrock
