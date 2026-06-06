#include <bedrock/protocol/PacketDispatcher.hpp>

namespace bedrock {

void PacketDispatcher::on(const std::string& packetName, Handler handler) {
    byName_[packetName].push_back(std::move(handler));
}

void PacketDispatcher::onId(uint32_t packetId, Handler handler) {
    byId_[packetId].push_back(std::move(handler));
}

void PacketDispatcher::onAny(Handler handler) {
    any_.push_back(std::move(handler));
}

void PacketDispatcher::dispatch(const GamePacket& packet) {
    PacketEvent event{packet};

    for (const auto& handler : any_) {
        handler(event);
    }

    auto nameIt = byName_.find(packet.name);
    if (nameIt != byName_.end()) {
        for (const auto& handler : nameIt->second) {
            handler(event);
        }
    }

    auto idIt = byId_.find(packet.packetId);
    if (idIt != byId_.end()) {
        for (const auto& handler : idIt->second) {
            handler(event);
        }
    }
}

void PacketDispatcher::dispatch(const DecodedBatch& batch) {
    dispatch(batch.packets);
}

void PacketDispatcher::dispatch(const std::vector<GamePacket>& packets) {
    for (const auto& packet : packets) {
        dispatch(packet);
    }
}

void PacketDispatcher::clear() {
    byName_.clear();
    byId_.clear();
    any_.clear();
}

} // namespace bedrock
