#pragma once

#include <bedrock/protocol/GamePacket.hpp>
#include <bedrock/protodef/ProtoDefPacketDecoder.hpp>

#include <iostream>
#include <string>

namespace bedrock {

class PacketInspector {
public:
    explicit PacketInspector(std::string minecraftVersion)
        : minecraftVersion_(std::move(minecraftVersion)) {}

    void inspect(const GamePacket& packet) const {
        ProtoDefPacketDecoder decoder(minecraftVersion_);
        auto fields = decoder.decodePacket(packet.name, packet.payload);

        std::cout << "[INSPECT] packet "
                  << packet.name
                  << " id="
                  << packet.packetId
                  << " fields="
                  << fields.size()
                  << "\n";

        for (const auto& field : fields) {
            std::cout << "  "
                      << field.path
                      << "="
                      << field.value
                      << " type="
                      << field.type
                      << " size="
                      << field.size
                      << "\n";
        }
    }

private:
    std::string minecraftVersion_;
};

} // namespace bedrock
