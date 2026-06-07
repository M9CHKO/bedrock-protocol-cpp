#include <bedrock/server/RakNetServer.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>

namespace bedrock {

namespace {

constexpr uint8_t ID_UNCONNECTED_PING = 0x01;
constexpr uint8_t ID_OPEN_CONNECTION_REQUEST_1 = 0x05;
constexpr uint8_t ID_OPEN_CONNECTION_REPLY_1 = 0x06;
constexpr uint8_t ID_OPEN_CONNECTION_REQUEST_2 = 0x07;
constexpr uint8_t ID_OPEN_CONNECTION_REPLY_2 = 0x08;
constexpr uint8_t ID_CONNECTED_PING = 0x00;
constexpr uint8_t ID_CONNECTED_PONG = 0x03;
constexpr uint8_t ID_CONNECTION_REQUEST = 0x09;
constexpr uint8_t ID_CONNECTION_REQUEST_ACCEPTED = 0x10;
constexpr uint8_t ID_UNCONNECTED_PONG = 0x1c;
constexpr uint8_t ID_NACK = 0xa0;
constexpr uint8_t ID_ACK = 0xc0;

constexpr int UDP_IPV4_HEADER_SIZE = 28;

constexpr uint8_t RAKNET_MAGIC[16] = {
    0x00, 0xff, 0xff, 0x00,
    0xfe, 0xfe, 0xfe, 0xfe,
    0xfd, 0xfd, 0xfd, 0xfd,
    0x12, 0x34, 0x56, 0x78
};

uint64_t makeGuid() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device rd;
    return (static_cast<uint64_t>(now) << 16u) ^ static_cast<uint64_t>(rd()) ^ 0xBEDC0FFEEULL;
}

void writeU16BE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void writeU32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
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
    if (offset + 2 > data.size()) {
        throw std::runtime_error("readU16BE out of range");
    }

    uint16_t value =
        static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) << 8u) |
        static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    return value;
}

uint64_t readU64BE(const std::vector<uint8_t>& data, std::size_t& offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("readU64BE out of range");
    }

    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8u) | static_cast<uint64_t>(data[offset + static_cast<std::size_t>(i)]);
    }
    offset += 8;
    return value;
}

uint32_t readTriadLE(const std::vector<uint8_t>& data, std::size_t& offset) {
    if (offset + 3 > data.size()) {
        throw std::runtime_error("readTriadLE out of range");
    }

    uint32_t value =
        static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1]) << 8u) |
        (static_cast<uint32_t>(data[offset + 2]) << 16u);
    offset += 3;
    return value;
}

