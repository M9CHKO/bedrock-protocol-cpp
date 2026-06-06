#pragma once

#include <string>

namespace bedrock {

struct BedrockClientDataOptions {
    std::string displayName;
    std::string xuid;
    std::string gameVersion;
    std::string deviceId;
    std::string serverAddress;
};

class BedrockClientDataBuilder {
public:
    static std::string buildClassicSkinClientData(const BedrockClientDataOptions& options);
};

} // namespace bedrock
