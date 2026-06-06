#pragma once

#include <bedrock/protocol/GeneratedProtocolRegistry.hpp>

namespace bedrock {

const ProtocolVersionInfo* generatedProtocolVersionByName(const std::string& minecraftVersion);
std::vector<std::string> generatedProtocolVersionNames();

} // namespace bedrock
