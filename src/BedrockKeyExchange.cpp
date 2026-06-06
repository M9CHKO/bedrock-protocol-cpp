#include "bedrock/BedrockKeyExchange.hpp"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace bedrock {

std::vector<std::string> BedrockKeyExchange::splitJwt(
    const std::string& jwt
) {
    std::vector<std::string> parts;
    std::string cur;

    for (char c : jwt) {
        if (c == '.') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }

    parts.push_back(cur);

    if (parts.size() < 2) {
        throw BedrockKeyExchangeError("JWT has less than 2 parts");
    }

    return parts;
}

std::vector<uint8_t> BedrockKeyExchange::base64Decode(
    const std::string& input
) {
    std::string b64;

    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            b64.push_back(c);
        }
    }

    size_t padding = 0;

    while (b64.size() % 4 != 0) {
        b64.push_back('=');
        padding++;
    }

    for (char c : b64) {
        if (c == '=') {
            padding++;
        }
    }

    std::vector<uint8_t> out;
    out.resize((b64.size() / 4) * 3 + 4);

    int len = EVP_DecodeBlock(
        out.data(),
        reinterpret_cast<const unsigned char*>(b64.data()),
        static_cast<int>(b64.size())
    );

    if (len < 0) {
        throw BedrockKeyExchangeError("EVP_DecodeBlock failed");
    }

    size_t realLen = static_cast<size_t>(len);

    size_t padChars = 0;

    if (!b64.empty() && b64[b64.size() - 1] == '=') {
        padChars++;
    }

    if (b64.size() >= 2 && b64[b64.size() - 2] == '=') {
        padChars++;
    }

    if (realLen >= padChars) {
        realLen -= padChars;
    }

    out.resize(realLen);
    return out;
}

std::vector<uint8_t> BedrockKeyExchange::base64UrlDecode(
    const std::string& b64url
) {
    std::string b64 = b64url;

    for (char& c : b64) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
    }

    return base64Decode(b64);
}

std::string BedrockKeyExchange::extractJwtHeaderJson(
    const std::string& jwt
) {
    auto parts = splitJwt(jwt);
    auto bytes = base64UrlDecode(parts[0]);

    return std::string(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
}

std::string BedrockKeyExchange::extractJwtPayloadJson(
    const std::string& jwt
) {
    auto parts = splitJwt(jwt);
    auto bytes = base64UrlDecode(parts[1]);

    return std::string(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
}

std::string BedrockKeyExchange::jsonExtractString(
    const std::string& json,
    const std::string& key
) {
    std::string needle = "\"" + key + "\"";

    size_t pos = json.find(needle);

    if (pos == std::string::npos) {
        throw BedrockKeyExchangeError("json key not found: " + key);
    }

    pos = json.find(':', pos);

    if (pos == std::string::npos) {
        throw BedrockKeyExchangeError("json colon not found for key: " + key);
    }

    pos++;

    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '"') {
        throw BedrockKeyExchangeError("json value is not string for key: " + key);
    }

    pos++;

    std::string value;
    bool escaped = false;

    for (; pos < json.size(); pos++) {
        char c = json[pos];

        if (escaped) {
            switch (c) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(c); break;
            }

            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == '"') {
            return value;
        }

        value.push_back(c);
    }

    throw BedrockKeyExchangeError("unterminated json string for key: " + key);
}

std::vector<uint8_t> BedrockKeyExchange::deriveEcdhSharedSecret(
    const std::string& clientPrivateKeyPem,
    const std::vector<uint8_t>& serverPublicKeyDer
) {
    BIO* privateBio = BIO_new_mem_buf(
        clientPrivateKeyPem.data(),
        static_cast<int>(clientPrivateKeyPem.size())
    );

    if (!privateBio) {
        throw BedrockKeyExchangeError("BIO_new_mem_buf private key failed");
    }

    EVP_PKEY* privateKey = PEM_read_bio_PrivateKey(
        privateBio,
        nullptr,
        nullptr,
        nullptr
    );

    BIO_free(privateBio);

    if (!privateKey) {
        throw BedrockKeyExchangeError("PEM_read_bio_PrivateKey failed");
    }

    const unsigned char* publicPtr = serverPublicKeyDer.data();

    EVP_PKEY* publicKey = d2i_PUBKEY(
        nullptr,
        &publicPtr,
        static_cast<long>(serverPublicKeyDer.size())
    );

    if (!publicKey) {
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("d2i_PUBKEY server public key failed");
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privateKey, nullptr);

    if (!ctx) {
        EVP_PKEY_free(publicKey);
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("EVP_PKEY_CTX_new failed");
    }

    if (EVP_PKEY_derive_init(ctx) != 1) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(publicKey);
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("EVP_PKEY_derive_init failed");
    }

    if (EVP_PKEY_derive_set_peer(ctx, publicKey) != 1) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(publicKey);
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("EVP_PKEY_derive_set_peer failed");
    }

    size_t secretLen = 0;

    if (EVP_PKEY_derive(ctx, nullptr, &secretLen) != 1) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(publicKey);
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("EVP_PKEY_derive length failed");
    }

    std::vector<uint8_t> sharedSecret(secretLen);

    if (EVP_PKEY_derive(ctx, sharedSecret.data(), &secretLen) != 1) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(publicKey);
        EVP_PKEY_free(privateKey);
        throw BedrockKeyExchangeError("EVP_PKEY_derive failed");
    }

    sharedSecret.resize(secretLen);

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(publicKey);
    EVP_PKEY_free(privateKey);

    return sharedSecret;
}

std::vector<uint8_t> BedrockKeyExchange::sha256(
    const std::vector<uint8_t>& a,
    const std::vector<uint8_t>& b
) {
    SHA256_CTX ctx;

    if (SHA256_Init(&ctx) != 1) {
        throw BedrockKeyExchangeError("SHA256_Init failed");
    }

    if (!a.empty() && SHA256_Update(&ctx, a.data(), a.size()) != 1) {
        throw BedrockKeyExchangeError("SHA256_Update a failed");
    }

    if (!b.empty() && SHA256_Update(&ctx, b.data(), b.size()) != 1) {
        throw BedrockKeyExchangeError("SHA256_Update b failed");
    }

    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);

    if (SHA256_Final(out.data(), &ctx) != 1) {
        throw BedrockKeyExchangeError("SHA256_Final failed");
    }

    return out;
}

DerivedKeyResult BedrockKeyExchange::deriveFromServerHandshakeJwtAndPrivateKeyPem(
    const std::string& serverHandshakeJwt,
    const std::string& clientPrivateKeyPem
) {
    std::string headerJson = extractJwtHeaderJson(serverHandshakeJwt);
    std::string payloadJson = extractJwtPayloadJson(serverHandshakeJwt);

    std::string x5u = jsonExtractString(headerJson, "x5u");
    std::string saltB64 = jsonExtractString(payloadJson, "salt");

    DerivedKeyResult result;

    result.serverPublicKeyDer = base64Decode(x5u);
    result.salt = base64Decode(saltB64);

    result.sharedSecret = deriveEcdhSharedSecret(
        clientPrivateKeyPem,
        result.serverPublicKeyDer
    );

    result.secretKeyBytes = sha256(
        result.salt,
        result.sharedSecret
    );

    if (result.secretKeyBytes.size() < 16) {
        throw BedrockKeyExchangeError("secret key too small");
    }

    result.iv16.assign(
        result.secretKeyBytes.begin(),
        result.secretKeyBytes.begin() + 16
    );

    return result;
}

} // namespace bedrock
