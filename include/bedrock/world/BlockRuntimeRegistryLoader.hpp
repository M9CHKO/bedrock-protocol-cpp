#pragma once

#include <bedrock/world/BlockRuntimeRegistry.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace bedrock {

class BlockRuntimeRegistryLoader {
public:
    static BlockRuntimeRegistry loadTsv(const std::string& path) {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("failed to open block runtime registry: " + path);
        }

        BlockRuntimeRegistry registry;

        std::string line;
        std::size_t lineNo = 0;

        while (std::getline(file, line)) {
            ++lineNo;

            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream ss(line);
            std::string idText;
            std::string name;

            if (!std::getline(ss, idText, '\t') || !std::getline(ss, name)) {
                throw std::runtime_error("bad block runtime registry line: " + std::to_string(lineNo));
            }

            uint32_t runtimeId = static_cast<uint32_t>(std::stoul(idText));
            registry.add(runtimeId, name);
        }

        return registry;
    }
};

} // namespace bedrock
