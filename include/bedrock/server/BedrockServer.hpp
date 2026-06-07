#pragma once

#include <bedrock/BedrockEncryption.hpp>
#include <bedrock/BedrockKeyExchange.hpp>
#include <bedrock/LoginPacket.hpp>
#include <bedrock/auth/BedrockAuthJwt.hpp>
#include <bedrock/protocol/ProtocolDefinition.hpp>
#include <bedrock/protocol/VersionedMcpeCodec.hpp>
#include <bedrock/protodef/ProtoDefPacketEncoder.hpp>
#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/server/RakNetServer.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bedrock {

struct BedrockServerOptions {
    std::string host = "0.0.0.0";
    uint16_t port = 19132;
    std::string version = "latest";
    std::string motd = "Bedrock Protocol C++";
    int maxPlayers = 3;
};

struct BedrockServerConnection {
    std::string address;
    uint16_t port = 0;
    uint64_t clientGuid = 0;
    int mtu = 1400;
    RakNetServerPeer peer;
};

struct BedrockServerPacketEvent {
    BedrockServerConnection connection;
    VersionedGamePacket packet;
};

class BedrockServer {
public:
    using ConnectionHandler = std::function<void(const BedrockServerConnection&)>;
    using PacketHandler = std::function<void(const BedrockServerPacketEvent&)>;

    explicit BedrockServer(BedrockServerOptions options = {})
        : options_(normalizeOptions(std::move(options))),
          raknet_(makeRakNetOptions(options_)),
          mcpeCodec_(VersionedMcpeCodec::forVersion(options_.version)) {}

    void onConnect(ConnectionHandler handler) {
        connectHandlers_.push_back(std::move(handler));
    }

    void onJoin(ConnectionHandler handler) {
        joinHandlers_.push_back(std::move(handler));
    }

    void onAny(PacketHandler handler) {
        anyPacketHandlers_.push_back(std::move(handler));
    }

    void on(const std::string& packetName, PacketHandler handler) {
        packetHandlers_[packetName].push_back(std::move(handler));
    }

    void listen() {
        raknet_.onOpenConnection([this](const RakNetServerPeer& peer) {
            BedrockServerConnection connection;
            connection.address = peer.address;
            connection.port = peer.port;
            connection.clientGuid = peer.clientGuid;
            connection.mtu = peer.mtu;
            connection.peer = peer;

            connections_[connectionKey(peer)] = connection;

            for (auto& handler : connectHandlers_) {
                handler(connection);
            }
        });
        raknet_.onEncapsulated([this](const RakNetServerPeer& peer, const std::vector<uint8_t>& payload) {
            handleEncapsulated(peer, payload);
        });
        raknet_.listen();
    }

    void close() {
        raknet_.close();
    }

    bool listening() const {
        return raknet_.listening();
    }

    uint16_t boundPort() const {
        return raknet_.boundPort();
    }

    const BedrockServerOptions& options() const {
        return options_;
    }

    void send(
        const BedrockServerConnection& connection,
        const std::string& packetName,
        const ProtoDefValue& value,
        VersionedMcpeCompression compression = VersionedMcpeCompression::Uncompressed
    ) {
        ProtoDefPacketEncoder encoder(options_.version);
        auto payload = encoder.encodePacket(packetName, value);
        sendPacket(
            connection,
            mcpeCodec_.packetCodec().makePacketByName(packetName, payload),
            compression
        );
    }

    void sendPacket(
        const BedrockServerConnection& connection,
        const VersionedGamePacket& packet,
        VersionedMcpeCompression compression = VersionedMcpeCompression::Uncompressed
    ) {
        auto& session = sessions_[connectionKey(connection.peer)];
        if (session.encryptionEnabled && session.hasEncryptionKeys) {
            auto compressionPacket = mcpeCodec_.encodeCompressionPacket({packet}, compression);
            auto encrypted = BedrockEncryption::encryptMcpePayloadGcm(
                compressionPacket,
                session.sendCounter++,
                session.encryptionKeys.secretKeyBytes,
                session.encryptionKeys.iv16
            );
            raknet_.sendReliable(connection.peer, encrypted);
            return;
        }

        auto mcpe = mcpeCodec_.encodeMcpePayload({packet}, compression);
        raknet_.sendReliable(connection.peer, mcpe);
    }

