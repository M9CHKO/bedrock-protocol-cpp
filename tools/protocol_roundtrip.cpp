#include <bedrock/BedrockFramer.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/protocol/VersionedPacketCodec.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
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

        if (!ok) {
            ++failures;
        }

        std::cout << "[ROUNDTRIP] " << version << " " << (ok ? "ok" : "fail") << "\n";
    }

    std::cout << "[ROUNDTRIP] checkedVersions=" << checkedVersions << " failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
