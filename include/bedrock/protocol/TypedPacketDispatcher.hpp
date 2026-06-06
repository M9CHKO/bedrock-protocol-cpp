#pragma once

#include <bedrock/protocol/PacketIO.hpp>
#if __has_include(<bedrock/protocol/PacketPayloadReader.hpp>)
#include <bedrock/protocol/PacketPayloadReader.hpp>
#elif __has_include(<bedrock/PacketPayloadReader.hpp>)
#include <bedrock/PacketPayloadReader.hpp>
#endif

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bedrock {

class TypedPacketDispatcher {
public:
    using PacketHandler = std::function<void(const GamePacket&)>;
    using ErrorHandler = std::function<void(const GamePacket&, const std::exception&)>;

    using PlayStatus = decltype(PacketPayloadReader::readPlayStatus(std::declval<GamePacket>()));
    using Text = decltype(PacketPayloadReader::readText(std::declval<GamePacket>()));
    using LevelChunk = decltype(PacketPayloadReader::readLevelChunk(std::declval<GamePacket>()));
    using StartGame = decltype(PacketPayloadReader::readStartGame(std::declval<GamePacket>()));

    using PlayStatusHandler = std::function<void(const PlayStatus&)>;
    using TextHandler = std::function<void(const Text&)>;
    using LevelChunkHandler = std::function<void(const LevelChunk&)>;
    using StartGameHandler = std::function<void(const StartGame&)>;

    void onAny(PacketHandler handler);
    void on(uint32_t packetId, PacketHandler handler);
    void on(const std::string& packetName, PacketHandler handler);
    void onError(ErrorHandler handler);

    void onPlayStatus(PlayStatusHandler handler);
    void onText(TextHandler handler);
    void onLevelChunk(LevelChunkHandler handler);
    void onStartGame(StartGameHandler handler);

    bool handle(const GamePacket& packet);
    void handleMany(const std::vector<GamePacket>& packets);

private:
    std::vector<PacketHandler> anyHandlers_;
    std::unordered_map<uint32_t, std::vector<PacketHandler>> idHandlers_;
    std::unordered_map<std::string, std::vector<PacketHandler>> nameHandlers_;
    std::vector<ErrorHandler> errorHandlers_;

    std::vector<PlayStatusHandler> playStatusHandlers_;
    std::vector<TextHandler> textHandlers_;
    std::vector<LevelChunkHandler> levelChunkHandlers_;
    std::vector<StartGameHandler> startGameHandlers_;

    void emitErrorOrThrow(const GamePacket& packet, const std::exception& err);
};

} // namespace bedrock
