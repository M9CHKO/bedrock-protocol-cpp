#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace bedrock {

class ProtocolTypeTsvIndex {
public:
    explicit ProtocolTypeTsvIndex(
        std::filesystem::path basePath = "data/generated/protocol-types/bedrock"
    )
        : basePath_(std::move(basePath)) {}

    std::optional<std::string> findTypeJson(
        const std::string& version,
        const std::string& typeName
    ) const {
        auto path = basePath_ / (version + ".tsv");

        std::ifstream file(path);
        if (!file) {
            return std::nullopt;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }

            auto name = line.substr(0, tab);
            if (name == typeName) {
                return line.substr(tab + 1);
            }
        }

        return std::nullopt;
    }

private:
    std::filesystem::path basePath_;
};

} // namespace bedrock
