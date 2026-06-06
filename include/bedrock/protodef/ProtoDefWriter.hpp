#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bedrock {

class ProtoDefWriter {
public:
    const std::vector<uint8_t>& data() const {
        return data_;
    }

    std::vector<uint8_t> take() {
        return std::move(data_);
    }

    void u8(uint8_t v) {
        data_.push_back(v);
    }

    void boolValue(bool v) {
        u8(v ? 1 : 0);
    }

    void u16le(uint16_t v) {
        data_.push_back(static_cast<uint8_t>(v & 0xff));
        data_.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    }

    void u32le(uint32_t v) {
        data_.push_back(static_cast<uint8_t>(v & 0xff));
        data_.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        data_.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        data_.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    }

    void u64le(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            data_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
        }
    }

    void varuint32(uint32_t v) {
        while (v >= 0x80) {
            data_.push_back(static_cast<uint8_t>((v & 0x7f) | 0x80));
            v >>= 7;
        }
        data_.push_back(static_cast<uint8_t>(v));
    }

    void zigzag32(int32_t v) {
        uint32_t encoded = (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
        varuint32(encoded);
    }

    void zigzag64(int64_t v) {
        uint64_t encoded =
            (static_cast<uint64_t>(v) << 1) ^
            static_cast<uint64_t>(v >> 63);

        while (encoded >= 0x80) {
            data_.push_back(static_cast<uint8_t>((encoded & 0x7f) | 0x80));
            encoded >>= 7;
        }

        data_.push_back(static_cast<uint8_t>(encoded));
    }

    void varuint128(unsigned __int128 v) {
        while (v >= 0x80) {
            data_.push_back(static_cast<uint8_t>((static_cast<uint8_t>(v) & 0x7f) | 0x80));
            v >>= 7;
        }

        data_.push_back(static_cast<uint8_t>(v));
    }

    void f32le(float value) {
        static_assert(sizeof(float) == 4, "float must be 4 bytes");
        uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        u32le(raw);
    }

    void f64le(double value) {
        static_assert(sizeof(double) == 8, "double must be 8 bytes");
        uint64_t raw = 0;
        std::memcpy(&raw, &value, 8);
        u64le(raw);
    }

    void string(const std::string& s) {
        varuint32(static_cast<uint32_t>(s.size()));
        bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void shortString(const std::string& s) {
        u16le(static_cast<uint16_t>(s.size()));
        bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void bytes(const uint8_t* p, std::size_t n) {
        data_.insert(data_.end(), p, p + n);
    }

    void bytes(const std::vector<uint8_t>& v) {
        data_.insert(data_.end(), v.begin(), v.end());
    }

private:
    std::vector<uint8_t> data_;
};

}
