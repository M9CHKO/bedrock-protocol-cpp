#pragma once

#include <bedrock/events/PacketEventApi.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock {

class PacketEventBus {
public:
    void on(const std::string& packetName, PacketCallback cb) {
        handlers_[packetName].push_back(std::move(cb));
    }

    void emit(const PacketEventApi& event) const {
        auto all = handlers_.find("*");
        if (all != handlers_.end()) {
            for (const auto& cb : all->second) cb(event);
        }

        auto exact = handlers_.find(event.name);
        if (exact != handlers_.end()) {
            for (const auto& cb : exact->second) cb(event);
        }
    }

private:
    std::unordered_map<std::string, std::vector<PacketCallback>> handlers_;
};

}
