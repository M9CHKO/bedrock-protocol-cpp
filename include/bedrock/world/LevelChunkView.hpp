#pragma once

#include <bedrock/world/LevelChunk.hpp>
#include <bedrock/world/SubChunk.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace bedrock {

struct LevelChunkView {
    LevelChunkHeader header;
    SubChunkScanResult subChunks;

    bool hasSubChunks() const {
        return !subChunks.sections.empty();
    }

    std::size_t subChunkCountDecoded() const {
        return subChunks.sections.size();
    }

    uint32_t getRuntimeId(
        std::size_t subChunkIndex,
        std::size_t storageIndex,
        std::size_t blockIndex
    ) const {
        if (subChunkIndex >= subChunks.sections.size()) {
            throw std::runtime_error("subchunk index outside chunk");
        }

        const auto& section = subChunks.sections[subChunkIndex];

        if (storageIndex >= section.storages.size()) {
            throw std::runtime_error("storage index outside subchunk");
        }

        return section.storages[storageIndex].getBlockRuntimeId(blockIndex);
    }

    static LevelChunkView parse(const std::vector<uint8_t>& payload) {
        LevelChunkView out;

        out.header = LevelChunkParser::readHeader(payload);

        if (!out.header.data.empty() && out.header.subChunkCount > 0) {
            out.subChunks = SubChunkParser::scanWithStorages(
                out.header.data,
                out.header.subChunkCount
            );
        }

        return out;
    }
};

} // namespace bedrock
