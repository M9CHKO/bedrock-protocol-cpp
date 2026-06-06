#pragma once

#include <bedrock/protocol/PacketFactory.hpp>
#include <bedrock/protocol/TypedPacketDispatcher.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bedrock {

struct BedrockClientRuntimeOptions {
    int32_t chunkRadius = 20;
    bool clientCacheEnabled = false;
    bool autoResourcePackResponse = true;
    bool autoStartGameInit = true;
};

class BedrockClientRuntime {
public:
    using Handler = std::function<void(const PacketEvent&)>;

    BedrockClientRuntime() = default;

    explicit BedrockClientRuntime(BedrockClientRuntimeOptions options)
        : options_(options) {}

    void onAny(Handler handler) {
        anyHandlers_.push_back(std::move(handler));
    }

    void on(const std::string& packetName, Handler handler) {
        nameHandlers_[packetName].push_back(std::move(handler));
    }

    void on(uint32_t packetId, Handler handler) {
        idHandlers_[packetId].push_back(std::move(handler));
    }

    void writeNetworkSettingsRequest(uint32_t protocolVersion) {
        pushOutgoing(
            193,
            "request_network_settings",
            PacketFactory::networkSettingsRequest(protocolVersion)
        );
    }

    void writeClientToServerHandshake() {
        pushOutgoing(
            4,
            "client_to_server_handshake",
            PacketFactory::clientToServerHandshake()
        );
    }

    void writeResourcePackHaveAllPacks() {
        pushOutgoing(
            8,
            "resource_pack_client_response",
            PacketFactory::resourcePackClientResponse(ResourcePackResponseStatus::HaveAllPacks)
        );
    }

    void writeResourcePackCompleted() {
        pushOutgoing(
            8,
            "resource_pack_client_response",
            PacketFactory::resourcePackClientResponse(ResourcePackResponseStatus::Completed)
        );
    }

    void writeClientCacheStatus(bool enabled = false) {
        pushOutgoing(
            129,
            "client_cache_status",
            PacketFactory::clientCacheStatus(enabled)
        );
    }

    void writeRequestChunkRadius(int32_t radius) {
        pushOutgoing(
            69,
            "request_chunk_radius",
            PacketFactory::requestChunkRadius(static_cast<int16_t>(radius))
        );
    }

    void writeSetLocalPlayerAsInitializedMinusOne() {
        pushOutgoing(
            113,
            "set_local_player_as_initialized",
            PacketFactory::setLocalPlayerInitializedMinusOne()
        );
    }

    void writeSetLocalPlayerInitializedMinusOne() {
        writeSetLocalPlayerAsInitializedMinusOne();
    }

    void writeSetLocalPlayerAsInitialized() {
        writeSetLocalPlayerAsInitializedMinusOne();
    }

    void writeSetLocalPlayerInitialized() {
        writeSetLocalPlayerAsInitializedMinusOne();
    }

    bool handle(const GamePacket& packet) {
        if (packet.packetId == 11 || packet.name == "start_game") {
            seenStartGame_ = true;
        }

        PacketEvent event{packet};

        bool handled = false;

        for (auto& handler : anyHandlers_) {
            handler(event);
            handled = true;
        }

        auto idIt = idHandlers_.find(packet.packetId);
        if (idIt != idHandlers_.end()) {
            for (auto& handler : idIt->second) {
                handler(event);
                handled = true;
            }
        }

        if (!packet.name.empty()) {
            auto nameIt = nameHandlers_.find(packet.name);
            if (nameIt != nameHandlers_.end()) {
                for (auto& handler : nameIt->second) {
                    handler(event);
                    handled = true;
                }
            }
        }

        autoRespond(packet);
        return handled;
    }

    bool handlePacket(const GamePacket& packet) {
        return handle(packet);
    }

    bool receive(const GamePacket& packet) {
        return handle(packet);
    }

    bool receivePacket(const GamePacket& packet) {
        return handle(packet);
    }

    void handlePackets(const std::vector<GamePacket>& packets) {
        for (const auto& packet : packets) {
            handle(packet);
        }
    }

    void receivePackets(const std::vector<GamePacket>& packets) {
        handlePackets(packets);
    }

    std::vector<GamePacket> takeOutgoingPackets() {
        auto out = outgoing_;
        outgoing_.clear();
        return out;
    }

    std::vector<GamePacket> drainOutgoingPackets() {
        return takeOutgoingPackets();
    }

    std::vector<GamePacket> takeOutgoing() {
        return takeOutgoingPackets();
    }

    std::vector<GamePacket> drainOutgoing() {
        return takeOutgoingPackets();
    }

    const std::vector<GamePacket>& outgoingPackets() const {
        return outgoing_;
    }

    void clearOutgoingPackets() {
        outgoing_.clear();
    }

    bool seenStartGame() const {
        return seenStartGame_;
    }

    bool sentPostStartGameInit() const {
        return sentStartGameInit_;
    }

    bool sentStartGameInit() const {
        return sentStartGameInit_;
    }

    const BedrockClientRuntimeOptions& options() const {
        return options_;
    }

    void setChunkRadius(int32_t radius) {
        options_.chunkRadius = radius;
    }

    int32_t chunkRadius() const {
        return options_.chunkRadius;
    }

private:
    BedrockClientRuntimeOptions options_{};
    std::vector<GamePacket> outgoing_{};

    std::vector<Handler> anyHandlers_{};
    std::unordered_map<uint32_t, std::vector<Handler>> idHandlers_{};
    std::unordered_map<std::string, std::vector<Handler>> nameHandlers_{};

    bool seenStartGame_ = false;
    bool sentStartGameInit_ = false;

    static uint64_t readVarUint(const std::vector<uint8_t>& data, size_t& offset) {
        uint64_t value = 0;
        int shift = 0;

        while (offset < data.size()) {
            uint8_t byte = data[offset++];
            value |= static_cast<uint64_t>(byte & 0x7f) << shift;

            if ((byte & 0x80) == 0) {
                return value;
            }

            shift += 7;
            if (shift > 63) {
                break;
            }
        }

        return value;
    }

    static std::vector<uint8_t> payloadFromRawPacket(const std::vector<uint8_t>& raw) {
        size_t offset = 0;
        readVarUint(raw, offset);

        if (offset > raw.size()) {
            return {};
        }

        return std::vector<uint8_t>(
            raw.begin() + static_cast<std::ptrdiff_t>(offset),
            raw.end()
        );
    }

    static GamePacket makePacket(
        uint32_t packetId,
        const std::string& name,
        const std::vector<uint8_t>& raw
    ) {
        GamePacket packet{};
        packet.packetId = packetId;
        packet.name = name;
        packet.payload = payloadFromRawPacket(raw);
        return packet;
    }

    void pushOutgoing(
        uint32_t packetId,
        const std::string& name,
        const std::vector<uint8_t>& raw
    ) {
        outgoing_.push_back(makePacket(packetId, name, raw));
    }

    void autoRespond(const GamePacket& packet) {
        if (options_.autoResourcePackResponse && packet.packetId == 6) {
            writeResourcePackHaveAllPacks();
            return;
        }

        if (options_.autoResourcePackResponse && packet.packetId == 7) {
            writeResourcePackCompleted();
            return;
        }

        if (options_.autoStartGameInit && packet.packetId == 11 && !sentStartGameInit_) {
            sentStartGameInit_ = true;

            writeClientCacheStatus(options_.clientCacheEnabled);
            writeRequestChunkRadius(options_.chunkRadius);
            writeSetLocalPlayerAsInitializedMinusOne();
            return;
        }
    }
};

} // namespace bedrock
