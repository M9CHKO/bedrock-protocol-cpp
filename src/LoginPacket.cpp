#include "bedrock/LoginPacket.hpp"

#include <limits>

namespace bedrock {

void LoginPacketCodec::writeVarUInt(std::vector<uint8_t>& out, uint32_t v) {
    while (true) {
        if ((v & ~0x7fu) == 0) {
            out.push_back(static_cast<uint8_t>(v));
            return;
        }

        out.push_back(static_cast<uint8_t>((v & 0x7f) | 0x80));
        v >>= 7;
    }
}

uint32_t LoginPacketCodec::readVarUInt(const std::vector<uint8_t>& data, size_t& off) {
    uint32_t result = 0;

    for (int shift = 0; shift <= 28; shift += 7) {
        if (off >= data.size()) {
            throw LoginPacketError("readVarUInt out of range");
        }

        uint8_t byte = data[off++];

        result |= static_cast<uint32_t>(byte & 0x7f) << shift;

        if ((byte & 0x80) == 0) {
            return result;
        }
    }

    throw LoginPacketError("VarUInt too big");
}

void LoginPacketCodec::writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

uint32_t LoginPacketCodec::readU32LE(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 4 > data.size()) {
        throw LoginPacketError("readU32LE out of range");
    }

    uint32_t v =
        static_cast<uint32_t>(data[off]) |
        (static_cast<uint32_t>(data[off + 1]) << 8) |
        (static_cast<uint32_t>(data[off + 2]) << 16) |
        (static_cast<uint32_t>(data[off + 3]) << 24);

    off += 4;
    return v;
}

void LoginPacketCodec::writeU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

uint32_t LoginPacketCodec::readU32BE(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 4 > data.size()) {
        throw LoginPacketError("readU32BE out of range");
    }

    uint32_t v =
        (static_cast<uint32_t>(data[off]) << 24) |
        (static_cast<uint32_t>(data[off + 1]) << 16) |
        (static_cast<uint32_t>(data[off + 2]) << 8) |
        static_cast<uint32_t>(data[off + 3]);

    off += 4;
    return v;
}

void LoginPacketCodec::writeStringU32LE(std::vector<uint8_t>& out, const std::string& s) {
    if (s.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        throw LoginPacketError("string too large");
    }

    writeU32LE(out, static_cast<uint32_t>(s.size()));

    out.insert(
        out.end(),
        reinterpret_cast<const uint8_t*>(s.data()),
        reinterpret_cast<const uint8_t*>(s.data()) + s.size()
    );
}

std::string LoginPacketCodec::readStringU32LE(const std::vector<uint8_t>& data, size_t& off) {
    uint32_t len = readU32LE(data, off);

    if (off + len > data.size()) {
        throw LoginPacketError("string length exceeds buffer");
    }

    std::string s(
        reinterpret_cast<const char*>(data.data() + off),
        len
    );

    off += len;
    return s;
}

std::vector<uint8_t> LoginPacketCodec::encode(
    uint32_t protocolVersion,
    const std::string& identity,
    const std::string& client
) {
    std::vector<uint8_t> tokensPayload;

    writeStringU32LE(tokensPayload, identity);
    writeStringU32LE(tokensPayload, client);

    std::vector<uint8_t> out;

    // GamePacket header. Low 10 bits = packet id.
    writeVarUInt(out, PACKET_ID_LOGIN);

    // IMPORTANT:
    // bedrock-protocol serializes protocol_version as big-endian i32.
    writeU32BE(out, protocolVersion);

    // Login tokens payload length as VarUInt.
    writeVarUInt(out, static_cast<uint32_t>(tokensPayload.size()));

    out.insert(out.end(), tokensPayload.begin(), tokensPayload.end());

    return out;
}

LoginPacketData LoginPacketCodec::decode(
    const std::vector<uint8_t>& packet
) {
    size_t off = 0;

    uint32_t header = readVarUInt(packet, off);
    uint32_t packetId = header & 0x3ff;

    if (packetId != PACKET_ID_LOGIN) {
        throw LoginPacketError("not a LoginPacket");
    }

    LoginPacketData out;

    out.protocolVersion = readU32BE(packet, off);

    uint32_t payloadLen = readVarUInt(packet, off);

    if (off + payloadLen > packet.size()) {
        throw LoginPacketError("login payload length exceeds packet size");
    }

    size_t payloadEnd = off + payloadLen;

    out.identity = readStringU32LE(packet, off);
    out.client = readStringU32LE(packet, off);

    if (off != payloadEnd) {
        throw LoginPacketError("login payload has trailing bytes");
    }

    if (off != packet.size()) {
        throw LoginPacketError("packet has trailing bytes");
    }

    return out;
}

} // namespace bedrock
