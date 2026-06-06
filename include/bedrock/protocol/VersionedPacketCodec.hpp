#pragma once

#include <bedrock/protocol/ProtocolDefinition.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

struct VersionedGamePacket {
    uint32_t packetId = 0;
    std::string name;
    std::string paramsType;
    std::vector<uint8_t> fullPacket;
    std::vector<uint8_t> payload;
};

class VersionedPacketCodec {
public:
    explicit VersionedPacketCodec(ProtocolDefinition definition)
        : definition_(std::move(definition)) {}

    static VersionedPacketCodec forVersion(const std::string& minecraftVersion) {
        return VersionedPacketCodec(ProtocolDefinition::forVersion(minecraftVersion));
    }

    const ProtocolDefinition& definition() const {
        return definition_;
    }

    VersionedGamePacket decodeFullPacket(const std::vector<uint8_t>& fullPacket) const {
        std::size_t offset = 0;
        const uint32_t packetId = readVarUInt(fullPacket, offset);

        VersionedGamePacket packet;
        packet.packetId = packetId;
        packet.name = definition_.packetName(packetId);
        packet.paramsType = definition_.packetParamsType(packetId);
        packet.fullPacket = fullPacket;

        if (offset > fullPacket.size()) {
            throw std::runtime_error("packet id offset overflow");
        }

        packet.payload.assign(fullPacket.begin() + static_cast<std::ptrdiff_t>(offset), fullPacket.end());
        return packet;
    }

    VersionedGamePacket makePacketById(uint32_t packetId, const std::vector<uint8_t>& payload = {}) const {
        VersionedGamePacket packet;
        packet.packetId = packetId;
        packet.name = definition_.packetName(packetId);
        packet.paramsType = definition_.packetParamsType(packetId);
        packet.payload = payload;
        packet.fullPacket = encodeFullPacketById(packetId, payload);
        return packet;
    }

    VersionedGamePacket makePacketByName(const std::string& packetName, const std::vector<uint8_t>& payload = {}) const {
        const uint32_t packetId = definition_.packetId(packetName);

        VersionedGamePacket packet;
        packet.packetId = packetId;
        packet.name = packetName;
        packet.paramsType = definition_.packetParamsType(packetName);
        packet.payload = payload;
        packet.fullPacket = encodeFullPacketById(packetId, payload);
        return packet;
    }

    std::vector<uint8_t> encodeFullPacketById(uint32_t packetId, const std::vector<uint8_t>& payload = {}) const {
        std::vector<uint8_t> out;
        writeVarUInt(out, packetId);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    std::vector<uint8_t> encodeFullPacketByName(const std::string& packetName, const std::vector<uint8_t>& payload = {}) const {
        return encodeFullPacketById(definition_.packetId(packetName), payload);
    }

    static void writeVarUInt(std::vector<uint8_t>& out, uint32_t value) {
        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
            value >>= 7u;

            if (value != 0) {
                byte |= 0x80u;
            }

            out.push_back(byte);
        } while (value != 0);
    }

    static uint32_t readVarUInt(const std::vector<uint8_t>& data, std::size_t& offset) {
        uint32_t result = 0;
        uint32_t shift = 0;

        while (true) {
            if (offset >= data.size()) {
                throw std::runtime_error("unexpected end while reading varuint");
            }

            const uint8_t byte = data[offset++];
            result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;

            if ((byte & 0x80u) == 0) {
                break;
            }

            shift += 7;
            if (shift >= 35) {
                throw std::runtime_error("varuint too large");
            }
        }

        return result;
    }

private:
    ProtocolDefinition definition_;
};

} // namespace bedrock