bool hasMagic(const std::vector<uint8_t>& data, std::size_t offset) {
    if (offset + 16 > data.size()) {
        return false;
    }

    return std::equal(std::begin(RAKNET_MAGIC), std::end(RAKNET_MAGIC), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

void appendMagic(std::vector<uint8_t>& out) {
    out.insert(out.end(), std::begin(RAKNET_MAGIC), std::end(RAKNET_MAGIC));
}

std::string sockaddrToIp(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip);
}

uint16_t sockaddrToPort(const sockaddr_in& addr) {
    return ntohs(addr.sin_port);
}

void writeRakNetAddressIPv4(std::vector<uint8_t>& out, const sockaddr_in& addr) {
    const uint8_t* ip = reinterpret_cast<const uint8_t*>(&addr.sin_addr.s_addr);
    out.push_back(4);
    out.push_back(static_cast<uint8_t>(~ip[0]));
    out.push_back(static_cast<uint8_t>(~ip[1]));
    out.push_back(static_cast<uint8_t>(~ip[2]));
    out.push_back(static_cast<uint8_t>(~ip[3]));
    writeU16BE(out, ntohs(addr.sin_port));
}

void writeRakNetAddressIPv4(
    std::vector<uint8_t>& out,
    const std::string& ipText,
    uint16_t port
) {
    in_addr addr {};
    if (inet_pton(AF_INET, ipText.c_str(), &addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + ipText);
    }

    const uint8_t* ip = reinterpret_cast<const uint8_t*>(&addr.s_addr);
    out.push_back(4);
    out.push_back(static_cast<uint8_t>(~ip[0]));
    out.push_back(static_cast<uint8_t>(~ip[1]));
    out.push_back(static_cast<uint8_t>(~ip[2]));
    out.push_back(static_cast<uint8_t>(~ip[3]));
    writeU16BE(out, port);
}

std::string defaultAdvertisement(const RakNetServerOptions& options) {
    if (!options.advertisement.empty()) {
        return options.advertisement;
    }

    std::ostringstream ss;
    ss << "MCPE;Bedrock Protocol C++;"
       << options.protocolVersion
       << ";unknown;0;"
       << options.maxPlayers
       << ";"
       << options.serverGuid
       << ";Bedrock Protocol C++;Survival;1;"
       << options.port
       << ";"
       << options.port
       << ";";
    return ss.str();
}

std::vector<uint8_t> buildUnconnectedPong(
    uint64_t pingTime,
    uint64_t serverGuid,
    const std::string& advertisement
) {
    std::vector<uint8_t> out;
    out.push_back(ID_UNCONNECTED_PONG);
    writeU64BE(out, pingTime);
    writeU64BE(out, serverGuid);
    appendMagic(out);
    writeU16BE(out, static_cast<uint16_t>(advertisement.size()));
    out.insert(out.end(), advertisement.begin(), advertisement.end());
    return out;
}

std::vector<uint8_t> buildOpenConnectionReply1(uint64_t serverGuid, uint16_t mtu) {
    std::vector<uint8_t> out;
    out.push_back(ID_OPEN_CONNECTION_REPLY_1);
    appendMagic(out);
    writeU64BE(out, serverGuid);
    out.push_back(0x00);
    writeU16BE(out, mtu);
    return out;
}

std::vector<uint8_t> buildOpenConnectionReply2(
    uint64_t serverGuid,
    const sockaddr_in& clientAddress,
    uint16_t mtu
) {
    std::vector<uint8_t> out;
    out.push_back(ID_OPEN_CONNECTION_REPLY_2);
    appendMagic(out);
    writeU64BE(out, serverGuid);
    writeRakNetAddressIPv4(out, clientAddress);
    writeU16BE(out, mtu);
    out.push_back(0x00);
    return out;
}

std::vector<uint8_t> buildAck(uint32_t sequence) {
    std::vector<uint8_t> out;
    out.push_back(ID_ACK);
    writeU16BE(out, 1);
    out.push_back(1);
    writeTriadLE(out, sequence);
    return out;
}

std::vector<uint8_t> buildConnectedPong(int64_t pingTime, int64_t pongTime) {
    std::vector<uint8_t> out;
    out.push_back(ID_CONNECTED_PONG);
    writeU64BE(out, static_cast<uint64_t>(pingTime));
    writeU64BE(out, static_cast<uint64_t>(pongTime));
    return out;
}

std::vector<uint8_t> buildConnectionRequestAccepted(
    const sockaddr_in& clientAddress,
    int64_t requestTimestamp,
    int64_t acceptedTimestamp
) {
    std::vector<uint8_t> out;
    out.push_back(ID_CONNECTION_REQUEST_ACCEPTED);
    writeRakNetAddressIPv4(out, clientAddress);
    writeU16BE(out, 0);

    for (int i = 0; i < 20; ++i) {
        writeRakNetAddressIPv4(out, "0.0.0.0", 0);
    }

    writeU64BE(out, static_cast<uint64_t>(requestTimestamp));
    writeU64BE(out, static_cast<uint64_t>(acceptedTimestamp));
    return out;
}

int64_t nowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool isReliable(uint8_t reliability) {
    return reliability == 2 || reliability == 3 || reliability == 4 ||
        reliability == 6 || reliability == 7;
}

bool isSequenced(uint8_t reliability) {
    return reliability == 1 || reliability == 4;
}

bool isOrdered(uint8_t reliability) {
    return reliability == 3 || reliability == 4 || reliability == 7;
}

struct ParsedFrame {
    uint8_t reliability = 0;
    std::vector<uint8_t> payload;
};

std::vector<ParsedFrame> parseConnectedDatagram(
    const std::vector<uint8_t>& data,
    uint32_t& sequence
) {
    std::vector<ParsedFrame> frames;
    if (data.empty() || data[0] < 0x80 || data[0] > 0x8f) {
        return frames;
    }

    std::size_t offset = 1;
    sequence = readTriadLE(data, offset);

    while (offset < data.size()) {
        ParsedFrame frame;
        const uint8_t flags = data[offset++];
        frame.reliability = static_cast<uint8_t>((flags & 0xe0u) >> 5u);
        const bool split = (flags & 0x10u) != 0;

        const uint16_t bitLength = readU16BE(data, offset);
        std::size_t byteLength = (static_cast<std::size_t>(bitLength) + 7u) / 8u;

        if (isReliable(frame.reliability)) {
            (void) readTriadLE(data, offset);
        }
        if (isSequenced(frame.reliability)) {
            (void) readTriadLE(data, offset);
        }
        if (isOrdered(frame.reliability)) {
            (void) readTriadLE(data, offset);
            if (offset >= data.size()) {
                throw std::runtime_error("ordered channel out of range");
            }
            ++offset;
        }
        if (split) {
            if (offset + 10 > data.size()) {
                throw std::runtime_error("split header out of range");
            }
            offset += 10;
        }
        if (split && byteLength == 0) {
            byteLength = data.size() - offset;
        }
        if (offset + byteLength > data.size()) {
            throw std::runtime_error("frame payload out of range");
        }

        frame.payload.assign(
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + byteLength)
        );
        offset += byteLength;
        frames.push_back(std::move(frame));
    }

    return frames;
}

