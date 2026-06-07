#include <bedrock/BedrockFramer.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/protocol/VersionedPacketCodec.hpp>
#include <bedrock/protodef/ProtoDefEncoder.hpp>
#include <bedrock/protodef/ProtoDefPacketEncoder.hpp>
#include <bedrock/protodef/ProtoDefPacketDecoder.hpp>
#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/protodef/ProtoDefWriter.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::vector<int> parseVersion(const std::string& version) {
    std::vector<int> out;
    std::string cur;

    for (char c : version) {
        if (c == '.') {
            out.push_back(cur.empty() ? 0 : std::stoi(cur));
            cur.clear();
        } else if (c >= '0' && c <= '9') {
            cur.push_back(c);
        }
    }

    out.push_back(cur.empty() ? 0 : std::stoi(cur));
    while (out.size() < 3) out.push_back(0);
    return out;
}

bool versionAtLeast(const std::string& version, const std::string& minimum) {
    return parseVersion(version) >= parseVersion(minimum);
}

bool versionEquals(const std::string& version, const std::string& other) {
    return parseVersion(version) == parseVersion(other);
}

bool sameBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

bool checkPacketRoundtrip(const std::string& version, const std::string& packetName) {
    auto codec = bedrock::VersionedPacketCodec::forVersion(version);

    uint32_t id = 0;
    try {
        id = codec.definition().packetId(packetName);
    } catch (const std::exception&) {
        return true;
    }

    const std::vector<uint8_t> payload = {
        static_cast<uint8_t>(id & 0xffu),
        static_cast<uint8_t>((id >> 8u) & 0xffu),
        0x42,
        0x7f
    };

    auto encoded = codec.encodeFullPacketByName(packetName, payload);
    auto decoded = codec.decodeFullPacket(encoded);

    if (decoded.packetId != id || decoded.name != packetName || !sameBytes(decoded.payload, payload)) {
        std::cerr << "[FAIL] " << version << " packet " << packetName << " id/name/payload mismatch\n";
        return false;
    }

    auto encodedById = codec.encodeFullPacketById(decoded.packetId, decoded.payload);
    if (!sameBytes(encoded, encodedById)) {
        std::cerr << "[FAIL] " << version << " packet " << packetName << " encode-by-id mismatch\n";
        return false;
    }

    return true;
}

bool checkBatchRoundtrip(const std::string& version, bool compressionReady) {
    auto codec = bedrock::VersionedPacketCodec::forVersion(version);

    std::vector<std::vector<uint8_t>> packets;
    packets.push_back(codec.encodeFullPacketByName("play_status", { 0x00 }));
    packets.push_back(codec.encodeFullPacketByName("resource_pack_client_response", { 0x03, 0x00 }));
    packets.push_back(codec.encodeFullPacketByName("request_chunk_radius", { 0x10 }));

    bedrock::BedrockFramerSettings settings;
    settings.compressionReady = compressionReady;
    settings.compressorInHeader = versionAtLeast(version, "1.20.61");
    settings.compressionThreshold = 0;
    settings.compressionAlgorithm = 0;

    auto encoded = bedrock::BedrockFramer::encodeBatch(packets, settings);
    auto decoded = bedrock::BedrockFramer::decodeBatch(encoded, settings);

    if (decoded.size() != packets.size()) {
        std::cerr << "[FAIL] " << version << " batch size mismatch\n";
        return false;
    }

    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (!sameBytes(decoded[i], packets[i])) {
            std::cerr << "[FAIL] " << version << " batch packet " << i << " mismatch\n";
            return false;
        }
    }

    return true;
}

bedrock::ProtoDefValue object(std::unordered_map<std::string, bedrock::ProtoDefValue> fields) {
    return bedrock::ProtoDefValue::object(std::move(fields));
}

bedrock::ProtoDefValue array(std::vector<bedrock::ProtoDefValue> values = {}) {
    return bedrock::ProtoDefValue::array(std::move(values));
}

bedrock::ProtoDefValue vec2f(double x, double z) {
    return object({
        {"x", bedrock::ProtoDefValue::floating(x)},
        {"z", bedrock::ProtoDefValue::floating(z)}
    });
}

bedrock::ProtoDefValue vec3f(double x, double y, double z) {
    return object({
        {"x", bedrock::ProtoDefValue::floating(x)},
        {"y", bedrock::ProtoDefValue::floating(y)},
        {"z", bedrock::ProtoDefValue::floating(z)}
    });
}

bedrock::ProtoDefValue inputFlags(std::initializer_list<std::string> enabled = {}) {
    auto flags = object({
        {"item_interact", bedrock::ProtoDefValue::boolean(false)},
        {"block_action", bedrock::ProtoDefValue::boolean(false)},
        {"item_stack_request", bedrock::ProtoDefValue::boolean(false)},
        {"client_predicted_vehicle", bedrock::ProtoDefValue::boolean(false)}
    });

    for (const auto& name : enabled) {
        flags.objectValue[name] = bedrock::ProtoDefValue::boolean(true);
    }

    return flags;
}

