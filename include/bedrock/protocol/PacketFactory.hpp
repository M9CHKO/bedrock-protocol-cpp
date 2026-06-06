#pragma once

#include <cstdint>
#include <vector>

namespace bedrock {

enum class ResourcePackResponseStatus : uint8_t {
    Refused = 1,
    SendPacks = 2,
    HaveAllPacks = 3,
    Completed = 4
};

class PacketFactory {
public:
    static std::vector<uint8_t> networkSettingsRequest(uint32_t protocolVersion);

    static std::vector<uint8_t> requestNetworkSettings(uint32_t protocolVersion) {
        return networkSettingsRequest(protocolVersion);
    }

    static std::vector<uint8_t> clientToServerHandshake();

    static std::vector<uint8_t> resourcePackClientResponse(ResourcePackResponseStatus status);

    static std::vector<uint8_t> resourcePackClientResponseHaveAllPacks() {
        return resourcePackClientResponse(ResourcePackResponseStatus::HaveAllPacks);
    }

    static std::vector<uint8_t> resourcePackClientResponseCompleted() {
        return resourcePackClientResponse(ResourcePackResponseStatus::Completed);
    }

    static std::vector<uint8_t> clientCacheStatus(bool enabled);

    static std::vector<uint8_t> requestChunkRadius(int32_t radius);

    static std::vector<uint8_t> setLocalPlayerInitializedMinusOne();

    static std::vector<uint8_t> setLocalPlayerAsInitializedMinusOne() {
        return setLocalPlayerInitializedMinusOne();
    }

    static std::vector<uint8_t> setLocalPlayerInitialized(uint64_t runtimeEntityId);

    static std::vector<uint8_t> setLocalPlayerAsInitialized(uint64_t runtimeEntityId) {
        return setLocalPlayerInitialized(runtimeEntityId);
    }
};

} // namespace bedrock
