#include <bedrock/protocol/BedrockClientRuntime.hpp>

#include <algorithm>
#include <utility>

namespace bedrock {

BedrockClientRuntime::BedrockClientRuntime(BedrockProtocolClientOptions options)
    : protocol_(options) {
}

void BedrockClientRuntime::on(const std::string& packetName, Handler handler) {
    protocol_.on(packetName, std::move(handler));
}

void BedrockClientRuntime::onId(uint32_t packetId, Handler handler) {
    protocol_.onId(packetId, std::move(handler));
}

void BedrockClientRuntime::onAny(Handler handler) {
    protocol_.onAny(std::move(handler));
}

void BedrockClientRuntime::handle(const GamePacket& packet) {
    protocol_.handle(packet);
}

void BedrockClientRuntime::handle(const DecodedBatch& batch) {
    protocol_.handle(batch);
}

void BedrockClientRuntime::handle(const std::vector<GamePacket>& packets) {
    protocol_.handle(packets);
}

void BedrockClientRuntime::writeRaw(
    uint32_t packetId,
    const std::string& name,
    std::vector<uint8_t> raw
) {
    queueManual(packetId, name, std::move(raw));
}

void BedrockClientRuntime::writeNetworkSettingsRequest(uint32_t protocol) {
    std::vector<uint8_t> raw;

    raw.push_back(0xc1);
    raw.push_back(0x01);

    raw.push_back(static_cast<uint8_t>((protocol >> 24) & 0xff));
    raw.push_back(static_cast<uint8_t>((protocol >> 16) & 0xff));
    raw.push_back(static_cast<uint8_t>((protocol >> 8) & 0xff));
    raw.push_back(static_cast<uint8_t>(protocol & 0xff));

    queueManual(193, "request_network_settings", std::move(raw));
}

void BedrockClientRuntime::writeClientToServerHandshake() {
    queueManual(
        4,
        "client_to_server_handshake",
        { 0x04 }
    );
}

void BedrockClientRuntime::writeResourcePackHaveAllPacks() {
    queueManual(
        8,
        "resource_pack_client_response",
        { 0x08, 0x03, 0x00, 0x00 }
    );
}

void BedrockClientRuntime::writeResourcePackCompleted() {
    queueManual(
        8,
        "resource_pack_client_response",
        { 0x08, 0x04, 0x00, 0x00 }
    );
}

void BedrockClientRuntime::writeClientCacheStatus(bool supported) {
    queueManual(
        129,
        "client_cache_status",
        {
            0x81,
            0x01,
            static_cast<uint8_t>(supported ? 0x01 : 0x00)
        }
    );
}

void BedrockClientRuntime::writeRequestChunkRadius(int32_t radius) {
    radius = std::clamp(radius, 0, 32767);

    std::vector<uint8_t> raw;
    raw.push_back(0x45);
    writeU16LE(raw, static_cast<uint16_t>(radius));

    queueManual(69, "request_chunk_radius", std::move(raw));
}

void BedrockClientRuntime::writeSetLocalPlayerAsInitialized(int64_t runtimeEntityId) {
    std::vector<uint8_t> raw;
    raw.push_back(0x71);
    writeI64Var(raw, runtimeEntityId);

    queueManual(113, "set_local_player_as_initialized", std::move(raw));
}

std::vector<OutgoingGamePacket> BedrockClientRuntime::drainOutgoing() {
    std::vector<OutgoingGamePacket> out;

    auto autoOut = protocol_.drainOutgoing();

    out.reserve(manualOutgoing_.size() + autoOut.size());

    for (auto& packet : manualOutgoing_) {
        out.push_back(std::move(packet));
    }

    manualOutgoing_.clear();

    for (auto& packet : autoOut) {
        out.push_back(std::move(packet));
    }

    return out;
}

std::vector<std::vector<uint8_t>> BedrockClientRuntime::drainOutgoingRaw() {
    auto packets = drainOutgoing();

    std::vector<std::vector<uint8_t>> raw;
    raw.reserve(packets.size());

    for (auto& packet : packets) {
        raw.push_back(std::move(packet.raw));
    }

    return raw;
}

bool BedrockClientRuntime::seenStartGame() const {
    return protocol_.seenStartGame();
}

bool BedrockClientRuntime::sentPostStartGameInit() const {
    return protocol_.sentPostStartGameInit();
}

size_t BedrockClientRuntime::queuedOutgoingCount() const {
    return manualOutgoing_.size() + protocol_.queuedOutgoingCount();
}

void BedrockClientRuntime::clearOutgoing() {
    manualOutgoing_.clear();
    protocol_.clearOutgoing();
}

void BedrockClientRuntime::queueManual(
    uint32_t packetId,
    const std::string& name,
    std::vector<uint8_t> raw
) {
    OutgoingGamePacket packet;
    packet.packetId = packetId;
    packet.name = name;
    packet.raw = std::move(raw);
    manualOutgoing_.push_back(std::move(packet));
}

void BedrockClientRuntime::writeU16LE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void BedrockClientRuntime::writeI64Var(std::vector<uint8_t>& out, int64_t value) {
    uint64_t v = static_cast<uint64_t>(value);

    do {
        uint8_t temp = static_cast<uint8_t>(v & 0x7f);
        v >>= 7;

        if (v != 0) {
            temp |= 0x80;
        }

        out.push_back(temp);
    } while (v != 0);
}

} // namespace bedrock
