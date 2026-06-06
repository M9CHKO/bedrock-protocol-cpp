#pragma once

#include <bedrock/world/BlockRuntimeRegistryLoader.hpp>
#include <bedrock/world/MinecraftDataIndex.hpp>
#include <bedrock/world/MinecraftDataPathResolver.hpp>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace bedrock {

struct MinecraftDataAssetsPaths {
    MinecraftDataVersionInfo version;

    std::string blocksDirectory;
    std::string itemsDirectory;
    std::string protocolDirectory;
    std::string typesDirectory;
    std::string biomesDirectory;
    std::string entitiesDirectory;
    std::string languageDirectory;

    std::filesystem::path versionDir;
    std::filesystem::path protocolJson;
    std::filesystem::path blocksJson;
    std::filesystem::path itemsJson;
    std::filesystem::path blockRuntimeTsv;
};

class MinecraftDataAssets {
public:
    explicit MinecraftDataAssets(
        std::filesystem::path bedrockDataPath = "data/minecraft-data/bedrock",
        std::filesystem::path generatedRuntimePath = "data/generated/block-runtime/bedrock",
        std::filesystem::path dataPathsPath = "data/minecraft-data/dataPaths.json"
    )
        : index_(std::move(bedrockDataPath)),
          generatedRuntimePath_(std::move(generatedRuntimePath)),
          dataPathsPath_(std::move(dataPathsPath)) {}

    MinecraftDataAssetsPaths resolveByVersion(const std::string& version) const {
        auto info = index_.findByMinecraftVersion(version);
        if (!info.has_value()) {
            throw std::runtime_error("minecraft-data version not found: " + version);
        }

        return makePaths(*info);
    }

    MinecraftDataAssetsPaths resolveByProtocol(int32_t protocol) const {
        auto info = index_.findByProtocol(protocol);
        if (!info.has_value()) {
            throw std::runtime_error("minecraft-data protocol not found: " + std::to_string(protocol));
        }

        return makePaths(*info);
    }

    BlockRuntimeRegistry loadBlockRuntimeRegistryByVersion(const std::string& version) const {
        auto paths = resolveByVersion(version);
        return BlockRuntimeRegistryLoader::loadTsv(paths.blockRuntimeTsv.string());
    }

    BlockRuntimeRegistry loadBlockRuntimeRegistryByProtocol(int32_t protocol) const {
        auto paths = resolveByProtocol(protocol);
        return BlockRuntimeRegistryLoader::loadTsv(paths.blockRuntimeTsv.string());
    }

private:
    MinecraftDataIndex index_;
    std::filesystem::path generatedRuntimePath_;
    std::filesystem::path dataPathsPath_;

    MinecraftDataAssetsPaths makePaths(const MinecraftDataVersionInfo& info) const {
        MinecraftDataAssetsPaths paths;
        paths.version = info;

        MinecraftDataPathSet remap;

        if (std::filesystem::exists(dataPathsPath_)) {
            MinecraftDataPathResolver resolver(dataPathsPath_);
            auto found = resolver.findBedrock(info.directory);
            if (found.has_value()) {
                remap = *found;
            } else {
                remap.version = info.directory;
                remap.blocks = info.directory;
                remap.items = info.directory;
                remap.protocol = info.directory;
                remap.types = info.directory;
                remap.biomes = info.directory;
                remap.entities = info.directory;
                remap.language = info.directory;
            }
        } else {
            remap.version = info.directory;
            remap.blocks = info.directory;
            remap.items = info.directory;
            remap.protocol = info.directory;
            remap.types = info.directory;
            remap.biomes = info.directory;
            remap.entities = info.directory;
            remap.language = info.directory;
        }

        paths.blocksDirectory = remap.blocks;
        paths.itemsDirectory = remap.items;
        paths.protocolDirectory = remap.protocol;
        paths.typesDirectory = remap.types;
        paths.biomesDirectory = remap.biomes;
        paths.entitiesDirectory = remap.entities;
        paths.languageDirectory = remap.language;

        paths.versionDir = index_.versionPath(info.directory);
        paths.protocolJson = index_.protocolPath(remap.protocol);
        paths.blocksJson = index_.blocksPath(remap.blocks);
        paths.itemsJson = index_.itemsPath(remap.items);
        paths.blockRuntimeTsv = generatedRuntimePath_ / (remap.blocks + ".tsv");

        if (!std::filesystem::exists(paths.versionDir)) {
            throw std::runtime_error("minecraft-data version dir missing: " + paths.versionDir.string());
        }

        if (!std::filesystem::exists(paths.protocolJson)) {
            throw std::runtime_error("minecraft-data protocol.json missing: " + paths.protocolJson.string());
        }

        if (!std::filesystem::exists(paths.blocksJson)) {
            throw std::runtime_error("minecraft-data blocks.json missing: " + paths.blocksJson.string());
        }

        if (!std::filesystem::exists(paths.itemsJson)) {
            throw std::runtime_error("minecraft-data items.json missing: " + paths.itemsJson.string());
        }

        if (!std::filesystem::exists(paths.blockRuntimeTsv)) {
            throw std::runtime_error("generated block runtime tsv missing: " + paths.blockRuntimeTsv.string());
        }

        return paths;
    }
};

} // namespace bedrock