    void disconnect(
        const BedrockServerConnection& connection,
        const std::string& reason = "Server closed",
        bool hide = false
    ) {
        if (!mcpeCodec_.definition().hasPacket("disconnect")) {
            return;
        }

        send(connection, "disconnect", ProtoDefValue::object({
            {"reason", ProtoDefValue::string("unknown")},
            {"hide_disconnect_reason", ProtoDefValue::boolean(hide)},
            {"message", ProtoDefValue::string(reason)},
            {"filtered_message", ProtoDefValue::string("")}
        }));
    }

private:
    BedrockServerOptions options_;
    RakNetServer raknet_;
    VersionedMcpeCodec mcpeCodec_;
    std::vector<ConnectionHandler> connectHandlers_;
    std::vector<ConnectionHandler> joinHandlers_;
    std::vector<PacketHandler> anyPacketHandlers_;
    std::unordered_map<std::string, std::vector<PacketHandler>> packetHandlers_;
    std::unordered_map<std::string, BedrockServerConnection> connections_;

    static BedrockServerOptions normalizeOptions(BedrockServerOptions options) {
        if (options.version.empty() || options.version == "auto" || options.version == "latest") {
            auto versions = ProtocolDefinition::versions();
            if (!versions.empty()) {
                options.version = versions.back();
            }
        }

        if (!ProtocolDefinition::supportsVersion(options.version)) {
            throw std::runtime_error("unsupported Bedrock server version: " + options.version);
        }

        return options;
    }

    static RakNetServerOptions makeRakNetOptions(const BedrockServerOptions& options) {
        RakNetServerOptions raknet;
        raknet.host = options.host;
        raknet.port = options.port;
        raknet.maxPlayers = options.maxPlayers;
        raknet.protocolVersion = protocolVersionForMinecraft(options.version) >= 589 ? 11 : 10;
        raknet.advertisement = buildAdvertisement(options);
        return raknet;
    }

    static int protocolVersionForMinecraft(const std::string& version) {
        return static_cast<int>(ProtocolDefinition::forVersion(version).protocolVersion());
    }

    static std::string buildAdvertisement(const BedrockServerOptions& options) {
        const int protocol = protocolVersionForMinecraft(options.version);
        return "MCPE;" + options.motd + ";" +
            std::to_string(protocol) + ";" +
            options.version + ";0;" +
            std::to_string(options.maxPlayers) +
            ";0;" + options.motd + ";Survival;1;" +
            std::to_string(options.port) + ";" +
            std::to_string(options.port) + ";";
    }

    static std::string connectionKey(const RakNetServerPeer& peer) {
        return peer.address + ":" + std::to_string(peer.port);
    }

    struct SessionState {
        BedrockClientKeyPair serverKeys;
        std::vector<uint8_t> salt;
        std::string clientPublicKeyDerBase64;
        DerivedKeyResult encryptionKeys;
        bool hasEncryptionKeys = false;
        bool encryptionEnabled = false;
        uint64_t sendCounter = 0;
        uint64_t receiveCounter = 0;
    };

    std::unordered_map<std::string, SessionState> sessions_;

    void handleEncapsulated(const RakNetServerPeer& peer, const std::vector<uint8_t>& payload) {
        BedrockServerConnection connection;
        auto key = connectionKey(peer);
        auto it = connections_.find(key);
        if (it != connections_.end()) {
            connection = it->second;
        } else {
            connection.address = peer.address;
            connection.port = peer.port;
            connection.clientGuid = peer.clientGuid;
            connection.mtu = peer.mtu;
            connection.peer = peer;
            connections_[key] = connection;
        }

        if (payload.empty() || payload[0] != 0xfe) {
            return;
        }

        auto& session = sessions_[key];
        VersionedMcpePayload decoded;
        if (session.encryptionEnabled && session.hasEncryptionKeys) {
            auto compressionPacket = BedrockEncryption::decryptMcpePayloadGcm(
                payload,
                session.receiveCounter++,
                session.encryptionKeys.secretKeyBytes,
                session.encryptionKeys.iv16
            );
            decoded = mcpeCodec_.decodeCompressionPacket(compressionPacket);
        } else {
            decoded = mcpeCodec_.decodeMcpePayload(payload);
        }

        for (const auto& packet : decoded.batch.packets) {
            BedrockServerPacketEvent event;
            event.connection = connection;
            event.packet = packet;

            for (auto& handler : anyPacketHandlers_) {
                handler(event);
            }

            auto nameIt = packetHandlers_.find(packet.name);
            if (nameIt != packetHandlers_.end()) {
                for (auto& handler : nameIt->second) {
                    handler(event);
                }
            }

            handleBuiltInPacket(connection, packet);
        }
    }