std::string peerKey(const sockaddr_in& address) {
    return sockaddrToIp(address) + ":" + std::to_string(sockaddrToPort(address));
}

} // namespace

RakNetServer::RakNetServer(RakNetServerOptions options)
    : options_(std::move(options)) {
    if (options_.serverGuid == 0) {
        options_.serverGuid = makeGuid();
    }
}

RakNetServer::~RakNetServer() {
    close();
}

RakNetServer::RakNetServer(RakNetServer&& other) noexcept {
    *this = std::move(other);
}

RakNetServer& RakNetServer::operator=(RakNetServer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    close();
    options_ = std::move(other.options_);
    socket_ = other.socket_;
    other.socket_ = -1;
    boundPort_ = other.boundPort_;
    running_.store(other.running_.load());
    openConnectionHandler_ = std::move(other.openConnectionHandler_);
    rawPacketHandler_ = std::move(other.rawPacketHandler_);
    if (other.thread_.joinable()) {
        thread_ = std::move(other.thread_);
    }
    return *this;
}

void RakNetServer::listen() {
    if (running_) {
        return;
    }

    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* res = nullptr;
    const std::string portString = std::to_string(options_.port);
    const char* host = options_.host.empty() || options_.host == "0.0.0.0"
        ? nullptr
        : options_.host.c_str();

    int gai = getaddrinfo(host, portString.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(gai));
    }

    socket_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_ < 0) {
        freeaddrinfo(res);
        throw std::runtime_error("socket failed");
    }

    int yes = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (::bind(socket_, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ::close(socket_);
        socket_ = -1;
        throw std::runtime_error("bind failed");
    }

    sockaddr_in bound {};
    socklen_t boundLen = sizeof(bound);
    if (getsockname(socket_, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
        boundPort_ = ntohs(bound.sin_port);
    } else {
        boundPort_ = options_.port;
    }

    freeaddrinfo(res);

    running_ = true;
    thread_ = std::thread([this]() {
        runLoop();
    });
}

void RakNetServer::close() {
    if (!running_ && socket_ < 0) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }

    running_ = false;
    if (socket_ >= 0) {
        ::shutdown(socket_, SHUT_RDWR);
        ::close(socket_);
        socket_ = -1;
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

void RakNetServer::runLoop() {
    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_, &readfds);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ready = select(socket_ + 1, &readfds, nullptr, nullptr, &timeout);
        if (!running_) {
            break;
        }
        if (ready <= 0) {
            continue;
        }

        std::vector<uint8_t> packet(4096);
        sockaddr_in sender {};
        socklen_t senderLen = sizeof(sender);
        ssize_t received = recvfrom(
            socket_,
            packet.data(),
            packet.size(),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &senderLen
        );

        if (received <= 0) {
            continue;
        }

        packet.resize(static_cast<std::size_t>(received));
        handlePacket(packet, &sender, static_cast<int>(senderLen));
    }
}

