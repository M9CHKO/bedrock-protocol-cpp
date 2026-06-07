#pragma once

// Easy public API. Include this in bots:
//   #include <bedrock/bedrock.hpp>

#include <bedrock/api/Client.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/relay/BedrockRelay.hpp>
#include <bedrock/server/BedrockServer.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bedrock {

using Options = api::ClientOptions;
using Packet = api::Packet;
using TextPacket = api::TextPacket;
using Client = api::Client;
using DebugMode = api::DebugMode;
using PacketValue = ProtoDefValue;
using PacketObject = std::unordered_map<std::string, PacketValue>;
using PacketArray = std::vector<PacketValue>;

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

inline PacketValue nil() {
    return PacketValue::null();
}

inline PacketValue boolean(bool value) {
    return PacketValue::boolean(value);
}

inline PacketValue i64(int64_t value) {
    return PacketValue::integer(value);
}

inline PacketValue i32(int32_t value) {
    return PacketValue::integer(value);
}

inline PacketValue u64(uint64_t value) {
    return PacketValue::uinteger(value);
}

inline PacketValue u32(uint32_t value) {
    return PacketValue::uinteger(value);
}

inline PacketValue f64(double value) {
    return PacketValue::floating(value);
}

inline PacketValue f32(float value) {
    return PacketValue::floating(value);
}

inline PacketValue str(std::string value) {
    return PacketValue::string(std::move(value));
}

inline PacketValue bytes(std::vector<uint8_t> value) {
    return PacketValue::bytes(std::move(value));
}

inline PacketValue object(PacketObject fields) {
    return PacketValue::object(std::move(fields));
}

inline PacketValue object(std::initializer_list<std::pair<const std::string, PacketValue>> fields) {
    return PacketValue::object(PacketObject(fields));
}

inline PacketValue array(PacketArray values) {
    return PacketValue::array(std::move(values));
}

inline PacketValue array(std::initializer_list<PacketValue> values) {
    return PacketValue::array(PacketArray(values));
}

} // namespace bedrock