    void handleBuiltInPacket(
        const BedrockServerConnection& connection,
        const VersionedGamePacket& packet
    ) {
        if (packet.name == "request_network_settings" && mcpeCodec_.definition().hasPacket("network_settings")) {
            send(connection, "network_settings", ProtoDefValue::object({
                {"compression_threshold", ProtoDefValue::uinteger(256)},
                {"compression_algorithm", ProtoDefValue::string("deflate")},
                {"client_throttle", ProtoDefValue::boolean(false)},
                {"client_throttle_threshold", ProtoDefValue::uinteger(0)},
                {"client_throttle_scalar", ProtoDefValue::floating(0.0)}
            }));
            return;
        }

        if (packet.name == "login" && mcpeCodec_.definition().hasPacket("server_to_client_handshake")) {
            handleLogin(connection, packet);
            return;
        }

        if (packet.name == "client_to_server_handshake" && mcpeCodec_.definition().hasPacket("play_status")) {
            send(connection, "play_status", ProtoDefValue::object({
                {"status", ProtoDefValue::string("login_success")}
            }));
            for (auto& handler : joinHandlers_) {
                handler(connection);
            }
        }
    }

    void handleLogin(
        const BedrockServerConnection& connection,
        const VersionedGamePacket& packet
    ) {
        auto login = LoginPacketCodec::decode(packet.fullPacket);
        auto clientPublicKey = extractClientPublicKey(login.identity);
        if (!clientPublicKey.has_value()) {
            return;
        }

        auto& session = sessions_[connectionKey(connection.peer)];
        if (session.serverKeys.privateKeyPem.empty()) {
            session.serverKeys = BedrockAuthJwt::generateP384KeyPair();
        }
        if (session.salt.empty()) {
            session.salt = {0xf0, 0x9f, 0xa7, 0x82};
        }
        session.clientPublicKeyDerBase64 = *clientPublicKey;
        session.encryptionKeys = BedrockKeyExchange::deriveFromRemotePublicKeyDerBase64AndPrivateKeyPem(
            session.clientPublicKeyDerBase64,
            session.serverKeys.privateKeyPem,
            session.salt
        );
        session.hasEncryptionKeys = true;

        const std::string payloadJson =
            "{\"salt\":\"" + BedrockAuthJwt::base64(session.salt) +
            "\",\"signedToken\":\"" + session.serverKeys.publicKeyDerBase64 + "\"}";

        auto token = BedrockAuthJwt::signEs384Jwt(
            session.serverKeys.privateKeyPem,
            session.serverKeys.publicKeyDerBase64,
            payloadJson
        );

        send(connection, "server_to_client_handshake", ProtoDefValue::object({
            {"token", ProtoDefValue::string(token)}
        }));

        session.encryptionEnabled = true;
        session.sendCounter = 0;
        session.receiveCounter = 0;
    }

    static std::optional<std::string> extractClientPublicKey(const std::string& identityJson) {
        auto chain = extractChain(identityJson);
        std::optional<std::string> finalKey;

        for (const auto& token : chain) {
            try {
                auto payload = BedrockKeyExchange::extractJwtPayloadJson(token);
                auto key = BedrockKeyExchange::jsonExtractString(payload, "identityPublicKey");
                if (!key.empty()) {
                    finalKey = key;
                }
            } catch (const std::exception&) {
            }
        }

        return finalKey;
    }

    static std::vector<std::string> extractChain(const std::string& identityJson) {
        std::string chainJson = identityJson;
        try {
            chainJson = BedrockKeyExchange::jsonExtractString(identityJson, "Certificate");
        } catch (const std::exception&) {
        }

        auto chainPos = chainJson.find("\"chain\"");
        if (chainPos == std::string::npos) {
            return {};
        }

        auto arrayStart = chainJson.find('[', chainPos);
        if (arrayStart == std::string::npos) {
            return {};
        }

        std::vector<std::string> out;
        for (std::size_t pos = arrayStart + 1; pos < chainJson.size();) {
            while (pos < chainJson.size() && (chainJson[pos] == ' ' || chainJson[pos] == '\n' || chainJson[pos] == '\r' || chainJson[pos] == '\t' || chainJson[pos] == ',')) {
                ++pos;
            }
            if (pos >= chainJson.size() || chainJson[pos] == ']') {
                break;
            }
            if (chainJson[pos] != '"') {
                break;
            }
            ++pos;

            std::string value;
            bool escaped = false;
            for (; pos < chainJson.size(); ++pos) {
                char c = chainJson[pos];
                if (escaped) {
                    switch (c) {
                        case '"': value.push_back('"'); break;
                        case '\\': value.push_back('\\'); break;
                        case '/': value.push_back('/'); break;
                        case 'b': value.push_back('\b'); break;
                        case 'f': value.push_back('\f'); break;
                        case 'n': value.push_back('\n'); break;
                        case 'r': value.push_back('\r'); break;
                        case 't': value.push_back('\t'); break;
                        default: value.push_back(c); break;
                    }
                    escaped = false;
                    continue;
                }
                if (c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == '"') {
                    ++pos;
                    break;
                }
                value.push_back(c);
            }
            out.push_back(std::move(value));
        }

        return out;
    }
};

inline BedrockServer createServer(BedrockServerOptions options = {}) {
    return BedrockServer(std::move(options));
}

} // namespace bedrock
