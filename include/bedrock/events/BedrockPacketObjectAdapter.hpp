#pragma once

#include <bedrock/events/BedrockPacket.hpp>
#include <bedrock/events/BedrockPacketEvent.hpp>

namespace bedrock {

class BedrockPacketObjectAdapter {
public:
    static BedrockPacket fromEvent(const BedrockPacketEvent& event) {
        BedrockPacket packet;

        packet.data.name = event.packetName;
        packet.packetId = event.packetId;
        packet.metadata.size = event.rawPacket.size();
        packet.buffer = event.payload;
        packet.fullBuffer = event.rawPacket;
        packet.decodeError = event.decodeError;

        for (const auto& field : event.fields) {
            packet.data.params[field.name] = field.value;
        }

        return packet;
    }
};

} // namespace bedrock
