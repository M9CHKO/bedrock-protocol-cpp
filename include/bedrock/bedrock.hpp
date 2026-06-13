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
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
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
    std::string clientDataJson;

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
        out.clientDataJson = options.clientDataJson;
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

struct RelayDestination {
    std::string host = "127.0.0.1";
    uint16_t port = 19132;
    bool offline = false;
};

struct RelayOptions {
    std::string version = "latest";
    std::string host = "0.0.0.0";
    uint16_t port = 19132;
    std::string motd = "Bedrock Protocol C++ Relay";
    std::string username = "RelayBot";
    bool offline = false;
    int maxPlayers = 3;
    RelayDestination destination;
};

struct RelayPacketDestination {
    bool canceled = false;

    void cancel() {
        canceled = true;
    }
};

class RelayPacketEvent {
public:
    BedrockRelayDirection direction = BedrockRelayDirection::Clientbound;
    std::string name;
    PacketObject params;
    VersionedGamePacket packet;
    bool canceled = false;

    RelayPacketEvent(
        std::string version,
        BedrockRelayPacketEvent& event
    ) : version_(std::move(version)),
        direction(event.direction),
        name(event.packet.name),
        packet(event.packet) {}

    void cancel() {
        canceled = true;
        replacements_.clear();
    }

    bool has(const std::string& key) const {
        ensureDecoded();
        return params.find(key) != params.end();
    }

    PacketObject& decodedParams() {
        ensureDecoded();
        mutated_ = true;
        return params;
    }

    const PacketObject& decodedParams() const {
        ensureDecoded();
        return params;
    }

    PacketObject& paramObject() {
        return decodedParams();
    }

    const PacketObject& paramObject() const {
        return decodedParams();
    }

    PacketValue& operator[](const std::string& key) {
        ensureDecoded();
        mutated_ = true;
        return params[key];
    }

    void set(const std::string& key, PacketValue value) {
        ensureDecoded();
        params[key] = std::move(value);
        mutated_ = true;
    }

    void set(const std::string& key, const std::string& value) {
        set(key, PacketValue::string(value));
    }

    void set(const std::string& key, const char* value) {
        set(key, PacketValue::string(value ? std::string(value) : std::string()));
    }

    void set(const std::string& key, bool value) {
        set(key, PacketValue::boolean(value));
    }

    void set(const std::string& key, int64_t value) {
        set(key, PacketValue::integer(value));
    }

    void set(const std::string& key, uint64_t value) {
        set(key, PacketValue::uinteger(value));
    }

    void set(const std::string& key, double value) {
        set(key, PacketValue::floating(value));
    }

    std::string get(const std::string& key) const {
        ensureDecoded();
        auto it = params.find(key);
        if (it == params.end()) return {};
        const auto& value = it->second;
        if (value.kind == PacketValue::Kind::String) return value.stringValue;
        if (value.kind == PacketValue::Kind::Bool) return value.boolValue ? "true" : "false";
        if (value.kind == PacketValue::Kind::Int) return std::to_string(value.intValue);
        if (value.kind == PacketValue::Kind::UInt) return std::to_string(value.uintValue);
        if (value.kind == PacketValue::Kind::Double) return std::to_string(value.doubleValue);
        return {};
    }

    void replace(VersionedGamePacket replacement) {
        canceled = false;
        replacements_.clear();
        replacements_.push_back(std::move(replacement));
    }

private:
    friend class Relay;
    std::string version_;
    mutable bool decoded_ = false;
    bool mutated_ = false;
    std::vector<VersionedGamePacket> replacements_;

    void ensureDecoded() const {
        if (decoded_) {
            return;
        }

        auto* self = const_cast<RelayPacketEvent*>(this);
        ProtoDefPacketDecoder decoder(version_);
        for (const auto& field : decoder.decodePacket(packet.name, packet.payload)) {
            if (field.path.find('.') == std::string::npos &&
                field.path.find('[') == std::string::npos) {
                self->params[field.path] = valueFromDecodedField(field);
            }
        }
        self->decoded_ = true;
    }

