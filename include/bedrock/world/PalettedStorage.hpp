#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

struct PalettedStorageHeader {
    uint8_t raw = 0;
    uint8_t bitsPerBlock = 0;
    bool runtime = false;
};

struct PalettedStorageInfo {
    PalettedStorageHeader header;
    std::size_t offset = 0;
    std::size_t wordCount = 0;
    std::size_t paletteCount = 0;
    std::size_t totalBytes = 0;
    std::vector<uint32_t> words;
    std::vector<uint32_t> runtimePalette;

    uint32_t getBlockRuntimeId(std::size_t blockIndex) const {
        if (blockIndex >= 4096) {
            throw std::runtime_error("block index outside paletted storage");
        }

        if (runtimePalette.empty()) {
            throw std::runtime_error("runtime palette is empty");
        }

        if (header.bitsPerBlock == 0) {
            return runtimePalette[0];
        }

        const std::size_t bits = header.bitsPerBlock;
        const std::size_t bitIndex = blockIndex * bits;
        const std::size_t wordIndex = bitIndex / 32;
        const std::size_t bitOffset = bitIndex % 32;

        if (wordIndex >= words.size()) {
            throw std::runtime_error("paletted storage word index outside data");
        }

        uint64_t value = words[wordIndex] >> bitOffset;

        if (bitOffset + bits > 32) {
            if (wordIndex + 1 >= words.size()) {
                throw std::runtime_error("paletted storage cross-word read outside data");
            }

            value |= static_cast<uint64_t>(words[wordIndex + 1]) << (32 - bitOffset);
        }

        const uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1ULL);
        const std::size_t paletteIndex = static_cast<std::size_t>(value & mask);

        if (paletteIndex >= runtimePalette.size()) {
            throw std::runtime_error("palette index outside runtime palette");
        }

        return runtimePalette[paletteIndex];
    }
};

class PalettedStorageParser {
public:
    static PalettedStorageHeader readHeader(uint8_t byte) {
        PalettedStorageHeader out;
        out.raw = byte;
        out.bitsPerBlock = byte >> 1;
        out.runtime = (byte & 0x01) != 0;
        return out;
    }

    static std::size_t wordsFor4096Blocks(uint8_t bitsPerBlock) {
        if (bitsPerBlock == 0) {
            return 0;
        }

        const std::size_t blocks = 4096;
        const std::size_t bits = static_cast<std::size_t>(bitsPerBlock) * blocks;
        return (bits + 31) / 32;
    }

    static PalettedStorageInfo scanAt(
        const std::vector<uint8_t>& data,
        std::size_t offset
    ) {
        require(data, offset, 1, "paletted storage header");

        PalettedStorageInfo out;
        out.offset = offset;
        out.header = readHeader(data[offset++]);

        out.wordCount = wordsFor4096Blocks(out.header.bitsPerBlock);

        const std::size_t wordsBytes = out.wordCount * 4;
        require(data, offset, wordsBytes, "paletted storage words");

        out.words.reserve(out.wordCount);
        for (std::size_t i = 0; i < out.wordCount; ++i) {
            uint32_t word =
                static_cast<uint32_t>(data[offset]) |
                (static_cast<uint32_t>(data[offset + 1]) << 8) |
                (static_cast<uint32_t>(data[offset + 2]) << 16) |
                (static_cast<uint32_t>(data[offset + 3]) << 24);

            out.words.push_back(word);
            offset += 4;
        }

        out.paletteCount = readUVarInt(data, offset);

        // Пока NBT/runtime palette полностью не декодируем.
        // Для runtime palette entries идут как varint runtime ids.
        // Для network-persistent NBT palette нужен полноценный NBT reader.
        out.runtimePalette.reserve(out.paletteCount);

        for (std::size_t i = 0; i < out.paletteCount; ++i) {
            if (out.header.runtime) {
                out.runtimePalette.push_back(readUVarInt(data, offset));
            } else {
                throw std::runtime_error("NBT palette scan is not implemented yet");
            }
        }

        out.totalBytes = offset - out.offset;
        return out;
    }

private:
    static void require(
        const std::vector<uint8_t>& data,
        std::size_t offset,
        std::size_t size,
        const char* what
    ) {
        if (offset + size > data.size()) {
            throw std::runtime_error(std::string("not enough bytes for ") + what);
        }
    }

    static uint32_t readUVarInt(
        const std::vector<uint8_t>& data,
        std::size_t& offset
    ) {
        uint32_t value = 0;

        for (int shift = 0; shift <= 28; shift += 7) {
            require(data, offset, 1, "uvarint");
            uint8_t byte = data[offset++];

            value |= static_cast<uint32_t>(byte & 0x7f) << shift;

            if ((byte & 0x80) == 0) {
                return value;
            }
        }

        throw std::runtime_error("uvarint too long");
    }
};

inline void writePalettedUVarInt(std::vector<uint8_t>& out, uint32_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }

    out.push_back(static_cast<uint8_t>(value));
}

inline void writePalettedU32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

} // namespace bedrock
