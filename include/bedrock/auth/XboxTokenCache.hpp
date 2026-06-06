#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace bedrock {

struct XboxTokenCacheData {
    std::string profileName;
    std::string gamertag;
    std::string xuid;

    std::string microsoftAccessToken;
    std::string microsoftRefreshToken;
    std::string microsoftExpiresAt;

    std::string xboxUserToken;
    std::string xstsToken;
    std::string xstsUserHash;
    std::string xstsExpiresAt;

    std::string minecraftAccessToken;
    std::string minecraftExpiresAt;
    std::string minecraftUuid;
    std::string minecraftName;
};

class XboxTokenCacheFile {
public:
    static XboxTokenCacheData load(const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("failed to read xbox token cache: " + path.string());
        }

        std::string json(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        XboxTokenCacheData data;
        data.profileName = readString(json, "profileName");
        data.gamertag = readString(json, "gamertag");
        data.xuid = readString(json, "xuid");
        data.microsoftAccessToken = readString(json, "microsoftAccessToken");
        data.microsoftRefreshToken = readString(json, "microsoftRefreshToken");
        data.microsoftExpiresAt = readString(json, "microsoftExpiresAt");
        data.xboxUserToken = readString(json, "xboxUserToken");
        data.xstsToken = readString(json, "xstsToken");
        data.xstsUserHash = readString(json, "xstsUserHash");
        data.xstsExpiresAt = readString(json, "xstsExpiresAt");
        data.minecraftAccessToken = readString(json, "minecraftAccessToken");
        data.minecraftExpiresAt = readString(json, "minecraftExpiresAt");
        data.minecraftUuid = readString(json, "minecraftUuid");
        data.minecraftName = readString(json, "minecraftName");
        return data;
    }

    static void save(const std::filesystem::path& path, const XboxTokenCacheData& data) {
        std::filesystem::create_directories(path.parent_path());

        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("failed to write xbox token cache: " + path.string());
        }

        out << "{\n";
        write(out, "profileName", data.profileName, true);
        write(out, "gamertag", data.gamertag, true);
        write(out, "xuid", data.xuid, true);
        write(out, "microsoftAccessToken", data.microsoftAccessToken, true);
        write(out, "microsoftRefreshToken", data.microsoftRefreshToken, true);
        write(out, "microsoftExpiresAt", data.microsoftExpiresAt, true);
        write(out, "xboxUserToken", data.xboxUserToken, true);
        write(out, "xstsToken", data.xstsToken, true);
        write(out, "xstsUserHash", data.xstsUserHash, true);
        write(out, "xstsExpiresAt", data.xstsExpiresAt, true);
        write(out, "minecraftAccessToken", data.minecraftAccessToken, true);
        write(out, "minecraftExpiresAt", data.minecraftExpiresAt, true);
        write(out, "minecraftUuid", data.minecraftUuid, true);
        write(out, "minecraftName", data.minecraftName, false);
        out << "}\n";
    }

private:
    static void write(std::ofstream& out, const std::string& key, const std::string& value, bool comma) {
        out << "  \"" << key << "\": \"" << escape(value) << "\"";
        if (comma) out << ",";
        out << "\n";
    }

    static std::string readString(const std::string& json, const std::string& key) {
        const std::string marker = "\"" + key + "\"";
        auto p = json.find(marker);
        if (p == std::string::npos) return "";

        p = json.find(':', p);
        if (p == std::string::npos) return "";

        p = json.find('"', p);
        if (p == std::string::npos) return "";

        std::string out;
        bool escaped = false;

        for (++p; p < json.size(); ++p) {
            char c = json[p];

            if (escaped) {
                out.push_back(c);
                escaped = false;
                continue;
            }

            if (c == '\\') {
                escaped = true;
                continue;
            }

            if (c == '"') {
                break;
            }

            out.push_back(c);
        }

        return out;
    }

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    }
};

} // namespace bedrock