    static PacketValue valueFromDecodedField(const ProtoDefField& field) {
        if (field.type.rfind("mapper<", 0) == 0) {
            auto slash = field.value.find('/');
            if (slash != std::string::npos && slash + 1 < field.value.size()) {
                return PacketValue::string(field.value.substr(slash + 1));
            }
            if (isIntegerText(field.value)) {
                return PacketValue::uinteger(static_cast<uint64_t>(std::strtoull(field.value.c_str(), nullptr, 10)));
            }
            return PacketValue::string(field.value);
        }

        if (field.type == "bool") {
            return PacketValue::boolean(field.value == "true" || field.value == "1");
        }

        if (field.type == "lf32" || field.type == "lf64" ||
            field.type == "bf32" || field.type == "bf64") {
            return PacketValue::floating(std::strtod(field.value.c_str(), nullptr));
        }

        if (isSignedType(field.type) && isIntegerText(field.value)) {
            return PacketValue::integer(static_cast<int64_t>(std::strtoll(field.value.c_str(), nullptr, 10)));
        }

        if (isUnsignedType(field.type) && isIntegerText(field.value)) {
            return PacketValue::uinteger(static_cast<uint64_t>(std::strtoull(field.value.c_str(), nullptr, 10)));
        }

        return PacketValue::string(field.value);
    }

    static bool isIntegerText(const std::string& value) {
        if (value.empty()) {
            return false;
        }
        std::size_t pos = value[0] == '-' ? 1 : 0;
        if (pos >= value.size()) {
            return false;
        }
        for (; pos < value.size(); ++pos) {
            if (value[pos] < '0' || value[pos] > '9') {
                return false;
            }
        }
        return true;
    }

    static bool isSignedType(const std::string& type) {
        return type == "i8" || type == "i16" || type == "li16" ||
            type == "i32" || type == "li32" ||
            type == "i64" || type == "li64" ||
            type == "zigzag32" || type == "zigzag64" ||
            type == "varint" || type == "varint64";
    }

    static bool isUnsignedType(const std::string& type) {
        return type == "u8" || type == "u16" || type == "lu16" ||
            type == "u32" || type == "lu32" ||
            type == "u64" || type == "lu64" ||
            type == "varuint" || type == "varuint32" ||
            type == "varuint64" || type == "varint128";
    }

    void apply(BedrockRelayPacketEvent& event) {
        if (canceled) {
            event.cancel();
            return;
        }
        if (!replacements_.empty()) {
            event.replace(std::move(replacements_));
            return;
        }
        if (!mutated_) {
            return;
        }

        try {
            ProtoDefPacketEncoder encoder(version_);
            auto payload = encoder.encodePacket(name, PacketValue::object(params));
            VersionedMcpeCodec codec = VersionedMcpeCodec::forVersion(version_);
            event.replace(codec.packetCodec().makePacketByName(name, payload));
        } catch (const std::exception&) {
            // Keep the original packet flowing if user-level params cannot be
            // re-encoded yet. Relay traffic must stay byte-for-byte safe.
        }
    }
};

class RelayPlayer {
public:
    class Upstream {
    public:
        explicit Upstream(BedrockLiveRelay* relay = nullptr) : relay_(relay) {}

        void queue(const std::string& packetName, PacketValue value) {
            if (!relay_ || !relay_->upstream()) return;
            relay_->upstream()->queue(packetName, value);
        }

        void write(const std::string& packetName, PacketValue value) {
            if (!relay_ || !relay_->upstream()) return;
            relay_->upstream()->write(packetName, value);
        }

    private:
        BedrockLiveRelay* relay_ = nullptr;
    };

    using PacketHandler = std::function<void(RelayPacketEvent&)>;
    using PacketWithDestinationHandler = std::function<void(RelayPacketEvent&, RelayPacketDestination&)>;

    BedrockServerConnection connection;
    Upstream upstream;

    RelayPlayer() = default;
    explicit RelayPlayer(BedrockLiveRelay* relay)
        : upstream(relay),
          relay_(relay) {}

