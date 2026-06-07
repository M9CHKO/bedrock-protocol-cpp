#include <bedrock/relay/BedrockLiveRelay.hpp>

#include <bedrock/protocol/ProtocolDefinition.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace bedrock {

BedrockLiveRelay::BedrockLiveRelay(BedrockLiveRelayOptions options)
    : options_(normalizeOptions(std::move(options))),
      server_(std::make_unique<BedrockServer>(options_.server)) {}

BedrockLiveRelay::~BedrockLiveRelay() {
    close();
}

void BedrockLiveRelay::listen() {
    if (!closed_.exchange(false)) {
        return;
    }

    server_->onConnect([this](const BedrockServerConnection& connection) {
        for (auto& handler : connectHandlers_) {
            handler(connection);
        }
        emitStatus();
    });

    server_->onJoin([this](const BedrockServerConnection& connection) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            downstream_ = connection;
            downstreamJoined_.store(true);
        }

        for (auto& handler : joinHandlers_) {
            handler(connection);
        }

        std::vector<VersionedGamePacket> queuedClientbound;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queuedClientbound = std::move(pendingClientbound_);
            pendingClientbound_.clear();
        }
        for (const auto& packet : queuedClientbound) {
            forwardClientbound(packet);
        }

        emitStatus();
        startUpstream();
    });

    server_->onAny([this](const BedrockServerPacketEvent& event) {
        handleDownstreamPacket(event);
    });

    server_->listen();
    listening_.store(true);
    emitStatus();
}

int BedrockLiveRelay::run() {
    listen();
    std::unique_lock<std::mutex> lock(mutex_);
    closedCv_.wait(lock, [this]() {
        return closed_.load();
    });
    return 0;
}

void BedrockLiveRelay::close(const std::string& reason) {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) {
        return;
    }

    if (upstream_) {
        upstream_->close(reason);
    }
    if (server_) {
        server_->close();
    }
    if (upstreamThread_.joinable() &&
        upstreamThread_.get_id() != std::this_thread::get_id()) {
        upstreamThread_.join();
    }

    listening_.store(false);
    upstreamReady_.store(false);
    emitStatus();
    closedCv_.notify_all();
}

void BedrockLiveRelay::onConnect(ConnectionHandler handler) {
    connectHandlers_.push_back(std::move(handler));
}

void BedrockLiveRelay::onJoin(ConnectionHandler handler) {
    joinHandlers_.push_back(std::move(handler));
}

void BedrockLiveRelay::onClientbound(PacketHandler handler) {
    clientboundHandlers_.push_back(std::move(handler));
}

void BedrockLiveRelay::onServerbound(PacketHandler handler) {
    serverboundHandlers_.push_back(std::move(handler));
}

void BedrockLiveRelay::on(const std::string& direction, PacketHandler handler) {
    if (direction == "clientbound") {
        onClientbound(std::move(handler));
        return;
    }
    if (direction == "serverbound") {
        onServerbound(std::move(handler));
        return;
    }
    throw std::runtime_error("unknown relay direction: " + direction);
}

void BedrockLiveRelay::onError(ErrorHandler handler) {
    errorHandlers_.push_back(std::move(handler));
}

void BedrockLiveRelay::onStatus(StatusHandler handler) {
    statusHandlers_.push_back(std::move(handler));
}

bool BedrockLiveRelay::listening() const {
    return listening_.load();
}

bool BedrockLiveRelay::downstreamJoined() const {
    return downstreamJoined_.load();
}

bool BedrockLiveRelay::upstreamStarted() const {
    return upstreamStarted_.load();
}

bool BedrockLiveRelay::upstreamReady() const {
    return upstreamReady_.load();
}

uint16_t BedrockLiveRelay::boundPort() const {
    return server_ ? server_->boundPort() : 0;
}

BedrockServer& BedrockLiveRelay::server() {
    return *server_;
}

BedrockNetworkClient* BedrockLiveRelay::upstream() {
    return upstream_.get();
}

BedrockLiveRelayOptions BedrockLiveRelay::normalizeOptions(BedrockLiveRelayOptions options) {
    if (options.server.version.empty() ||
        options.server.version == "auto" ||
        options.server.version == "latest") {
        auto versions = ProtocolDefinition::versions();
        if (!versions.empty()) {
            options.server.version = versions.back();
        }
    }

    if (options.upstream.version.empty() ||
        options.upstream.version == "auto" ||
        options.upstream.version == "latest") {
        options.upstream.version = options.server.version;
    }
    if (options.upstream.version != options.server.version) {
        throw std::runtime_error("live relay currently requires matching server/upstream versions");
    }

    options.upstream.autoResourcePackResponses = true;
    options.upstream.autoInitPlayer = true;
    if (options.upstream.profile.empty()) {
        options.upstream.profile = options.upstream.username.empty()
            ? std::string("RelayBot")
            : options.upstream.username;
    }
    if (options.upstream.username.empty()) {
        options.upstream.username = options.upstream.profile;
    }
    return options;
}

