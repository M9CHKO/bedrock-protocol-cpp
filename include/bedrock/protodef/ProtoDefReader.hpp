#pragma once

#include <bedrock/debug/PacketFieldDecoder.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace bedrock {

class ProtoDefReader {
public:
    explicit ProtoDefReader(PacketFieldCursor& cursor)
        : cursor_(cursor) {}

    std::size_t offset() const {
        return cursor_.offset();
    }

    std::size_t remaining() const {
        return cursor_.remaining();
    }

    uint8_t u8() {
        if (remaining() < 1) {
            throw std::runtime_error("not enough bytes for u8 at offset " + std::to_string(offset()));
        }
        return cursor_.u8();
    }

    bool tryU8(uint8_t& out) {
        if (remaining() < 1) return false;
        out = u8();
        return true;
    }

    uint16_t u16le() {
        return cursor_.u16le();
    }

    uint32_t u32le() {
        return cursor_.u32le();
    }

    uint64_t u64le() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8()) << (i * 8);
        }
        return v;
    }

    int64_t i64le() {
        return static_cast<int64_t>(u64le());
    }

    uint64_t readU64LE() {
        return u64le();
    }

    float readF32LE() {
        uint32_t raw = u32le();
        float v = 0.0f;
        std::memcpy(&v, &raw, sizeof(v));
        return v;
    }

    double readF64LE() {
        uint64_t raw = u64le();
        double v = 0.0;
        std::memcpy(&v, &raw, sizeof(v));
        return v;
    }

    int32_t i32le() {
        return cursor_.i32le();
    }

    uint32_t varuint32() {
        return cursor_.varuint32();
    }

    uint64_t varuint64() {
        return cursor_.varuint64();
    }

    int64_t varint64() {
        return static_cast<int64_t>(cursor_.varuint64());
    }

    int32_t zigzag32() {
        return cursor_.zigzag32();
    }

    int64_t zigzag64() {
        return cursor_.zigzag64();
    }

    bool boolean() {
        return cursor_.boolean();
    }

    std::string string() {
        return cursor_.string();
    }

    void skip(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            cursor_.u8();
        }
    }

    void rewindTo(std::size_t offset) {
        cursor_.rewindTo(offset);
    }

private:
    PacketFieldCursor& cursor_;
};

}
