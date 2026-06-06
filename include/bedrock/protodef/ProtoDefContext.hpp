#pragma once

#include <string>
#include <unordered_map>

namespace bedrock {

class ProtoDefContext {
public:
    void set(
        const std::string& key,
        const std::string& value
    ) {
        values_[key] = value;

        // Convenience aliases for nested protodef references.
        // Example: recipes[0].recipe.width is also available as width.
        // This keeps nested count refs like {count:"width"} working even
        // when generated schemas omit the full relative path.
        auto dot = key.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < key.size()) {
            std::string shortKey = key.substr(dot + 1);
            if (!shortKey.empty()) {
                values_[shortKey] = value;
            }
        }
    }

    bool has(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    std::string get(
        const std::string& key,
        const std::string& defaultValue = ""
    ) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return defaultValue;
        }
        return it->second;
    }

private:
    std::unordered_map<std::string,std::string> values_;
};

}
