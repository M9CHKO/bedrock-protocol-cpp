#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

class BedrockKeyExchangeError : public std::runtime_error {
public:
    explicit BedrockKeyExchangeError(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct DerivedKeyResult {
    std::vector<uint8_t> serverPublicKeyDer;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> sharedSecret;
    std::vector<uint8_t> secretKeyBytes;
    std::vector<uint8_t> iv16;
};

class BedrockKeyExchange {
public:
    static DerivedKeyResult deriveFromServerHandshakeJwtAndPrivateKeyPem(
        const std::string& serverHandshakeJwt,
        const std::string& clientPrivateKeyPem
    );

    static std::string extractJwtHeaderJson(const std::string& jwt);
    static std::string extractJwtPayloadJson(const std::string& jwt);

    static std::string jsonExtractString(
        const std::string& json,
        const std::string& key
    );

    static std::vector<uint8_t> base64Decode(
        const std::string& b64
    );

    static std::vector<uint8_t> base64UrlDecode(
        const std::string& b64url
    );

private:
    static std::vector<std::string> splitJwt(
        const std::string& jwt
    );

    static std::vector<uint8_t> deriveEcdhSharedSecret(
        const std::string& clientPrivateKeyPem,
        const std::vector<uint8_t>& serverPublicKeyDer
    );

    static std::vector<uint8_t> sha256(
        const std::vector<uint8_t>& a,
        const std::vector<uint8_t>& b
    );
};

} // namespace bedrock
