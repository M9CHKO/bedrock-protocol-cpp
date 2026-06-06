#pragma once

#include <bedrock/LoginPacket.hpp>

#include <cstdint>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

struct XboxAuthCache {
    uint32_t protocolVersion = 0;
    std::string minecraftVersion;
    std::vector<std::string> chain;
    std::string clientJwt;

    std::string identityJsonLegacy() const {
        std::string out = "{\"chain\":[";
        for (std::size_t i = 0; i < chain.size(); ++i) {
            if (i) out += ",";
            out += "\"";
            out += chain[i];
            out += "\"";
        }
        out += "]}";
        return out;
    }

    std::vector<uint8_t> makeLoginPacket() const {
        if (chain.empty()) {
            throw std::runtime_error("XboxAuthCache chain is empty");
        }
        if (clientJwt.empty()) {
            throw std::runtime_error("XboxAuthCache clientJwt is empty");
        }

        return LoginPacketCodec::encode(
            protocolVersion,
            identityJsonLegacy(),
            clientJwt
        );
    }
};

class XboxAuthCacheLoader {
public:
    static XboxAuthCache loadSimpleJson(const std::string& path) {
        std::ifstream f(path);
        if (!f) {
            throw std::runtime_error("failed to open xbox auth cache: " + path);
        }

        std::string json{
            std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()
        };

        XboxAuthCache out;
        out.protocolVersion = static_cast<uint32_t>(readInt(json, "protocolVersion", 0));
        out.minecraftVersion = readString(json, "minecraftVersion", "");
        out.clientJwt = readString(json, "clientJwt", "");
        out.chain = readStringArray(json, "chain");

        return out;
    }

private:
    static std::string readString(
        const std::string& json,
        const std::string& key,
        const std::string& fallback
    ) {
        const auto marker = "\"" + key + "\"";
        auto pos = json.find(marker);
        if (pos == std::string::npos) return fallback;

        pos = json.find(':', pos);
        if (pos == std::string::npos) return fallback;

        pos = json.find('"', pos);
        if (pos == std::string::npos) return fallback;

        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return fallback;

        return json.substr(pos + 1, end - pos - 1);
    }

    static int readInt(
        const std::string& json,
        const std::string& key,
        int fallback
    ) {
        const auto marker = "\"" + key + "\"";
        auto pos = json.find(marker);
        if (pos == std::string::npos) return fallback;

        pos = json.find(':', pos);
        if (pos == std::string::npos) return fallback;
        ++pos;

        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }

        auto end = pos;
        while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
            ++end;
        }

        if (end == pos) return fallback;
        return std::stoi(json.substr(pos, end - pos));
    }

    static std::vector<std::string> readStringArray(
        const std::string& json,
        const std::string& key
    ) {
        std::vector<std::string> out;

        const auto marker = "\"" + key + "\"";
        auto pos = json.find(marker);
        if (pos == std::string::npos) return out;

        pos = json.find('[', pos);
        if (pos == std::string::npos) return out;

        auto endArray = json.find(']', pos);
        if (endArray == std::string::npos) return out;

        while (true) {
            auto q1 = json.find('"', pos);
            if (q1 == std::string::npos || q1 > endArray) break;

            auto q2 = json.find('"', q1 + 1);
            if (q2 == std::string::npos || q2 > endArray) break;

            out.push_back(json.substr(q1 + 1, q2 - q1 - 1));
            pos = q2 + 1;
        }

        return out;
    }
};

} // namespace bedrock
