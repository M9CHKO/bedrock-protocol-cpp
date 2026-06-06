#include <bedrock/protocol/PacketRegistry.hpp>

namespace bedrock {

std::string PacketRegistry::nameOf(uint32_t packetId) {
    switch (packetId) {
        case 145: return "creative_content";
        case 162: return "item_registry";
        default: return "packet_" + std::to_string(packetId);
    }
}

} // namespace bedrock
