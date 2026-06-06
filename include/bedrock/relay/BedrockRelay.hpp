#pragma once

#include <bedrock/client/BedrockClient.hpp>

#include <utility>
#include <vector>

namespace bedrock {

struct BedrockRelayOptions {
    BedrockClientOptions clientOptions;
    bool forwardClientToServer = true;
    bool forwardServerToClient = true;
};

struct BedrockRelayFrame {
    std::vector<VersionedGamePacket> packets;
    std::vector<uint8_t> forwardedMcpe;
};

class BedrockRelay {
public:
    using PacketHandler = BedrockClient::PacketHandler;

    explicit BedrockRelay(BedrockRelayOptions options = {})
        : options_(std::move(options)),
          upstream_(options_.clientOptions),
          downstream_(options_.clientOptions) {}

    BedrockClient& upstream() { return upstream_; }
    BedrockClient& downstream() { return downstream_; }

    void onServerPacket(PacketHandler handler) {
        upstream_.onAny(std::move(handler));
    }

    void onClientPacket(PacketHandler handler) {
        downstream_.onAny(std::move(handler));
    }

    BedrockRelayFrame handleServerToClientMcpe(const std::vector<uint8_t>& mcpePayload) {
        BedrockRelayFrame frame;
        auto batch = upstream_.handleMcpePayload(mcpePayload);
        frame.packets = batch.packets;

        if (options_.forwardServerToClient) {
            frame.forwardedMcpe = mcpePayload;
        }

        return frame;
    }

    BedrockRelayFrame handleClientToServerMcpe(const std::vector<uint8_t>& mcpePayload) {
        BedrockRelayFrame frame;
        auto batch = downstream_.handleMcpePayload(mcpePayload);
        frame.packets = batch.packets;

        if (options_.forwardClientToServer) {
            frame.forwardedMcpe = mcpePayload;
        }

        return frame;
    }

    std::vector<VersionedGamePacket> takeUpstreamAutoResponses() {
        return upstream_.takeOutgoingPackets();
    }

    std::vector<VersionedGamePacket> takeDownstreamAutoResponses() {
        return downstream_.takeOutgoingPackets();
    }

private:
    BedrockRelayOptions options_;
    BedrockClient upstream_;
    BedrockClient downstream_;
};

inline BedrockRelay createRelay(BedrockRelayOptions options = {}) {
    return BedrockRelay(std::move(options));
}

} // namespace bedrock
