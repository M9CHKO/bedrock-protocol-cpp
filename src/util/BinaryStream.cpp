#include <bedrock/util/BinaryStream.hpp>

#include <stdexcept>

namespace bedrock {

BinaryStreamReader::BinaryStreamReader(const std::vector<uint8_t>& data)
    : data_(data) {
}

bool BinaryStreamReader::eof() const {
    return offset_ >= data_.size();
}

size_t BinaryStreamReader::offset() const {
    return offset_;
}

size_t BinaryStreamReader::remaining() const {
    if (offset_ >= data_.size()) {
        return 0;
    }
    return data_.size() - offset_;
}

uint8_t BinaryStreamReader::readU8() {
    if (remaining() < 1) {
        throw std::runtime_error("BinaryStreamReader::readU8: unexpected end");
    }

    return data_[offset_++];
}

uint16_t BinaryStreamReader::readLe16() {
    if (remaining() < 2) {
        throw std::runtime_error("BinaryStreamReader::readLe16: unexpected end");
    }

    uint16_t value = static_cast<uint16_t>(data_[offset_])
        | static_cast<uint16_t>(data_[offset_ + 1] << 8);

    offset_ += 2;
    return value;
}

uint32_t BinaryStreamReader::readLe32() {
    if (remaining() < 4) {
        throw std::runtime_error("BinaryStreamReader::readLe32: unexpected end");
    }

    uint32_t value =
        static_cast<uint32_t>(data_[offset_])
        | (static_cast<uint32_t>(data_[offset_ + 1]) << 8)
        | (static_cast<uint32_t>(data_[offset_ + 2]) << 16)
        | (static_cast<uint32_t>(data_[offset_ + 3]) << 24);

    offset_ += 4;
    return value;
}

uint32_t BinaryStreamReader::readBe32() {
    if (remaining() < 4) {
        throw std::runtime_error("BinaryStreamReader::readBe32: unexpected end");
    }

    uint32_t value =
        (static_cast<uint32_t>(data_[offset_]) << 24)
        | (static_cast<uint32_t>(data_[offset_ + 1]) << 16)
        | (static_cast<uint32_t>(data_[offset_ + 2]) << 8)
        | static_cast<uint32_t>(data_[offset_ + 3]);

    offset_ += 4;
    return value;
}

std::vector<uint8_t> BinaryStreamReader::readBytes(size_t count) {
    if (remaining() < count) {
        throw std::runtime_error("BinaryStreamReader::readBytes: unexpected end");
    }

    std::vector<uint8_t> out(data_.begin() + static_cast<long>(offset_),
                             data_.begin() + static_cast<long>(offset_ + count));
    offset_ += count;
    return out;
}

void BinaryStreamWriter::writeU8(uint8_t value) {
    data_.push_back(value);
}

void BinaryStreamWriter::writeLe16(uint16_t value) {
    data_.push_back(static_cast<uint8_t>(value & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void BinaryStreamWriter::writeLe32(uint32_t value) {
    data_.push_back(static_cast<uint8_t>(value & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void BinaryStreamWriter::writeBe32(uint32_t value) {
    data_.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    data_.push_back(static_cast<uint8_t>(value & 0xff));
}

void BinaryStreamWriter::writeBytes(const std::vector<uint8_t>& bytes) {
    data_.insert(data_.end(), bytes.begin(), bytes.end());
}

const std::vector<uint8_t>& BinaryStreamWriter::data() const {
    return data_;
}

std::vector<uint8_t> BinaryStreamWriter::take() {
    return std::move(data_);
}

} // namespace bedrock
