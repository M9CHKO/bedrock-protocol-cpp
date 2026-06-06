#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bedrock {

class BinaryStreamReader {
public:
    explicit BinaryStreamReader(const std::vector<uint8_t>& data);

    bool eof() const;
    size_t offset() const;
    size_t remaining() const;

    uint8_t readU8();
    uint16_t readLe16();
    uint32_t readLe32();
    uint32_t readBe32();

    std::vector<uint8_t> readBytes(size_t count);

private:
    const std::vector<uint8_t>& data_;
    size_t offset_ = 0;
};

class BinaryStreamWriter {
public:
    void writeU8(uint8_t value);
    void writeLe16(uint16_t value);
    void writeLe32(uint32_t value);
    void writeBe32(uint32_t value);
    void writeBytes(const std::vector<uint8_t>& bytes);

    const std::vector<uint8_t>& data() const;
    std::vector<uint8_t> take();

private:
    std::vector<uint8_t> data_;
};

} // namespace bedrock
