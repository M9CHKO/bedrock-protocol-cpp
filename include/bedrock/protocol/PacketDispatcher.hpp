#pragma once

#include <bedrock/protocol/BatchCodec.hpp>
#include <bedrock/protocol/GamePacket.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock {

struct PacketEvent {
    GamePacket packet;

    uint32_t packetId() const {
        return packet.packetId;
    }

    const std::string& name() const {
        return packet.name;
    }

    const std::vector<uint8_t>& payload() const {
        return packet.payload;
    }

    size_t payloadSize() const {
        return packet.payload.size();
    }

    bool is(const std::string& expectedName) const {
        return packet.name == expectedName;
    }

    bool is(uint32_t expectedId) const {
        return packet.packetId == expectedId;
    }
};

class PacketDispatcher {
public:
    using Handler = std::function<void(const PacketEvent&)>;

    void on(const std::string& packetName, Handler handler);
    void onId(uint32_t packetId, Handler handler);
    void onAny(Handler handler);

    void dispatch(const GamePacket& packet);
    void dispatch(const DecodedBatch& batch);
    void dispatch(const std::vector<GamePacket>& packets);

    void clear();

private:
    std::unordered_map<std::string, std::vector<Handler>> byName_;
    std::unordered_map<uint32_t, std::vector<Handler>> byId_;
    std::vector<Handler> any_;
};

} // namespace bedrock
