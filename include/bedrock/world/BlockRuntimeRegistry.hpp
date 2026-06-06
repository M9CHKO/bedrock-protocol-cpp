#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace bedrock {

class BlockRuntimeRegistry {
public:
    void add(uint32_t runtimeId, std::string name) {
        idToName_[runtimeId] = name;
        nameToId_[std::move(name)] = runtimeId;
    }

    bool hasRuntimeId(uint32_t runtimeId) const {
        return idToName_.find(runtimeId) != idToName_.end();
    }

    bool hasName(const std::string& name) const {
        return nameToId_.find(name) != nameToId_.end();
    }

    std::optional<std::string> nameOf(uint32_t runtimeId) const {
        auto it = idToName_.find(runtimeId);
        if (it == idToName_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<uint32_t> runtimeIdOf(const std::string& name) const {
        auto it = nameToId_.find(name);
        if (it == nameToId_.end()) return std::nullopt;
        return it->second;
    }

    std::size_t size() const {
        return idToName_.size();
    }

private:
    std::unordered_map<uint32_t, std::string> idToName_;
    std::unordered_map<std::string, uint32_t> nameToId_;
};

} // namespace bedrock
