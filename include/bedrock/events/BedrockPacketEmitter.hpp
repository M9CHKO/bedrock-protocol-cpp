#pragma once

#include <bedrock/events/BedrockPacket.hpp>

#include <map>
#include <string>
#include <vector>

namespace bedrock {

class BedrockPacketEmitter {
public:
    void on(const std::string& name, BedrockPacketHandler handler) {
        handlers_[name].push_back(std::move(handler));
    }

    void emit(const BedrockPacket& packet) const {
        emitTo("packet", packet);
        emitTo(packet.data.name, packet);
    }

private:
    std::map<std::string, std::vector<BedrockPacketHandler>> handlers_;

    void emitTo(const std::string& name, const BedrockPacket& packet) const {
        auto it = handlers_.find(name);
        if (it == handlers_.end()) return;

        for (const auto& handler : it->second) {
            handler(packet);
        }
    }
};

} // namespace bedrock
