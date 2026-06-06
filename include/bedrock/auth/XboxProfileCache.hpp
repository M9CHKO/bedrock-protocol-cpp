#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace bedrock {

struct XboxProfileCachePaths {
    std::string profileName;
    std::string version;
    std::filesystem::path root;
    std::filesystem::path profileDir;
    std::filesystem::path versionDir;
    std::filesystem::path authJson;
    std::filesystem::path loginPacketBin;
    std::filesystem::path identityUuidTxt;
};

class XboxProfileCache {
public:
    explicit XboxProfileCache(
        std::filesystem::path root = defaultRoot()
    )
        : root_(std::move(root)) {}

    XboxProfileCachePaths pathsFor(const std::string& profileName) const {
        return pathsForVersion(profileName, "");
    }

    XboxProfileCachePaths pathsForVersion(const std::string& profileName, const std::string& version) const {
        if (profileName.empty()) {
            throw std::runtime_error("xbox profile name is empty");
        }

        XboxProfileCachePaths out;
        out.profileName = profileName;
        out.version = version;
        out.root = root_;
        out.profileDir = root_ / sanitize(profileName);
        out.versionDir = version.empty() ? out.profileDir : (out.profileDir / sanitize(version));
        out.authJson = out.profileDir / "auth.json";
        out.loginPacketBin = out.versionDir / "real_login_packet.bin";
        out.identityUuidTxt = out.profileDir / "identity_uuid.txt";
        return out;
    }

    void ensureProfileDir(const std::string& profileName) const {
        auto paths = pathsFor(profileName);
        std::filesystem::create_directories(paths.profileDir);
    }

    void ensureProfileVersionDir(const std::string& profileName, const std::string& version) const {
        auto paths = pathsForVersion(profileName, version);
        std::filesystem::create_directories(paths.profileDir);
        std::filesystem::create_directories(paths.versionDir);
    }

private:
    std::filesystem::path root_;

    static std::filesystem::path defaultRoot() {
        // Hidden, autonomous cache. Regular bot users do not need to pass or see paths.
        // Microsoft/Xbox auth is shared per profile; Bedrock login packets are stored per version.
        if (const char* env = std::getenv("BEDROCK_XBOX_CACHE_DIR")) {
            if (*env) return std::filesystem::path(env);
        }
        if (const char* env = std::getenv("XDG_CACHE_HOME")) {
            if (*env) return std::filesystem::path(env) / "bedrock-protocol-cpp" / "xbox";
        }
        if (const char* home = std::getenv("HOME")) {
            if (*home) return std::filesystem::path(home) / ".cache" / "bedrock-protocol-cpp" / "xbox";
        }
        return std::filesystem::temp_directory_path() / "bedrock-protocol-cpp" / "xbox";
    }

    static std::string sanitize(const std::string& input) {
        std::string out;

        for (char c : input) {
            if (
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' ||
                c == '-'
            ) {
                out.push_back(c);
            } else {
                out.push_back('_');
            }
        }

        return out;
    }
};

} // namespace bedrock
