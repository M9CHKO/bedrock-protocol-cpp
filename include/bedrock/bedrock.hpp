#pragma once

// Easy public API. Include this in bots:
//   #include <bedrock/bedrock.hpp>

#include <bedrock/api/Client.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>

#include <string>
#include <utility>
#include <vector>

namespace bedrock {

using Options = api::ClientOptions;
using Packet = api::Packet;
using TextPacket = api::TextPacket;
using Client = api::Client;
using DebugMode = api::DebugMode;

inline Client createClient(Options options = {}) {
    if (options.profile.empty()) {
        options.profile = options.username.empty() ? std::string("Bot") : options.username;
    }
    if (options.version.empty() || options.version == "auto" || options.version == "latest") {
        auto vs = ProtocolDefinition::versions();
        if (!vs.empty()) options.version = vs.back();
    }
    return api::createClient(std::move(options));
}

inline std::vector<std::string> versions() {
    return ProtocolDefinition::versions();
}

inline bool supportsVersion(const std::string& version) {
    return ProtocolDefinition::supportsVersion(version);
}

} // namespace bedrock
