#pragma once

// Easy public API. Include this in bots:
//   #include <bedrock/bedrock.hpp>

#include <bedrock/api/Client.hpp>
#include <bedrock/client/BedrockNetworkClient.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/protodef/ProtoDefPacketDecoder.hpp>
#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/relay/BedrockLiveRelay.hpp>
#include <bedrock/relay/BedrockRelay.hpp>
#include <bedrock/server/BedrockServer.hpp>
#include <bedrock/world/BedrockChunk.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bedrock {

using Packet = api::Packet;
using TextPacket = api::TextPacket;
using DebugMode = api::DebugMode;
using PacketValue = ProtoDefValue;
using PacketObject = std::unordered_map<std::string, PacketValue>;
using PacketArray = std::vector<PacketValue>;

struct Options {
    std::string host = "localhost";
    uint16_t port = 19132;
    std::string username = "Bot";
    std::string version = "latest";
    bool offline = false;
    bool interactiveAuth = true;
    std::string xboxClientId;
    std::filesystem::path authCacheRoot;

    int mtu = 1400;
    int connectTimeoutMs = 9000;
    int batchingIntervalMs = 20;
    bool autoInitPlayer = true;
    bool autoResourcePackResponses = true;
    bool clientCacheEnabled = false;
    bool trackWorld = false;
    int32_t chunkRadius = 20;
    std::vector<uint8_t> loginPacket;

    DebugMode debug = DebugMode::Off;
    bool decodePackets = true;
    bool packetDump = false;
    bool quiet = true;
};

class Client {
public:
    using PacketHandler = std::function<void(const Packet&)>;
    using TextHandler = std::function<void(const TextPacket&)>;

    explicit Client(Options options = {})
        : options_(normalizeOptions(std::move(options))),
          decoder_(options_.version),
          network_(toNetworkOptions(options_)) {}

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect() {
        return network_.connect();
    }

    int run() {
        return network_.run();
    }

    void close(const std::string& reason = "closed") {
        network_.close(reason);
    }

    void on(const std::string& packetName, PacketHandler handler) {
        if (packetName == "packet") {
            onAny(std::move(handler));
            return;
        }
        subscribedPackets_.insert(packetName);
        network_.on(packetName, [this, handler = std::move(handler)](const BedrockNetworkClientPacketEvent& event) {
            handler(toApiPacket(event.packet));
        });
    }

    void onAny(PacketHandler handler) {
        decodeAnyPacket_ = true;
        network_.onAny([this, handler = std::move(handler)](const BedrockNetworkClientPacketEvent& event) {
            handler(toApiPacket(event.packet));
        });
    }

    void onText(TextHandler handler) {
        subscribedPackets_.insert("text");
        network_.on("text", [this, handler = std::move(handler)](const BedrockNetworkClientPacketEvent& event) {
            auto packet = toApiPacket(event.packet);
            TextPacket text;
            text.sourceName = packet.get("source_name");
            text.message = packet.get("message");
            text.xuid = packet.get("xuid");
            text.platformChatId = packet.get("platform_chat_id");
            handler(text);
        });
    }

    void onJoin(std::function<void()> handler) {
        network_.onJoin(std::move(handler));
    }

    void onClose(std::function<void(const std::string&)> handler) {
        network_.onClose(std::move(handler));
    }

    void onError(std::function<void(const std::string&)> handler) {
        network_.onError(std::move(handler));
    }

    void send(const std::string& packetName, ProtoDefValue value) {
        network_.send(packetName, value);
    }

    void write(const std::string& packetName, ProtoDefValue value) {
        network_.write(packetName, value);
    }

    void queue(const std::string& packetName, ProtoDefValue value) {
        queuedValues_.push_back({packetName, value});
        network_.queue(packetName, value);
    }

    void sendQueued() {
        network_.sendQueued();
    }

    const auto& queuedPacketValues() const {
        return queuedValues_;
    }

    BedrockNetworkClient& network() {
        return network_;
    }

    const BedrockNetworkClient& network() const {
        return network_;
    }

    BedrockWorld& world() {
        return network_.world();
    }

    const BedrockWorld& world() const {
        return network_.world();
    }

private:
    Options options_;
    ProtoDefPacketDecoder decoder_;
    BedrockNetworkClient network_;
    std::vector<std::pair<std::string, ProtoDefValue>> queuedValues_;
    std::unordered_set<std::string> subscribedPackets_;
    bool decodeAnyPacket_ = false;

    static Options normalizeOptions(Options options) {
        if (options.version.empty() || options.version == "auto" || options.version == "latest") {
            auto vs = ProtocolDefinition::versions();
            if (!vs.empty()) options.version = vs.back();
        }
        return options;
    }

