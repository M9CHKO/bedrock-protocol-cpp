#include <bedrock/RakNetConnect.hpp>
#include <bedrock/RakNetPing.hpp>
#include <bedrock/BedrockEncryption.hpp>
#include <bedrock/BedrockKeyExchange.hpp>
#include <bedrock/LoginPacket.hpp>
#include <bedrock/auth/BedrockAuthJwt.hpp>
#include <bedrock/protocol/VersionedMcpeCodec.hpp>
#include <bedrock/server/BedrockServer.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void writeU16BE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void writeU64BE(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
}

void writeTriadLE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
}

uint16_t readU16BE(const std::vector<uint8_t>& data, std::size_t& offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("readU16BE out of range");
    uint16_t value =
        static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) << 8u) |
        static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    return value;
}

uint32_t readVarUInt(const std::vector<uint8_t>& data, std::size_t& offset) {
    uint32_t result = 0;
    uint32_t shift = 0;

    while (true) {
        if (offset >= data.size()) throw std::runtime_error("readVarUInt out of range");
        uint8_t byte = data[offset++];
        result |= static_cast<uint32_t>(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) return result;
        shift += 7;
        if (shift >= 35) throw std::runtime_error("varuint too large");
    }
}

uint32_t readTriadLE(const std::vector<uint8_t>& data, std::size_t& offset) {
    if (offset + 3 > data.size()) throw std::runtime_error("readTriadLE out of range");
    uint32_t value =
        static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1]) << 8u) |
        (static_cast<uint32_t>(data[offset + 2]) << 16u);
    offset += 3;
    return value;
}

std::vector<uint8_t> buildConnectionRequestDatagram() {
    std::vector<uint8_t> payload;
    payload.push_back(0x09);
    writeU64BE(payload, 0x0102030405060708ull);
    writeU64BE(payload, 1234567);
    payload.push_back(0x00);

    std::vector<uint8_t> out;
    out.push_back(0x80);
    writeTriadLE(out, 0);
    out.push_back(3u << 5u);
    writeU16BE(out, static_cast<uint16_t>(payload.size() * 8u));
    writeTriadLE(out, 0);
    writeTriadLE(out, 0);
    out.push_back(0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> buildReliableDatagram(
    const std::vector<uint8_t>& payload,
    uint32_t sequence,
    uint32_t reliableIndex,
    uint32_t orderedIndex
) {
    std::vector<uint8_t> out;
    out.push_back(0x80);
    writeTriadLE(out, sequence);
    out.push_back(3u << 5u);
    writeU16BE(out, static_cast<uint16_t>(payload.size() * 8u));
    writeTriadLE(out, reliableIndex);
    writeTriadLE(out, orderedIndex);
    out.push_back(0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::vector<uint8_t>> parseDatagramPayloads(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> payloads;
    if (data.empty() || data[0] < 0x80 || data[0] > 0x8f) {
        return payloads;
    }

    std::size_t offset = 1;
    (void) readTriadLE(data, offset);

    while (offset < data.size()) {
        const uint8_t flags = data[offset++];
        const uint8_t reliability = static_cast<uint8_t>((flags & 0xe0u) >> 5u);
        const uint16_t bitLength = readU16BE(data, offset);
        const std::size_t byteLength = (static_cast<std::size_t>(bitLength) + 7u) / 8u;

        if (reliability == 2 || reliability == 3 || reliability == 4 || reliability == 6 || reliability == 7) {
            (void) readTriadLE(data, offset);
        }
        if (reliability == 1 || reliability == 4) {
            (void) readTriadLE(data, offset);
        }
        if (reliability == 3 || reliability == 4 || reliability == 7) {
            (void) readTriadLE(data, offset);
            ++offset;
        }
        if (flags & 0x10u) {
            offset += 10;
        }
        if (offset + byteLength > data.size()) {
            break;
        }
        payloads.emplace_back(
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + byteLength)
        );
        offset += byteLength;
    }

    return payloads;
}

bool isConnectionRequestAcceptedDatagram(const std::vector<uint8_t>& data) {
    for (const auto& payload : parseDatagramPayloads(data)) {
        if (!payload.empty() && payload[0] == 0x10) {
            return true;
        }
    }

    return false;
}

bool checkConnectedRequestAccepted(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

    auto request = buildConnectionRequestDatagram();
    sendto(
        sock,
        request.data(),
        request.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    for (int attempt = 0; attempt < 10; ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;

        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        std::vector<uint8_t> reply(4096);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) {
            continue;
        }
        reply.resize(static_cast<std::size_t>(received));
        if (isConnectionRequestAcceptedDatagram(reply)) {
            close(sock);
            return true;
        }
    }

    close(sock);
    return false;
}

bool checkNetworkSettingsResponse(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

    auto connectionRequest = buildConnectionRequestDatagram();
    sendto(
        sock,
        connectionRequest.data(),
        connectionRequest.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    bool accepted = false;
    for (int attempt = 0; attempt < 10 && !accepted; ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        std::vector<uint8_t> reply(4096);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) continue;
        reply.resize(static_cast<std::size_t>(received));
        accepted = isConnectionRequestAcceptedDatagram(reply);
    }

    if (!accepted) {
        close(sock);
        return false;
    }

    auto codec = bedrock::VersionedMcpeCodec::forVersion("1.20.40");
    auto request = codec.packetCodec().makePacketByName("request_network_settings", {0x00, 0x00, 0x02, 0x6e});
    auto mcpe = codec.encodeMcpePayload({request}, bedrock::VersionedMcpeCompression::Uncompressed);
    auto datagram = buildReliableDatagram(mcpe, 1, 1, 1);

    sendto(
        sock,
        datagram.data(),
        datagram.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    for (int attempt = 0; attempt < 10; ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        std::vector<uint8_t> reply(8192);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) continue;
        reply.resize(static_cast<std::size_t>(received));

        for (const auto& payload : parseDatagramPayloads(reply)) {
            if (payload.empty() || payload[0] != 0xfe) continue;

            auto decoded = codec.decodeMcpePayload(payload);
            for (const auto& packet : decoded.batch.packets) {
                if (packet.name == "network_settings") {
                    close(sock);
                    return true;
                }
            }
        }
    }

    close(sock);
    return false;
}

bool waitForAccepted(int sock) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        std::vector<uint8_t> reply(4096);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) continue;
        reply.resize(static_cast<std::size_t>(received));
        if (isConnectionRequestAcceptedDatagram(reply)) {
            return true;
        }
    }

    return false;
}