bool BedrockLiveRelay::isDownstreamHandshakePacket(const std::string& name) {
    return name == "request_network_settings" ||
        name == "login" ||
        name == "client_to_server_handshake" ||
        name == "resource_pack_client_response" ||
        name == "client_cache_status" ||
        name == "request_chunk_radius" ||
        name == "set_local_player_as_initialized";
}

bool BedrockLiveRelay::isClientboundResourcePackPacket(const std::string& name) {
    return name == "resource_packs_info" ||
        name == "resource_pack_stack" ||
        name == "resource_pack_data_info" ||
        name == "resource_pack_chunk_data";
}

bool BedrockLiveRelay::isPlayStatusLoginSuccess(const VersionedGamePacket& packet) {
    return packet.name == "play_status" && !packet.payload.empty() && packet.payload[0] == 0x00;
}

void BedrockLiveRelay::emitError(const std::string& message) {
    for (auto& handler : errorHandlers_) {
        handler(message);
    }
}

void BedrockLiveRelay::emitStatus() {
    BedrockLiveRelayStatus status;
    status.listening = listening_.load();
    status.downstreamJoined = downstreamJoined_.load();
    status.upstreamStarted = upstreamStarted_.load();
    status.upstreamReady = upstreamReady_.load();
    status.boundPort = boundPort();

    for (auto& handler : statusHandlers_) {
        handler(status);
    }
}

void BedrockLiveRelay::startUpstream() {
    bool expected = false;
    if (!upstreamStarted_.compare_exchange_strong(expected, true)) {
        return;
    }

    upstream_ = std::make_unique<BedrockNetworkClient>(options_.upstream);

    upstream_->onError([this](const std::string& message) {
        emitError("[upstream] " + message);
    });

    upstream_->onClose([this](const std::string& reason) {
        if (!closed_.load()) {
            emitError("[upstream closed] " + reason);
        }
        upstreamReady_.store(false);
        emitStatus();
    });

    upstream_->onAny([this](const BedrockNetworkClientPacketEvent& event) {
        handleUpstreamPacket(event.packet);
    });

    upstream_->onJoin([this]() {
        upstreamReady_.store(true);
        emitStatus();

        std::vector<VersionedGamePacket> queued;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queued = std::move(pendingServerbound_);
            pendingServerbound_.clear();
        }
        for (const auto& packet : queued) {
            forwardServerbound(packet);
        }
    });

    upstreamThread_ = std::thread([this]() {
        upstream_->run();
    });
    emitStatus();
}

void BedrockLiveRelay::handleUpstreamPacket(const VersionedGamePacket& packet) {
    if (options_.skipClientboundLoginSuccess && isPlayStatusLoginSuccess(packet)) {
        return;
    }

    if (options_.skipClientboundResourcePacks &&
        isClientboundResourcePackPacket(packet.name)) {
        return;
    }

    if (!options_.forwardClientbound) {
        return;
    }

    auto packets = applyHandlers(BedrockRelayDirection::Clientbound, packet);
    for (const auto& candidate : packets) {
        forwardClientbound(candidate);
    }
}

void BedrockLiveRelay::handleDownstreamPacket(const BedrockServerPacketEvent& event) {
    if (options_.filterDownstreamHandshakePackets &&
        isDownstreamHandshakePacket(event.packet.name)) {
        return;
    }

    if (!options_.forwardServerbound) {
        return;
    }

    auto packets = applyHandlers(BedrockRelayDirection::Serverbound, event.packet);
    for (const auto& candidate : packets) {
        if (!upstream_ || !upstreamReady_.load()) {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingServerbound_.push_back(candidate);
            continue;
        }
        forwardServerbound(candidate);
    }
}

void BedrockLiveRelay::forwardClientbound(const VersionedGamePacket& packet) {
    std::optional<BedrockServerConnection> downstream;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        downstream = downstream_;
        if (!downstream.has_value()) {
            pendingClientbound_.push_back(packet);
            return;
        }
    }
    server_->sendPacket(*downstream, packet, options_.clientboundCompression);
}

void BedrockLiveRelay::forwardServerbound(const VersionedGamePacket& packet) {
    if (!upstream_ || !upstreamReady_.load()) {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingServerbound_.push_back(packet);
        return;
    }
    upstream_->sendPacket(packet);
}

std::vector<VersionedGamePacket> BedrockLiveRelay::applyHandlers(
    BedrockRelayDirection direction,
    const VersionedGamePacket& packet
) {
    BedrockRelayPacketEvent event;
    event.direction = direction;
    event.packet = packet;

    auto& handlers = direction == BedrockRelayDirection::Clientbound
        ? clientboundHandlers_
        : serverboundHandlers_;

    for (auto& handler : handlers) {
        handler(event);
    }

    if (event.canceled) {
        return {};
    }
    if (!event.replacements.empty()) {
        return std::move(event.replacements);
    }
    return { std::move(event.packet) };
}

} // namespace bedrock