bool checkProtoDefNativeHelpers() {
    bool ok = true;
    bedrock::ProtoDefEncoder encoder;

    auto expectBytes = [&](const std::string& label, const std::string& typeJson, const bedrock::ProtoDefValue& value, std::vector<uint8_t> expected) {
        try {
            bedrock::ProtoDefWriter writer;
            encoder.encode(typeJson, value, writer);
            auto actual = writer.take();
            if (!sameBytes(actual, expected)) {
                std::cerr << "[FAIL] protodef helper " << label << " byte mismatch\n";
                ok = false;
            }
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] protodef helper " << label << ": " << e.what() << "\n";
            ok = false;
        }
    };

    expectBytes(
        "fixed-buffer",
        "[\"buffer\",{\"count\":4}]",
        bedrock::ProtoDefValue::bytes({1, 2, 3, 4}),
        {1, 2, 3, 4}
    );

    expectBytes(
        "ipAddress",
        "\"ipAddress\"",
        bedrock::ProtoDefValue::string("127.0.0.1"),
        {127, 0, 0, 1}
    );

    expectBytes(
        "endOfArray",
        "[\"endOfArray\",{\"type\":\"u8\"}]",
        array({bedrock::ProtoDefValue::uinteger(7), bedrock::ProtoDefValue::uinteger(8)}),
        {7, 8}
    );

    expectBytes(
        "entityMetadataLoop",
        "[\"entityMetadataLoop\",{\"type\":\"u8\",\"endVal\":127}]",
        array({bedrock::ProtoDefValue::uinteger(1), bedrock::ProtoDefValue::uinteger(2)}),
        {1, 2, 127}
    );

    expectBytes(
        "varint128-bitflags",
        "[\"bitflags\",{\"type\":\"varint128\",\"flags\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\",\"j\",\"k\",\"l\",\"m\",\"n\",\"o\",\"p\",\"q\",\"r\",\"s\",\"t\",\"u\",\"v\",\"w\",\"x\",\"y\",\"z\",\"aa\",\"ab\",\"ac\",\"ad\",\"ae\",\"af\",\"ag\",\"ah\",\"ai\",\"aj\",\"ak\",\"al\",\"am\",\"an\",\"ao\",\"ap\",\"aq\",\"ar\",\"as\",\"at\",\"au\",\"av\",\"aw\",\"ax\",\"ay\",\"az\",\"ba\",\"bb\",\"bc\",\"bd\",\"be\",\"bf\",\"bg\",\"bh\",\"bi\",\"bj\",\"bk\",\"bl\",\"bm\"]}]",
        array({bedrock::ProtoDefValue::string("bl")}),
        {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01}
    );

    return ok;
}

bool checkSchemaEncode(
    const std::string& version,
    const std::string& packetName,
    const bedrock::ProtoDefValue& value
) {
    auto codec = bedrock::VersionedPacketCodec::forVersion(version);
    if (!codec.definition().hasPacket(packetName)) {
        return true;
    }

    try {
        bedrock::ProtoDefPacketEncoder encoder(version);
        auto payload = encoder.encodePacket(packetName, value);
        auto full = codec.encodeFullPacketByName(packetName, payload);
        auto decoded = codec.decodeFullPacket(full);

        if (decoded.name != packetName || decoded.payload != payload) {
            std::cerr << "[FAIL] " << version << " schema encode " << packetName << " mismatch\n";
            return false;
        }

        bedrock::ProtoDefPacketDecoder decoder(version);
        (void) decoder.decodePacket(packetName, payload);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << version << " schema encode " << packetName << ": " << e.what() << "\n";
        return false;
    }

    return true;
}

