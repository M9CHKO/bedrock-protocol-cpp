#include <bedrock/protocol/PacketFactory.hpp>

#include <bedrock/protocol/GamePacket.hpp>
#include <bedrock/protocol/GamePacketCodec.hpp>
#include <bedrock/util/VarInt.hpp>

#include <limits>

namespace bedrock {

std::vector<uint8_t> PacketFactory::networkSettingsRequest(uint32_t protocolVersion) {
    std::vector<uint8_t> payload;

    // Bedrock protocol version в request_network_settings/login идёт BE:
    // 827 = 00 00 03 3b
    VarInt::writeBe32(payload, protocolVersion);

    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::RequestNetworkSettings),
        payload
    );
}

std::vector<uint8_t> PacketFactory::clientToServerHandshake() {
    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::ClientToServerHandshake),
        {}
    );
}

std::vector<uint8_t> PacketFactory::resourcePackClientResponse(ResourcePackResponseStatus status) {
    std::vector<uint8_t> payload;

    payload.push_back(static_cast<uint8_t>(status));

    // resourcepackids count = 0
    VarInt::writeLe16(payload, 0);

    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::ResourcePackClientResponse),
        payload
    );
}

std::vector<uint8_t> PacketFactory::clientCacheStatus(bool enabled) {
    std::vector<uint8_t> payload;
    payload.push_back(enabled ? 1 : 0);

    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::ClientCacheStatus),
        payload
    );
}

std::vector<uint8_t> PacketFactory::requestChunkRadius(int32_t radius) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(PacketId::RequestChunkRadius));

    uint32_t encodedRadius =
        (static_cast<uint32_t>(radius) << 1) ^
        static_cast<uint32_t>(radius >> 31);

    while (encodedRadius >= 0x80) {
        out.push_back(static_cast<uint8_t>((encodedRadius & 0x7f) | 0x80));
        encodedRadius >>= 7;
    }

    out.push_back(static_cast<uint8_t>(encodedRadius));

    // max_radius = false
    out.push_back(0x00);

    return out;
}

std::vector<uint8_t> PacketFactory::setLocalPlayerInitializedMinusOne() {
    std::vector<uint8_t> payload = {
        0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
        0x7f
    };

    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::SetLocalPlayerAsInitialized),
        payload
    );
}

std::vector<uint8_t> PacketFactory::setLocalPlayerInitialized(uint64_t runtimeEntityId) {
    if (runtimeEntityId == std::numeric_limits<uint64_t>::max()) {
        return setLocalPlayerInitializedMinusOne();
    }

    std::vector<uint8_t> payload;
    VarInt::writeUnsignedVarLong(payload, runtimeEntityId);

    return GamePacketCodec::encodePacket(
        static_cast<uint32_t>(PacketId::SetLocalPlayerAsInitialized),
        payload
    );
}

} // namespace bedrock