    static BedrockNetworkClientOptions toNetworkOptions(const Options& options) {
        BedrockNetworkClientOptions out;
        out.host = options.host;
        out.port = options.port;
        out.username = options.username;
        out.profile = options.username.empty()
            ? std::string("Bot")
            : options.username;
        out.version = options.version;
        out.offline = options.offline;
        out.interactiveAuth = options.interactiveAuth;
        out.xboxClientId = options.xboxClientId;
        out.authCacheRoot = options.authCacheRoot;
        out.mtu = options.mtu;
        out.connectTimeoutMs = options.connectTimeoutMs;
        out.batchingIntervalMs = options.batchingIntervalMs;
        out.autoInitPlayer = options.autoInitPlayer;
        out.autoResourcePackResponses = options.autoResourcePackResponses;
        out.clientCacheEnabled = options.clientCacheEnabled;
        out.trackWorld = options.trackWorld;
        out.chunkRadius = options.chunkRadius;
        out.loginPacket = options.loginPacket;
        return out;
    }

    Packet toApiPacket(const VersionedGamePacket& packet) const {
        Packet out;
        out.id = packet.packetId;
        out.name = packet.name;
        out.ok = true;

        if (options_.decodePackets) {
            bool shouldDecode =
                decodeAnyPacket_ ||
                subscribedPackets_.find(packet.name) != subscribedPackets_.end();

            if (!shouldDecode) {
                return out;
            }

            try {
                auto fields = decoder_.decodePacket(packet.name, packet.payload);

                for (const auto& field : fields) {
                    out.fields[field.path] = field.value;

                    auto c1 = field.value.find(',');
                    auto c2 = c1 == std::string::npos
                        ? std::string::npos
                        : field.value.find(',', c1 + 1);

                    if (c1 != std::string::npos && c2 != std::string::npos) {
                        out.fields[field.path + ".x"] = field.value.substr(0, c1);
                        out.fields[field.path + ".y"] = field.value.substr(c1 + 1, c2 - c1 - 1);
                        out.fields[field.path + ".z"] = field.value.substr(c2 + 1);
                    }

                    auto dot = field.path.rfind('.');
                    if (dot != std::string::npos) {
                        out.fields[field.path.substr(dot + 1)] = field.value;
                    }
                }
            } catch (const std::exception& e) {
                out.ok = false;
                out.fields["decode_error"] = e.what();
            }
        }

        if (options_.debug != DebugMode::Off && (!options_.quiet || options_.packetDump)) {
            std::cout << "[packet] " << out.name << " id=" << out.id << "\n";
        }
        return out;
    }
};

// Direct in-process network client, if needed later.






inline Client createClient(Options options = {}) {
    return Client(std::move(options));
}

inline Client createNetworkClient(Options options = {}) {
    return Client(std::move(options));
}

inline api::Client createExternalClient(api::ClientOptions options = {}) {
    return api::createClient(std::move(options));
}

inline std::vector<std::string> versions() {
    return ProtocolDefinition::versions();
}

inline bool supportsVersion(const std::string& version) {
    return ProtocolDefinition::supportsVersion(version);
}

inline PacketValue nil() {
    return PacketValue::null();
}

inline PacketValue boolean(bool value) {
    return PacketValue::boolean(value);
}

inline PacketValue i64(int64_t value) {
    return PacketValue::integer(value);
}

inline PacketValue i32(int32_t value) {
    return PacketValue::integer(value);
}

inline PacketValue u64(uint64_t value) {
    return PacketValue::uinteger(value);
}

inline PacketValue u32(uint32_t value) {
    return PacketValue::uinteger(value);
}

inline PacketValue f64(double value) {
    return PacketValue::floating(value);
}

inline PacketValue f32(float value) {
    return PacketValue::floating(value);
}

inline PacketValue str(std::string value) {
    return PacketValue::string(std::move(value));
}

inline PacketValue bytes(std::vector<uint8_t> value) {
    return PacketValue::bytes(std::move(value));
}

inline PacketValue object(PacketObject fields) {
    return PacketValue::object(std::move(fields));
}

inline PacketValue object(std::initializer_list<std::pair<const std::string, PacketValue>> fields) {
    return PacketValue::object(PacketObject(fields));
}

inline PacketValue array(PacketArray values) {
    return PacketValue::array(std::move(values));
}

inline PacketValue array(std::initializer_list<PacketValue> values) {
    return PacketValue::array(PacketArray(values));
}

} // namespace bedrock
