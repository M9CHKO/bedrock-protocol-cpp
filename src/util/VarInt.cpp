#include <bedrock/util/VarInt.hpp>

#include <stdexcept>

namespace bedrock {

uint32_t VarInt::readUnsignedVarInt(const std::vector<uint8_t>& data, size_t& offset) {
    uint32_t result = 0;

    for (int shift = 0; shift <= 28; shift += 7) {
        if (offset >= data.size()) {
            throw std::runtime_error("readUnsignedVarInt: unexpected end");
        }

        const uint8_t byte = data[offset++];
        result |= static_cast<uint32_t>(byte & 0x7f) << shift;

        if ((byte & 0x80) == 0) {
            return result;
        }
    }

    throw std::runtime_error("readUnsignedVarInt: varint too large");
}

uint64_t VarInt::readUnsignedVarLong(const std::vector<uint8_t>& data, size_t& offset) {
    uint64_t result = 0;

    for (int shift = 0; shift <= 63; shift += 7) {
        if (offset >= data.size()) {
            throw std::runtime_error("readUnsignedVarLong: unexpected end");
        }

        const uint8_t byte = data[offset++];
        result |= static_cast<uint64_t>(byte & 0x7f) << shift;

        if ((byte & 0x80) == 0) {
            return result;
        }
    }

    throw std::runtime_error("readUnsignedVarLong: varlong too large");
}

void VarInt::writeUnsignedVarInt(std::vector<uint8_t>& out, uint32_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7f);
        value >>= 7;

        if (value != 0) {
            byte |= 0x80;
        }

        out.push_back(byte);
    } while (value != 0);
}

void VarInt::writeUnsignedVarLong(std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7f);
        value >>= 7;

        if (value != 0) {
            byte |= 0x80;
        }

        out.push_back(byte);
    } while (value != 0);
}

void VarInt::writeBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void VarInt::writeLe16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

} // namespace bedrock
