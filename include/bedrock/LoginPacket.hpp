#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace bedrock {

class LoginPacketError : public std::runtime_error {
public:
    explicit LoginPacketError(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct LoginPacketData {
    uint32_t protocolVersion = 0;
    std::string identity;
    std::string client;
};

class LoginPacketCodec {
public:
    static constexpr uint32_t PACKET_ID_LOGIN = 1;

    static std::vector<uint8_t> encode(
        uint32_t protocolVersion,
        const std::string& identity,
        const std::string& client
    );

    static LoginPacketData decode(
        const std::vector<uint8_t>& packet
    );

private:
    static void writeVarUInt(std::vector<uint8_t>& out, uint32_t v);
    static uint32_t readVarUInt(const std::vector<uint8_t>& data, size_t& off);

    static void writeU32LE(std::vector<uint8_t>& out, uint32_t v);
    static uint32_t readU32LE(const std::vector<uint8_t>& data, size_t& off);

    static void writeU32BE(std::vector<uint8_t>& out, uint32_t v);
    static uint32_t readU32BE(const std::vector<uint8_t>& data, size_t& off);

    static void writeStringU32LE(std::vector<uint8_t>& out, const std::string& s);
    static std::string readStringU32LE(const std::vector<uint8_t>& data, size_t& off);
};

} // namespace bedrock
