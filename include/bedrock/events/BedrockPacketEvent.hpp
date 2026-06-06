#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock {

struct BedrockPacketEventField {
    std::string name;
    std::string type;
    std::string value;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct BedrockPacketEvent {
    std::string version;
    uint32_t packetId = 0;
    std::string packetName;
    std::vector<uint8_t> rawPacket;
    std::vector<uint8_t> payload;
    std::vector<BedrockPacketEventField> fields;
    std::string decodeError;

    bool hasField(const std::string& name) const {
        for (const auto& f : fields) {
            if (f.name == name) return true;
        }
        return false;
    }

    std::string fieldValue(const std::string& name) const {
        for (const auto& f : fields) {
            if (f.name == name) return f.value;
        }
        return "";
    }
};

using BedrockPacketEventHandler = std::function<void(const BedrockPacketEvent&)>;

class BedrockPacketEventBus {
public:
    void onAny(BedrockPacketEventHandler handler) {
        anyHandlers_.push_back(std::move(handler));
    }

    void on(const std::string& packetName, BedrockPacketEventHandler handler) {
        namedHandlers_[packetName].push_back(std::move(handler));
    }

    void emit(const BedrockPacketEvent& event) const {
        for (const auto& h : anyHandlers_) {
            h(event);
        }

        auto it = namedHandlers_.find(event.packetName);
        if (it == namedHandlers_.end()) return;

        for (const auto& h : it->second) {
            h(event);
        }
    }

private:
    std::vector<BedrockPacketEventHandler> anyHandlers_;
    std::unordered_map<std::string, std::vector<BedrockPacketEventHandler>> namedHandlers_;
};

} // namespace bedrock