void RakNetServer::handlePacket(
    const std::vector<uint8_t>& packet,
    const void* sender,
    int senderLen
) {
    if (packet.empty() || !sender || senderLen < static_cast<int>(sizeof(sockaddr_in))) {
        return;
    }

    const auto& senderAddr = *reinterpret_cast<const sockaddr_in*>(sender);
    const uint8_t packetId = packet[0];

    try {
        if (packetId == ID_UNCONNECTED_PING) {
            std::size_t offset = 1;
            uint64_t pingTime = readU64BE(packet, offset);
            if (!hasMagic(packet, offset)) {
                return;
            }

            auto pong = buildUnconnectedPong(
                pingTime,
                options_.serverGuid,
                defaultAdvertisement(options_)
            );
            sendTo(sender, senderLen, pong);
            return;
        }

        if (packetId == ID_OPEN_CONNECTION_REQUEST_1) {
            std::size_t offset = 1;
            if (!hasMagic(packet, offset)) {
                return;
            }
            offset += 16;
            if (offset >= packet.size()) {
                return;
            }

            const uint8_t clientProtocol = packet[offset];
            if (clientProtocol != static_cast<uint8_t>(options_.protocolVersion)) {
                // JavaScript rak backends reject incompatible RakNet protocol here.
                return;
            }

            const int mtu = static_cast<int>(packet.size()) + UDP_IPV4_HEADER_SIZE;
            auto reply = buildOpenConnectionReply1(
                options_.serverGuid,
                static_cast<uint16_t>(std::max(576, std::min(1492, mtu)))
            );
            sendTo(sender, senderLen, reply);
            return;
        }

        if (packetId == ID_OPEN_CONNECTION_REQUEST_2) {
            std::size_t offset = 1;
            if (!hasMagic(packet, offset)) {
                return;
            }
            offset += 16;

            if (offset + 7 > packet.size()) {
                return;
            }
            offset += 7;

            const uint16_t mtu = readU16BE(packet, offset);
            const uint64_t clientGuid = readU64BE(packet, offset);

            auto reply = buildOpenConnectionReply2(options_.serverGuid, senderAddr, mtu);
            sendTo(sender, senderLen, reply);

            RakNetServerPeer peer;
            peer.address = sockaddrToIp(senderAddr);
            peer.port = sockaddrToPort(senderAddr);
            peer.clientGuid = clientGuid;
            peer.mtu = mtu;
            {
                std::lock_guard<std::mutex> lock(peersMutex_);
                auto& state = peers_[peerKey(senderAddr)];
                state.peer = peer;
                state.endpointLen = senderLen;
                std::memcpy(state.endpoint.data(), sender, static_cast<std::size_t>(senderLen));
            }

            if (openConnectionHandler_) {
                openConnectionHandler_(peer);
            }
            return;
        }

        if (packetId >= 0x80 && packetId <= 0x8f) {
            uint32_t sequence = 0;
            auto frames = parseConnectedDatagram(packet, sequence);
            sendTo(sender, senderLen, buildAck(sequence));

            RakNetServerPeer peer;
            {
                std::lock_guard<std::mutex> lock(peersMutex_);
                auto& state = peers_[peerKey(senderAddr)];
                if (state.peer.address.empty()) {
                    state.peer.address = sockaddrToIp(senderAddr);
                    state.peer.port = sockaddrToPort(senderAddr);
                    state.peer.mtu = 1400;
                }
                state.endpointLen = senderLen;
                std::memcpy(state.endpoint.data(), sender, static_cast<std::size_t>(senderLen));
                peer = state.peer;
            }

            for (const auto& frame : frames) {
                if (frame.payload.empty()) {
                    continue;
                }

                if (frame.payload[0] == ID_CONNECTED_PING) {
                    std::size_t offset = 1;
                    const int64_t pingTime = static_cast<int64_t>(readU64BE(frame.payload, offset));
                    sendReliableOrdered(
                        peer,
                        sender,
                        senderLen,
                        buildConnectedPong(pingTime, nowMillis())
                    );
                    continue;
                }

                if (frame.payload[0] == ID_CONNECTION_REQUEST) {
                    std::size_t offset = 1;
                    const uint64_t clientGuid = readU64BE(frame.payload, offset);
                    const int64_t requestTimestamp = static_cast<int64_t>(readU64BE(frame.payload, offset));
                    (void) clientGuid;
                    sendReliableOrdered(
                        peer,
                        sender,
                        senderLen,
                        buildConnectionRequestAccepted(senderAddr, requestTimestamp, nowMillis())
                    );
                    continue;
                }

                if (encapsulatedHandler_) {
                    encapsulatedHandler_(peer, frame.payload);
                }
            }
            return;
        }

        if (rawPacketHandler_) {
            RakNetServerPeer peer;
            peer.address = sockaddrToIp(senderAddr);
            peer.port = sockaddrToPort(senderAddr);
            rawPacketHandler_(peer, packet);
        }
    } catch (...) {
        return;
    }
}

