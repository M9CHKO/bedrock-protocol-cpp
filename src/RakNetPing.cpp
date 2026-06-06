#include "bedrock/RakNetPing.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace bedrock {

static const uint8_t RAKNET_MAGIC[16] = {
    0x00, 0xff, 0xff, 0x00,
    0xfe, 0xfe, 0xfe, 0xfe,
    0xfd, 0xfd, 0xfd, 0xfd,
    0x12, 0x34, 0x56, 0x78
};

static void writeU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

static void writeU64BE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
    }
}

static uint16_t readU16BE(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 2 > data.size()) {
        throw std::runtime_error("readU16BE out of range");
    }

    uint16_t v =
        (static_cast<uint16_t>(data[off]) << 8) |
        static_cast<uint16_t>(data[off + 1]);

    off += 2;
    return v;
}

static uint64_t readU64BE(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 8 > data.size()) {
        throw std::runtime_error("readU64BE out of range");
    }

    uint64_t v = 0;

    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(data[off + i]);
    }

    off += 8;
    return v;
}

static bool checkMagic(const std::vector<uint8_t>& data, size_t off) {
    if (off + 16 > data.size()) {
        return false;
    }

    for (size_t i = 0; i < 16; ++i) {
        if (data[off + i] != RAKNET_MAGIC[i]) {
            return false;
        }
    }

    return true;
}

static std::vector<std::string> splitSemi(const std::string& s) {
    std::vector<std::string> parts;
    std::string current;

    for (char c : s) {
        if (c == ';') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    parts.push_back(current);
    return parts;
}

static int toIntSafe(const std::string& s, int fallback = -1) {
    try {
        if (s.empty()) return fallback;
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

static uint64_t makeClientGuid() {
    auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();

    std::random_device rd;

    uint64_t a = static_cast<uint64_t>(now);
    uint64_t b = static_cast<uint64_t>(rd());

    return (a << 16) ^ b ^ 0xBADC0FFEEULL;
}

static int64_t nowMillis() {
    using namespace std::chrono;

    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

std::vector<uint8_t> RakNetPinger::buildUnconnectedPing() {
    std::vector<uint8_t> out;

    // RakNet ID_UNCONNECTED_PING
    out.push_back(0x01);

    // ping time, big endian
    writeU64BE(out, static_cast<uint64_t>(nowMillis()));

    // RakNet offline message magic
    out.insert(out.end(), std::begin(RAKNET_MAGIC), std::end(RAKNET_MAGIC));

    // client guid, big endian
    writeU64BE(out, makeClientGuid());

    return out;
}

RakNetPongInfo RakNetPinger::parseUnconnectedPong(
    const std::string& host,
    uint16_t port,
    const std::vector<uint8_t>& data
) {
    RakNetPongInfo info;
    info.host = host;
    info.port = port;

    try {
        if (data.size() < 35) {
            info.error = "pong packet too small";
            return info;
        }

        size_t off = 0;

        uint8_t packetId = data[off++];

        if (packetId != 0x1c) {
            std::ostringstream ss;
            ss << "unexpected packet id: 0x"
               << std::hex
               << static_cast<int>(packetId);

            info.error = ss.str();
            return info;
        }

        info.pingTime = static_cast<int64_t>(readU64BE(data, off));
        info.serverGuid = readU64BE(data, off);

        if (!checkMagic(data, off)) {
            info.error = "invalid RakNet magic in pong";
            return info;
        }

        off += 16;

        uint16_t strLen = readU16BE(data, off);

        if (off + strLen > data.size()) {
            info.error = "motd string length exceeds packet size";
            return info;
        }

        info.rawMotd.assign(
            reinterpret_cast<const char*>(&data[off]),
            strLen
        );

        auto parts = splitSemi(info.rawMotd);

        if (parts.size() > 0) info.edition = parts[0];
        if (parts.size() > 1) info.motd = parts[1];
        if (parts.size() > 2) info.protocolVersion = toIntSafe(parts[2]);
        if (parts.size() > 3) info.gameVersion = parts[3];
        if (parts.size() > 4) info.onlinePlayers = toIntSafe(parts[4]);
        if (parts.size() > 5) info.maxPlayers = toIntSafe(parts[5]);
        if (parts.size() > 6) info.serverId = parts[6];
        if (parts.size() > 7) info.subMotd = parts[7];
        if (parts.size() > 8) info.gameMode = parts[8];
        if (parts.size() > 9) info.gameModeNumeric = toIntSafe(parts[9]);
        if (parts.size() > 10) info.ipv4Port = toIntSafe(parts[10]);
        if (parts.size() > 11) info.ipv6Port = toIntSafe(parts[11]);

        info.ok = true;
        return info;
    } catch (const std::exception& e) {
        info.error = e.what();
        return info;
    }
}

RakNetPongInfo RakNetPinger::ping(
    const std::string& host,
    uint16_t port,
    int timeoutMs
) {
    RakNetPongInfo result;
    result.host = host;
    result.port = port;

    int sock = -1;
    struct addrinfo hints {};
    struct addrinfo* res = nullptr;

    try {
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        std::string portStr = std::to_string(port);

        int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);

        if (gai != 0) {
            result.error = std::string("getaddrinfo failed: ") + gai_strerror(gai);
            return result;
        }

        struct addrinfo* chosen = nullptr;

        for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
            if (p->ai_family == AF_INET || p->ai_family == AF_INET6) {
                chosen = p;
                break;
            }
        }

        if (!chosen) {
            result.error = "no usable address found";
            freeaddrinfo(res);
            return result;
        }

        sock = socket(chosen->ai_family, chosen->ai_socktype, chosen->ai_protocol);

        if (sock < 0) {
            result.error = "socket() failed";
            freeaddrinfo(res);
            return result;
        }

        auto packet = buildUnconnectedPing();

        ssize_t sent = sendto(
            sock,
            packet.data(),
            packet.size(),
            0,
            chosen->ai_addr,
            chosen->ai_addrlen
        );

        if (sent < 0 || static_cast<size_t>(sent) != packet.size()) {
            result.error = "sendto() failed";
            close(sock);
            freeaddrinfo(res);
            return result;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv {};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);

        if (ready < 0) {
            result.error = "select() failed";
            close(sock);
            freeaddrinfo(res);
            return result;
        }

        if (ready == 0) {
            result.error = "timeout waiting for pong";
            close(sock);
            freeaddrinfo(res);
            return result;
        }

        std::vector<uint8_t> buf(4096);

        ssize_t received = recvfrom(
            sock,
            buf.data(),
            buf.size(),
            0,
            nullptr,
            nullptr
        );

        if (received <= 0) {
            result.error = "recvfrom() failed";
            close(sock);
            freeaddrinfo(res);
            return result;
        }

        buf.resize(static_cast<size_t>(received));

        close(sock);
        freeaddrinfo(res);

        return parseUnconnectedPong(host, port, buf);
    } catch (const std::exception& e) {
        if (sock >= 0) {
            close(sock);
        }

        if (res) {
            freeaddrinfo(res);
        }

        result.error = e.what();
        return result;
    }
}

} // namespace bedrock
