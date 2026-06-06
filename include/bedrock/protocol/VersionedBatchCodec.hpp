#pragma once

#include <bedrock/protocol/VersionedPacketCodec.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

struct VersionedPacketBatch {
    std::vector<VersionedGamePacket> packets;

    bool empty() const {
        return packets.empty();
    }

    std::size_t size() const {
        return packets.size();
    }
};

class VersionedBatchCodec {
public:
    explicit VersionedBatchCodec(VersionedPacketCodec packetCodec)
        : packetCodec_(std::move(packetCodec)) {}

    static VersionedBatchCodec forVersion(const std::string& minecraftVersion) {
        return VersionedBatchCodec(VersionedPacketCodec::forVersion(minecraftVersion));
    }

    const VersionedPacketCodec& packetCodec() const {
        return packetCodec_;
    }

    const ProtocolDefinition& definition() const {
        return packetCodec_.definition();
    }

    VersionedPacketBatch decodeFramedBatch(const std::vector<uint8_t>& framedBatch) const {
        VersionedPacketBatch batch;

        std::size_t offset = 0;

        while (offset < framedBatch.size()) {
            const uint32_t packetSize = VersionedPacketCodec::readVarUInt(framedBatch, offset);

            if (packetSize == 0) {
                throw std::runtime_error("framed batch contains empty packet");
            }

            if (offset + packetSize > framedBatch.size()) {
                throw std::runtime_error("framed batch packet exceeds buffer size");
            }

            std::vector<uint8_t> fullPacket(
                framedBatch.begin() + static_cast<std::ptrdiff_t>(offset),
                framedBatch.begin() + static_cast<std::ptrdiff_t>(offset + packetSize)
            );

            offset += packetSize;

            batch.packets.push_back(packetCodec_.decodeFullPacket(fullPacket));
        }

        return batch;
    }

    std::vector<uint8_t> encodeFramedBatch(const std::vector<VersionedGamePacket>& packets) const {
        std::vector<uint8_t> out;

        for (const auto& packet : packets) {
            if (packet.fullPacket.empty()) {
                throw std::runtime_error("cannot encode packet with empty fullPacket");
            }

            VersionedPacketCodec::writeVarUInt(out, static_cast<uint32_t>(packet.fullPacket.size()));
            out.insert(out.end(), packet.fullPacket.begin(), packet.fullPacket.end());
        }

        return out;
    }

    std::vector<uint8_t> encodeFramedBatchFromFullPackets(
        const std::vector<std::vector<uint8_t>>& fullPackets
    ) const {
        std::vector<uint8_t> out;

        for (const auto& fullPacket : fullPackets) {
            if (fullPacket.empty()) {
                throw std::runtime_error("cannot encode empty full packet");
            }

            VersionedPacketCodec::writeVarUInt(out, static_cast<uint32_t>(fullPacket.size()));
            out.insert(out.end(), fullPacket.begin(), fullPacket.end());
        }

        return out;
    }

    std::vector<uint8_t> encodeFramedBatchByNames(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& packets
    ) const {
        std::vector<VersionedGamePacket> made;
        made.reserve(packets.size());

        for (const auto& item : packets) {
            made.push_back(packetCodec_.makePacketByName(item.first, item.second));
        }

        return encodeFramedBatch(made);
    }

private:
    VersionedPacketCodec packetCodec_;
};

} // namespace bedrock