void RakNetServer::sendTo(const void* target, int targetLen, const std::vector<uint8_t>& packet) {
    if (socket_ < 0 || !target || packet.empty()) {
        return;
    }

    (void) sendto(
        socket_,
        packet.data(),
        packet.size(),
        0,
        reinterpret_cast<const sockaddr*>(target),
        static_cast<socklen_t>(targetLen)
    );
}

void RakNetServer::sendReliable(const RakNetServerPeer& peer, const std::vector<uint8_t>& payload) {
    std::array<uint8_t, 128> endpoint {};
    int endpointLen = 0;
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        auto it = peers_.find(peer.address + ":" + std::to_string(peer.port));
        if (it == peers_.end() || it->second.endpointLen <= 0) {
            return;
        }
        endpoint = it->second.endpoint;
        endpointLen = it->second.endpointLen;
    }

    sendReliableOrdered(peer, endpoint.data(), endpointLen, payload);
}

void RakNetServer::sendReliableOrdered(
    const RakNetServerPeer& peer,
    const void* target,
    int targetLen,
    const std::vector<uint8_t>& payload
) {
    if (payload.empty()) {
        return;
    }

    uint32_t sequence = 0;
    uint32_t reliableIndex = 0;
    uint32_t orderedIndex = 0;
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        auto& state = peers_[peer.address + ":" + std::to_string(peer.port)];
        sequence = state.outgoingSequence++;
        reliableIndex = state.reliableIndex++;
        orderedIndex = state.orderedIndex++;
    }

    std::vector<uint8_t> out;
    out.push_back(0x80);
    writeTriadLE(out, sequence);

    const uint8_t reliability = 3;
    out.push_back(static_cast<uint8_t>(reliability << 5u));
    writeU16BE(out, static_cast<uint16_t>(payload.size() * 8u));
    writeTriadLE(out, reliableIndex);
    writeTriadLE(out, orderedIndex);
    out.push_back(0);
    out.insert(out.end(), payload.begin(), payload.end());

    sendTo(target, targetLen, out);
}

} // namespace bedrock
