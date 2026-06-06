#pragma once

#include <bedrock/debug/ProtocolSchemaIndex.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock {

class PacketSchemaTsvIndex {
public:
    explicit PacketSchemaTsvIndex(
        std::filesystem::path basePath = "data/generated/packet-schema/bedrock"
    )
        : basePath_(std::move(basePath)) {}

    std::optional<PacketSchemaInfo> findPacket(
        const std::string& version,
        const std::string& packetName
    ) const {
        auto path = basePath_ / (version + ".tsv");

        std::ifstream file(path);
        if (!file) {
            return std::nullopt;
        }

        PacketSchemaInfo out;
        out.packetName = packetName;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream ss(line);
            std::string packet;
            std::string field;
            std::string type;

            if (!std::getline(ss, packet, '\t')) continue;
            if (!std::getline(ss, field, '\t')) continue;
            if (!std::getline(ss, type)) continue;

            if (packet != packetName) {
                continue;
            }

            out.fields.push_back(PacketFieldSchema{field, type});
        }

        if (out.fields.empty()) {
            return std::nullopt;
        }

        return out;
    }

private:
    std::filesystem::path basePath_;
};

} // namespace bedrock