    void on(const std::string& direction, PacketHandler handler) {
        if (direction == "clientbound") {
            clientboundHandlers_.push_back(std::move(handler));
            return;
        }
        if (direction == "serverbound") {
            serverboundHandlers_.push_back(std::move(handler));
            return;
        }
        throw std::runtime_error("unknown relay player direction: " + direction);
    }

    void on(const std::string& direction, PacketWithDestinationHandler handler) {
        if (direction == "clientbound") {
            clientboundDestinationHandlers_.push_back(std::move(handler));
            return;
        }
        if (direction == "serverbound") {
            serverboundDestinationHandlers_.push_back(std::move(handler));
            return;
        }
        throw std::runtime_error("unknown relay player direction: " + direction);
    }

    void onClientbound(PacketHandler handler) {
        on("clientbound", std::move(handler));
    }

    void onClientbound(PacketWithDestinationHandler handler) {
        on("clientbound", std::move(handler));
    }

    void onServerbound(PacketHandler handler) {
        on("serverbound", std::move(handler));
    }

    void onServerbound(PacketWithDestinationHandler handler) {
        on("serverbound", std::move(handler));
    }

    void queue(const std::string& packetName, PacketValue value) {
        if (!relay_) return;
        relay_->server().send(connection, packetName, value);
    }

    void write(const std::string& packetName, PacketValue value) {
        queue(packetName, std::move(value));
    }

private:
    friend class Relay;
    BedrockLiveRelay* relay_ = nullptr;
    std::vector<PacketHandler> clientboundHandlers_;
    std::vector<PacketHandler> serverboundHandlers_;
    std::vector<PacketWithDestinationHandler> clientboundDestinationHandlers_;
    std::vector<PacketWithDestinationHandler> serverboundDestinationHandlers_;

    void dispatch(RelayPacketEvent& event) {
        auto& handlers = event.direction == BedrockRelayDirection::Clientbound
            ? clientboundHandlers_
            : serverboundHandlers_;
        for (auto& handler : handlers) {
            handler(event);
        }

        auto& destinationHandlers = event.direction == BedrockRelayDirection::Clientbound
            ? clientboundDestinationHandlers_
            : serverboundDestinationHandlers_;
        RelayPacketDestination des;
        for (auto& handler : destinationHandlers) {
            handler(event, des);
            if (des.canceled) {
                event.cancel();
            }
        }
    }
};

