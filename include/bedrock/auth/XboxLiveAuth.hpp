#pragma once

#include <bedrock/auth/BedrockAuthJwt.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bedrock {

struct XboxDeviceCodeInfo {
    std::string verificationUri;
    std::string userCode;
    std::string message;
};

struct XboxLiveAuthOptions {
    std::string profileName = "Bot";
    std::string version = "latest";
    uint32_t protocolVersion = 0;
    std::string serverAddress;
    bool offline = false;
    bool interactiveAuth = true;
    std::string xboxClientId;
    std::filesystem::path cacheRoot;
    std::vector<std::filesystem::path> minecraftDataRoots;
    std::string clientDataJson;
    std::function<void(const XboxDeviceCodeInfo&)> onDeviceCode;
    std::function<void(const std::string&)> onLog;
};

struct XboxLiveLoginPacket {
    std::vector<uint8_t> loginPacket;
    BedrockClientKeyPair keyPair;
    std::string identity;
    std::string displayName;
    std::string xuid;
    bool online = false;
};

class XboxLiveAuth {
public:
    static XboxLiveLoginPacket makeLoginPacket(XboxLiveAuthOptions options);

    static BedrockClientKeyPair loadOrCreateProfileKeyPair(
        const std::string& profileName,
        const std::filesystem::path& cacheRoot = {}
    );
};

} // namespace bedrock
