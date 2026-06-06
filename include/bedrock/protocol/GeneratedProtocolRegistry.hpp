#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bedrock {

struct ProtocolPacketInfo {
    uint32_t id;
    const char* name;
    const char* paramsType;
};

struct ProtocolVersionInfo {
    const char* minecraftVersion;
    uint32_t protocolVersion;
    const ProtocolPacketInfo* packets;
    std::size_t packetCount;
};

const ProtocolVersionInfo* getGeneratedProtocolVersion(const std::string& minecraftVersion);
std::vector<std::string> getGeneratedProtocolVersions();

class GeneratedProtocolRegistry {
public:
    static inline const ProtocolVersionInfo* get(const std::string& minecraftVersion) {
        return getGeneratedProtocolVersion(minecraftVersion);
    }

    static inline const ProtocolPacketInfo* packetById(const ProtocolVersionInfo& version, uint32_t packetId) {
        for (std::size_t i = 0; i < version.packetCount; ++i) {
            if (version.packets[i].id == packetId) return &version.packets[i];
        }
        return nullptr;
    }

    static inline const ProtocolPacketInfo* packetByName(const ProtocolVersionInfo& version, const std::string& packetName) {
        for (std::size_t i = 0; i < version.packetCount; ++i) {
            if (packetName == version.packets[i].name) return &version.packets[i];
        }
        return nullptr;
    }

    static inline std::vector<std::string> versions() {
        return getGeneratedProtocolVersions();
    }
};

} // namespace bedrock