bool checkLoginHandshakeResponse(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

    auto connectionRequest = buildConnectionRequestDatagram();
    sendto(
        sock,
        connectionRequest.data(),
        connectionRequest.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    if (!waitForAccepted(sock)) {
        close(sock);
        return false;
    }

    auto keys = bedrock::BedrockAuthJwt::generateP384KeyPair();
    const std::string identityPayload =
        "{\"extraData\":{\"displayName\":\"Smoke\",\"identity\":\"00000000-0000-0000-0000-000000000000\",\"XUID\":\"0\"},"
        "\"certificateAuthority\":true,"
        "\"identityPublicKey\":\"" + keys.publicKeyDerBase64 + "\"}";
    auto identityToken = bedrock::BedrockAuthJwt::signEs384Jwt(
        keys.privateKeyPem,
        keys.publicKeyDerBase64,
        identityPayload
    );
    const std::string identityJson = "{\"chain\":[\"" + identityToken + "\"]}";

    const std::string clientPayload = "{\"GameVersion\":\"1.20.40\",\"ThirdPartyName\":\"Smoke\"}";
    auto clientToken = bedrock::BedrockAuthJwt::signEs384Jwt(
        keys.privateKeyPem,
        keys.publicKeyDerBase64,
        clientPayload
    );

    auto loginFullPacket = bedrock::LoginPacketCodec::encode(622, identityJson, clientToken);
    auto codec = bedrock::VersionedMcpeCodec::forVersion("1.20.40");
    auto loginPacket = codec.packetCodec().decodeFullPacket(loginFullPacket);
    auto mcpe = codec.encodeMcpePayload({loginPacket}, bedrock::VersionedMcpeCompression::Uncompressed);
    auto datagram = buildReliableDatagram(mcpe, 1, 1, 1);

    sendto(
        sock,
        datagram.data(),
        datagram.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    std::string serverHandshakeToken;
    for (int attempt = 0; attempt < 10 && serverHandshakeToken.empty(); ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        std::vector<uint8_t> reply(8192);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) continue;
        reply.resize(static_cast<std::size_t>(received));

        for (const auto& payload : parseDatagramPayloads(reply)) {
            if (payload.empty() || payload[0] != 0xfe) continue;

            auto decoded = codec.decodeMcpePayload(payload);
            for (const auto& packet : decoded.batch.packets) {
                if (packet.name == "server_to_client_handshake") {
                    std::size_t offset = 0;
                    uint32_t tokenLen = readVarUInt(packet.payload, offset);
                    if (offset + tokenLen <= packet.payload.size()) {
                        serverHandshakeToken.assign(
                            reinterpret_cast<const char*>(packet.payload.data() + offset),
                            tokenLen
                        );
                    }
                }
            }
        }
    }

    if (serverHandshakeToken.empty()) {
        close(sock);
        return false;
    }

    auto derived = bedrock::BedrockKeyExchange::deriveFromServerHandshakeJwtAndPrivateKeyPem(
        serverHandshakeToken,
        keys.privateKeyPem
    );

    auto handshake = codec.packetCodec().makePacketByName("client_to_server_handshake", {});
    auto compressionPacket = codec.encodeCompressionPacket(
        {handshake},
        bedrock::VersionedMcpeCompression::Uncompressed
    );
    auto encrypted = bedrock::BedrockEncryption::encryptMcpePayloadGcm(
        compressionPacket,
        0,
        derived.secretKeyBytes,
        derived.iv16
    );
    auto encryptedDatagram = buildReliableDatagram(encrypted, 2, 2, 2);

    sendto(
        sock,
        encryptedDatagram.data(),
        encryptedDatagram.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target)
    );

    for (int attempt = 0; attempt < 10; ++attempt) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        std::vector<uint8_t> reply(8192);
        ssize_t received = recvfrom(sock, reply.data(), reply.size(), 0, nullptr, nullptr);
        if (received <= 0) continue;
        reply.resize(static_cast<std::size_t>(received));

        for (const auto& payload : parseDatagramPayloads(reply)) {
            if (payload.empty() || payload[0] != 0xfe) continue;

            auto plaintext = bedrock::BedrockEncryption::decryptMcpePayloadGcm(
                payload,
                0,
                derived.secretKeyBytes,
                derived.iv16
            );
            auto decoded = codec.decodeCompressionPacket(plaintext);
            for (const auto& packet : decoded.batch.packets) {
                if (packet.name == "play_status") {
                    close(sock);
                    return true;
                }
            }
        }
    }

    close(sock);
    return false;
}

} // namespace