class Relay {
public:
    using ConnectHandler = std::function<void(RelayPlayer&)>;
    using DisconnectHandler = std::function<void(RelayPlayer&)>;
    using PacketHandler = std::function<void(RelayPacketEvent&)>;
    using PacketWithDestinationHandler = std::function<void(RelayPacketEvent&, RelayPacketDestination&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using StatusHandler = std::function<void(const BedrockLiveRelayStatus&)>;

    explicit Relay(RelayOptions options)
        : options_(normalizeOptions(std::move(options))),
          live_(toLiveOptions(options_)),
          player_(&live_) {}

    void listen() {
        live_.onConnect([this](const BedrockServerConnection& connection) {
            player_.connection = connection;
            for (auto& handler : connectHandlers_) {
                handler(player_);
            }
        });

        live_.onDisconnect([this](const BedrockServerConnection& connection) {
            player_.connection = connection;
            for (auto& handler : disconnectHandlers_) {
                handler(player_);
            }
        });

        live_.on("clientbound", [this](BedrockRelayPacketEvent& event) {
            RelayPacketEvent wrapped(options_.version, event);
            player_.dispatch(wrapped);
            for (auto& handler : clientboundHandlers_) {
                handler(wrapped);
            }
            RelayPacketDestination des;
            for (auto& handler : clientboundDestinationHandlers_) {
                handler(wrapped, des);
                if (des.canceled) {
                    wrapped.cancel();
                }
            }
            wrapped.apply(event);
        });

        live_.on("serverbound", [this](BedrockRelayPacketEvent& event) {
            RelayPacketEvent wrapped(options_.version, event);
            player_.dispatch(wrapped);
            for (auto& handler : serverboundHandlers_) {
                handler(wrapped);
            }
            RelayPacketDestination des;
            for (auto& handler : serverboundDestinationHandlers_) {
                handler(wrapped, des);
                if (des.canceled) {
                    wrapped.cancel();
                }
            }
            wrapped.apply(event);
        });

        live_.listen();
    }

    int run() {
        listen();
        return live_.run();
    }

    void close(const std::string& reason = "closed") {
        live_.close(reason);
    }

    void on(const std::string& eventName, ConnectHandler handler) {
        if (eventName != "connect") {
            throw std::runtime_error("unknown relay event: " + eventName);
        }
        connectHandlers_.push_back(std::move(handler));
    }

    void on(const std::string& direction, PacketHandler handler) {
        if (direction == "clientbound") {
            clientboundHandlers_.push_back(std::move(handler));
            return;
        }
        if (direction == "serverbound") {
            serverboundHandlers_.push_back(std::move(handler));
            return;
        }
        throw std::runtime_error("unknown relay packet direction: " + direction);
    }

    void on(const std::string& direction, PacketWithDestinationHandler handler) {
        if (direction == "clientbound") {
            clientboundDestinationHandlers_.push_back(std::move(handler));
            return;
        }
        if (direction == "serverbound") {
            serverboundDestinationHandlers_.push_back(std::move(handler));
            return;
        }
        throw std::runtime_error("unknown relay packet direction: " + direction);
    }

    void onConnect(ConnectHandler handler) {
        on("connect", std::move(handler));
    }

    void onDisconnect(DisconnectHandler handler) {
        disconnectHandlers_.push_back(std::move(handler));
    }

    void onClientbound(PacketHandler handler) {
        on("clientbound", std::move(handler));
    }

    void onClientbound(PacketWithDestinationHandler handler) {
        on("clientbound", std::move(handler));
    }

    void onServerbound(PacketHandler handler) {
        on("serverbound", std::move(handler));
    }

    void onServerbound(PacketWithDestinationHandler handler) {
        on("serverbound", std::move(handler));
    }

    void onError(ErrorHandler handler) {
        live_.onError(std::move(handler));
    }

    void onStatus(StatusHandler handler) {
        live_.onStatus(std::move(handler));
    }

    BedrockLiveRelay& live() { return live_; }
    RelayPlayer& player() { return player_; }

private:
    RelayOptions options_;
    BedrockLiveRelay live_;
    RelayPlayer player_;
    std::vector<ConnectHandler> connectHandlers_;
    std::vector<DisconnectHandler> disconnectHandlers_;
    std::vector<PacketHandler> clientboundHandlers_;
    std::vector<PacketHandler> serverboundHandlers_;
    std::vector<PacketWithDestinationHandler> clientboundDestinationHandlers_;
    std::vector<PacketWithDestinationHandler> serverboundDestinationHandlers_;

    static RelayOptions normalizeOptions(RelayOptions options) {
        if (options.version.empty() || options.version == "auto" || options.version == "latest") {
            auto vs = ProtocolDefinition::versions();
        if (!vs.empty()) options.version = vs.back();
        }
        return options;
    }

    static BedrockLiveRelayOptions toLiveOptions(const RelayOptions& options) {
        BedrockLiveRelayOptions out;
        out.server.host = options.host;
        out.server.port = options.port;
        out.server.version = options.version;
        out.server.motd = options.motd;
        out.server.maxPlayers = options.maxPlayers;
        out.server.compressionThreshold = 256;
        out.server.compressionAlgorithm = "deflate";

        out.upstream.host = options.destination.host;
        out.upstream.port = options.destination.port;
        out.upstream.version = options.version;
        out.upstream.username = options.username;
        out.upstream.profile = options.username;
        out.upstream.offline = options.destination.offline || options.offline;
        out.upstream.interactiveAuth = true;
        out.upstream.clientCacheEnabled = false;
        out.upstream.trackWorld = false;
        out.upstream.chunkRadius = 20;

        out.enableChunkCaching = false;
        out.logging = false;
        return out;
    }
};

inline Relay createRelay(RelayOptions options) {
    return Relay(std::move(options));
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
