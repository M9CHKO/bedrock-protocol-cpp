#include <bedrock/protocol/TypedPacketDispatcher.hpp>

#include <utility>

namespace bedrock {

void TypedPacketDispatcher::onAny(PacketHandler handler) {
    anyHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::on(uint32_t packetId, PacketHandler handler) {
    idHandlers_[packetId].push_back(std::move(handler));
}

void TypedPacketDispatcher::on(const std::string& packetName, PacketHandler handler) {
    nameHandlers_[packetName].push_back(std::move(handler));
}

void TypedPacketDispatcher::onError(ErrorHandler handler) {
    errorHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::onPlayStatus(PlayStatusHandler handler) {
    playStatusHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::onText(TextHandler handler) {
    textHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::onLevelChunk(LevelChunkHandler handler) {
    levelChunkHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::onStartGame(StartGameHandler handler) {
    startGameHandlers_.push_back(std::move(handler));
}

void TypedPacketDispatcher::emitErrorOrThrow(const GamePacket& packet, const std::exception& err) {
    if (errorHandlers_.empty()) {
        throw;
    }

    for (const auto& handler : errorHandlers_) {
        handler(packet, err);
    }
}

bool TypedPacketDispatcher::handle(const GamePacket& packet) {
    bool handled = false;

    for (const auto& handler : anyHandlers_) {
        handler(packet);
        handled = true;
    }

    auto idIt = idHandlers_.find(packet.packetId);
    if (idIt != idHandlers_.end()) {
        for (const auto& handler : idIt->second) {
            handler(packet);
            handled = true;
        }
    }

    if (!packet.name.empty()) {
        auto nameIt = nameHandlers_.find(packet.name);
        if (nameIt != nameHandlers_.end()) {
            for (const auto& handler : nameIt->second) {
                handler(packet);
                handled = true;
            }
        }
    }

    try {
        switch (packet.packetId) {
            case 2: { // play_status
                if (!playStatusHandlers_.empty()) {
                    auto decoded = PacketPayloadReader::readPlayStatus(packet);
                    for (const auto& handler : playStatusHandlers_) {
                        handler(decoded);
                    }
                    handled = true;
                }
                break;
            }

            case 9: { // text
                if (!textHandlers_.empty()) {
                    auto decoded = PacketPayloadReader::readText(packet);
                    for (const auto& handler : textHandlers_) {
                        handler(decoded);
                    }
                    handled = true;
                }
                break;
            }

            case 11: { // start_game
                if (!startGameHandlers_.empty()) {
                    auto decoded = PacketPayloadReader::readStartGame(packet);
                    for (const auto& handler : startGameHandlers_) {
                        handler(decoded);
                    }
                    handled = true;
                }
                break;
            }

            case 58: { // level_chunk
                if (!levelChunkHandlers_.empty()) {
                    auto decoded = PacketPayloadReader::readLevelChunk(packet);
                    for (const auto& handler : levelChunkHandlers_) {
                        handler(decoded);
                    }
                    handled = true;
                }
                break;
            }

            default:
                break;
        }
    } catch (const std::exception& err) {
        emitErrorOrThrow(packet, err);
        handled = true;
    }

    return handled;
}

void TypedPacketDispatcher::handleMany(const std::vector<GamePacket>& packets) {
    for (const auto& packet : packets) {
        handle(packet);
    }
}

} // namespace bedrock