int main() {
    std::atomic<int> connects {0};
    std::atomic<int> requestNetworkSettingsPackets {0};
    std::atomic<int> joins {0};

    auto server = bedrock::createServer({
        .host = "127.0.0.1",
        .port = 0,
        .version = "1.20.40",
        .motd = "Bedrock Protocol C++ Smoke",
        .maxPlayers = 3
    });

    server.onConnect([&](const bedrock::BedrockServerConnection& connection) {
        ++connects;
        std::cout
            << "[SMOKE] openConnection "
            << connection.address
            << ":"
            << connection.port
            << " mtu="
            << connection.mtu
            << "\n";
    });
    server.on("request_network_settings", [&](const bedrock::BedrockServerPacketEvent& event) {
        ++requestNetworkSettingsPackets;
        std::cout
            << "[SMOKE] packet "
            << event.packet.name
            << " from "
            << event.connection.address
            << ":"
            << event.connection.port
            << "\n";
    });
    server.onJoin([&](const bedrock::BedrockServerConnection& connection) {
        ++joins;
        std::cout
            << "[SMOKE] join "
            << connection.address
            << ":"
            << connection.port
            << "\n";
    });

    server.listen();
    const uint16_t port = server.boundPort();
    std::cout << "[SMOKE] listening 127.0.0.1:" << port << "\n";

    auto pong = bedrock::RakNetPinger::ping("127.0.0.1", port, 1000);
    if (!pong.ok) {
        std::cerr << "[SMOKE] ping failed: " << pong.error << "\n";
        server.close();
        return 1;
    }

    if (pong.motd != "Bedrock Protocol C++ Smoke" || pong.gameVersion != "1.20.40") {
        std::cerr << "[SMOKE] advertisement mismatch: " << pong.rawMotd << "\n";
        server.close();
        return 1;
    }

    auto open = bedrock::RakNetConnector::openConnection("127.0.0.1", port, 1400, 1000);
    if (!open.ok) {
        std::cerr << "[SMOKE] open connection failed: " << open.error << "\n";
        server.close();
        return 1;
    }

    for (int i = 0; i < 20 && connects.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (connects.load() == 0) {
        std::cerr << "[SMOKE] server did not emit open connection\n";
        server.close();
        return 1;
    }

    if (!checkConnectedRequestAccepted(port)) {
        std::cerr << "[SMOKE] connected ConnectionRequestAccepted failed\n";
        server.close();
        return 1;
    }

    if (!checkNetworkSettingsResponse(port)) {
        std::cerr << "[SMOKE] request_network_settings -> network_settings failed\n";
        server.close();
        return 1;
    }

    if (requestNetworkSettingsPackets.load() == 0) {
        std::cerr << "[SMOKE] server did not emit request_network_settings\n";
        server.close();
        return 1;
    }

    if (!checkLoginHandshakeResponse(port)) {
        std::cerr << "[SMOKE] login -> server_to_client_handshake failed\n";
        server.close();
        return 1;
    }

    if (joins.load() == 0) {
        std::cerr << "[SMOKE] server did not emit join\n";
        server.close();
        return 1;
    }

    server.close();
    std::cout << "[SMOKE] ok\n";
    return 0;
}
