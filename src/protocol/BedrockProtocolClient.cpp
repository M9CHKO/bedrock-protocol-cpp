#include <bedrock/protocol/BedrockProtocolClient.hpp>

#include <utility>

namespace bedrock {

BedrockProtocolClient::BedrockProtocolClient(BedrockProtocolClientOptions options)
    : options_(options) {
}

void BedrockProtocolClient::on(const std::string& packetName, Handler handler) {
    dispatcher_.on(packetName, std::move(handler));
}

void BedrockProtocolClient::onId(uint32_t packetId, Handler handler) {
    dispatcher_.onId(packetId, std::move(handler));
}

void BedrockProtocolClient::onAny(Handler handler) {
    dispatcher_.onAny(std::move(handler));
}

void BedrockProtocolClient::handle(const GamePacket& packet) {
    dispatcher_.dispatch(packet);
    autoRespond(packet);
}

void BedrockProtocolClient::handle(const DecodedBatch& batch) {
    handle(batch.packets);
}

void BedrockProtocolClient::handle(const std::vector<GamePacket>& packets) {
    for (const auto& packet : packets) {
        handle(packet);
    }
}

std::vector<OutgoingGamePacket> BedrockProtocolClient::drainOutgoing() {
    auto out = std::move(outgoing_);
    outgoing_.clear();
    return out;
}

bool BedrockProtocolClient::seenStartGame() const {
    return seenStartGame_;
}

bool BedrockProtocolClient::sentPostStartGameInit() const {
    return sentPostStartGameInit_;
}

size_t BedrockProtocolClient::queuedOutgoingCount() const {
    return outgoing_.size();
}

void BedrockProtocolClient::clearHandlers() {
    dispatcher_.clear();
}

void BedrockProtocolClient::clearOutgoing() {
    outgoing_.clear();
}

void BedrockProtocolClient::autoRespond(const GamePacket& packet) {
    if (options_.autoResourcePackResponse) {
        if (packet.packetId == 6 || packet.name == "resource_packs_info") {
            queueResourcePackHaveAllPacks();
            return;
        }

        if (packet.packetId == 7 || packet.name == "resource_pack_stack") {
            queueResourcePackCompleted();
            return;
        }
    }

    if (packet.packetId == 11 || packet.name == "start_game") {
        seenStartGame_ = true;

        if (options_.autoPostStartGameInit && !sentPostStartGameInit_) {
            queuePostStartGameInit();
            sentPostStartGameInit_ = true;
        }
    }
}

void BedrockProtocolClient::queueRaw(
    uint32_t packetId,
    const std::string& name,
    std::vector<uint8_t> raw
) {
    OutgoingGamePacket packet;
    packet.packetId = packetId;
    packet.name = name;
    packet.raw = std::move(raw);
    outgoing_.push_back(std::move(packet));
}

void BedrockProtocolClient::queueResourcePackHaveAllPacks() {
    queueRaw(
        8,
        "resource_pack_client_response",
        {
            0x08,
            0x03,
            0x00,
            0x00
        }
    );
}

void BedrockProtocolClient::queueResourcePackCompleted() {
    queueRaw(
        8,
        "resource_pack_client_response",
        {
            0x08,
            0x04,
            0x00,
            0x00
        }
    );
}

void BedrockProtocolClient::queuePostStartGameInit() {
    queueRaw(
        129,
        "client_cache_status",
        makeClientCacheStatus(options_.clientCacheSupported)
    );

    queueRaw(
        69,
        "request_chunk_radius",
        makeRequestChunkRadius(options_.chunkRadius)
    );

    queueRaw(
        113,
        "set_local_player_as_initialized",
        makeSetLocalPlayerAsInitializedMinusOne()
    );
}

std::vector<uint8_t> BedrockProtocolClient::makeClientCacheStatus(bool supported) {
    return {
        0x81,
        0x01,
        static_cast<uint8_t>(supported ? 0x01 : 0x00)
    };
}

std::vector<uint8_t> BedrockProtocolClient::makeRequestChunkRadius(int32_t radius) {
    if (radius < 0) {
        radius = 0;
    }

    if (radius > 32767) {
        radius = 32767;
    }

    return {
        0x45,
        static_cast<uint8_t>(radius & 0xff),
        static_cast<uint8_t>((radius >> 8) & 0xff)
    };
}

std::vector<uint8_t> BedrockProtocolClient::makeSetLocalPlayerAsInitializedMinusOne() {
    return {
        0x71,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0x7f
    };
}

} // namespace bedrock
