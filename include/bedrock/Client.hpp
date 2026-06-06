#pragma once

#include <bedrock/Packet.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bedrock {

class Client {
public:
    using Handler = std::function<void(Packet&)>;

    void on(const std::string& name, Handler handler) {
        handlers[name].push_back(std::move(handler));
    }

    void emit(Packet packet) {
        emitTo("packet", packet);
        emitTo(packet.name, packet);
    }

private:
    std::map<std::string, std::vector<Handler>> handlers;

    void emitTo(const std::string& name, Packet& packet) {
        auto it = handlers.find(name);
        if (it == handlers.end()) {
            return;
        }

        for (auto& handler : it->second) {
            handler(packet);
        }
    }
};

} // namespace bedrock
