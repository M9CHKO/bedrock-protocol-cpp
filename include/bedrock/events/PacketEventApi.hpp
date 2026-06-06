#pragma once

#include <bedrock/events/BedrockPacketEvent.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bedrock {

struct PacketEventApi {
    uint32_t id = 0;
    std::string name;
    std::map<std::string, std::string> params;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> rawPacket;
    std::string decodeError;

    bool ok() const {
        return decodeError.empty();
    }

    std::string get(const std::string& key) const {
        auto it = params.find(key);
        if (it == params.end()) return "";
        return it->second;
    }

    bool has(const std::string& key) const {
        return params.find(key) != params.end();
    }
};

inline PacketEventApi toPacketEventApi(const BedrockPacketEvent& e) {
    PacketEventApi out;
    out.id = e.packetId;
    out.name = e.packetName;
    out.payload = e.payload;
    out.rawPacket = e.rawPacket;
    out.decodeError = e.decodeError;

    for (const auto& f : e.fields) {
        out.params[f.name] = f.value;

        auto dot = f.name.find_last_of('.');
        if (dot != std::string::npos) {
            std::string shortName = f.name.substr(dot + 1);
            if (!shortName.empty() && !out.params.count(shortName)) {
                out.params[shortName] = f.value;
            }
        }
    }

    return out;
}

using PacketCallback = std::function<void(const PacketEventApi&)>;

}