bool checkSchemaEncodes(const std::string& version) {
    bool ok = true;

    ok = checkSchemaEncode(version, "request_chunk_radius", object({
        {"chunk_radius", bedrock::ProtoDefValue::integer(20)},
        {"max_radius", bedrock::ProtoDefValue::uinteger(0)}
    })) && ok;

    ok = checkSchemaEncode(version, "client_cache_status", object({
        {"enabled", bedrock::ProtoDefValue::boolean(false)}
    })) && ok;

    ok = checkSchemaEncode(version, "set_local_player_as_initialized", object({
        {"runtime_entity_id", bedrock::ProtoDefValue::uinteger(UINT64_MAX)}
    })) && ok;

    ok = checkSchemaEncode(version, "resource_pack_client_response", object({
        {"response_status", bedrock::ProtoDefValue::string("completed")},
        {"resourcepackids", array()}
    })) && ok;

    auto textPacket = object({
        {"type", bedrock::ProtoDefValue::string("raw")},
        {"needs_translation", bedrock::ProtoDefValue::boolean(false)},
        {"message", bedrock::ProtoDefValue::string("hello from schema encoder")},
        {"xuid", bedrock::ProtoDefValue::string("")},
        {"platform_chat_id", bedrock::ProtoDefValue::string("")},
        {"filtered_message", bedrock::ProtoDefValue::string("")}
    });
    if (versionAtLeast(version, "1.21.130")) {
        textPacket.objectValue["category"] = bedrock::ProtoDefValue::string("message_only");
        textPacket.objectValue["raw"] = bedrock::ProtoDefValue::string("hello from schema encoder");
        textPacket.objectValue["tip"] = bedrock::ProtoDefValue::string("");
        textPacket.objectValue["system_message"] = bedrock::ProtoDefValue::string("");
        textPacket.objectValue["text_object_whisper"] = bedrock::ProtoDefValue::string("");
        textPacket.objectValue["text_object_announcement"] = bedrock::ProtoDefValue::string("");
        textPacket.objectValue["text_object"] = bedrock::ProtoDefValue::string("");
        textPacket.objectValue["has_filtered_message"] = bedrock::ProtoDefValue::boolean(false);
    }
    ok = checkSchemaEncode(version, "text", textPacket) && ok;

    auto movePlayer = object({
        {"runtime_id", bedrock::ProtoDefValue::uinteger(1)},
        {"position", object({
            {"x", bedrock::ProtoDefValue::floating(0.0)},
            {"y", bedrock::ProtoDefValue::floating(64.0)},
            {"z", bedrock::ProtoDefValue::floating(0.0)}
        })},
        {"pitch", bedrock::ProtoDefValue::floating(0.0)},
        {"yaw", bedrock::ProtoDefValue::floating(0.0)},
        {"head_yaw", bedrock::ProtoDefValue::floating(0.0)},
        {"mode", bedrock::ProtoDefValue::string("normal")},
        {"on_ground", bedrock::ProtoDefValue::boolean(true)},
        {"ridden_runtime_id", bedrock::ProtoDefValue::uinteger(0)},
        {"tick", bedrock::ProtoDefValue::uinteger(1)}
    });
    if (versionEquals(version, "1.26.10")) {
        movePlayer.objectValue["mode"] = bedrock::ProtoDefValue::uinteger(0);
    }
    ok = checkSchemaEncode(version, "move_player", movePlayer) && ok;

    ok = checkSchemaEncode(version, "player_auth_input", object({
        {"pitch", bedrock::ProtoDefValue::floating(0.0)},
        {"yaw", bedrock::ProtoDefValue::floating(0.0)},
        {"position", vec3f(0.0, 64.0, 0.0)},
        {"move_vector", vec2f(0.0, 0.0)},
        {"head_yaw", bedrock::ProtoDefValue::floating(0.0)},
        {"input_data", inputFlags()},
        {"input_mode", bedrock::ProtoDefValue::string("mouse")},
        {"play_mode", bedrock::ProtoDefValue::string("normal")},
        {"interaction_model", bedrock::ProtoDefValue::string("classic")},
        {"interact_rotation", vec2f(0.0, 0.0)},
        {"tick", bedrock::ProtoDefValue::uinteger(1)},
        {"delta", vec3f(0.0, 0.0, 0.0)},
        {"analogue_move_vector", vec2f(0.0, 0.0)},
        {"camera_orientation", vec3f(0.0, 0.0, 0.0)},
        {"raw_move_vector", vec2f(0.0, 0.0)}
    })) && ok;

    ok = checkSchemaEncode(version, "move_entity", object({
        {"runtime_entity_id", bedrock::ProtoDefValue::uinteger(1)},
        {"flags", bedrock::ProtoDefValue::uinteger(0)},
        {"position", vec3f(0.0, 64.0, 0.0)},
        {"rotation", object({
            {"yaw", bedrock::ProtoDefValue::floating(90.0)},
            {"pitch", bedrock::ProtoDefValue::floating(0.0)},
            {"head_yaw", bedrock::ProtoDefValue::floating(90.0)}
        })}
    })) && ok;

    return ok;
}

} // namespace

int main() {
    const std::vector<std::string> packetNames = {
        "play_status",
        "resource_pack_client_response",
        "client_cache_status",
        "request_chunk_radius",
        "set_local_player_as_initialized"
    };

    int checkedVersions = 0;
    int failures = 0;

    if (!checkProtoDefNativeHelpers()) {
        ++failures;
    }

    for (const auto& version : bedrock::ProtocolDefinition::versions()) {
        if (!versionAtLeast(version, "1.20.0")) {
            continue;
        }

        ++checkedVersions;
        bool ok = true;

        for (const auto& packetName : packetNames) {
            ok = checkPacketRoundtrip(version, packetName) && ok;
        }

        ok = checkBatchRoundtrip(version, false) && ok;
        ok = checkBatchRoundtrip(version, true) && ok;
        ok = checkSchemaEncodes(version) && ok;

        if (!ok) {
            ++failures;
        }

        std::cout << "[ROUNDTRIP] " << version << " " << (ok ? "ok" : "fail") << "\n";
    }

    std::cout << "[ROUNDTRIP] checkedVersions=" << checkedVersions << " failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
