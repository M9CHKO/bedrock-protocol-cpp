#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

namespace bedrock {

class BedrockEncryptionError : public std::runtime_error {
public:
    explicit BedrockEncryptionError(const std::string& msg)
        : std::runtime_error(msg) {}
};

class BedrockAesGcmStream {
public:
    enum class Mode {
        Encrypt,
        Decrypt
    };

    BedrockAesGcmStream(
        const std::vector<uint8_t>& secretKeyBytes,
        const std::vector<uint8_t>& iv16,
        Mode mode
    );

    ~BedrockAesGcmStream();

    BedrockAesGcmStream(const BedrockAesGcmStream&) = delete;
    BedrockAesGcmStream& operator=(const BedrockAesGcmStream&) = delete;

    std::vector<uint8_t> process(
        const std::vector<uint8_t>& input
    );

private:
    EVP_CIPHER_CTX* ctx_ = nullptr;
    Mode mode_;
};

class BedrockEncryption {
public:
    static std::vector<uint8_t> computeChecksum(
        const std::vector<uint8_t>& packetPlaintext,
        uint64_t sendCounter,
        const std::vector<uint8_t>& secretKeyBytes
    );

    static std::vector<uint8_t> makeAesPlaintext(
        const std::vector<uint8_t>& packetPlaintext,
        uint64_t sendCounter,
        const std::vector<uint8_t>& secretKeyBytes
    );

    static std::vector<uint8_t> aes256GcmEncryptNoTag(
        const std::vector<uint8_t>& secretKeyBytes,
        const std::vector<uint8_t>& iv12,
        const std::vector<uint8_t>& plaintext
    );

    static std::vector<uint8_t> aes256GcmDecryptNoTag(
        const std::vector<uint8_t>& secretKeyBytes,
        const std::vector<uint8_t>& iv12,
        const std::vector<uint8_t>& encrypted
    );

    static std::vector<uint8_t> encryptMcpePayloadGcm(
        const std::vector<uint8_t>& packetPlaintext,
        uint64_t sendCounter,
        const std::vector<uint8_t>& secretKeyBytes,
        const std::vector<uint8_t>& iv16
    );

    static std::vector<uint8_t> decryptMcpePayloadGcm(
        const std::vector<uint8_t>& encryptedMcpePayload,
        uint64_t receiveCounter,
        const std::vector<uint8_t>& secretKeyBytes,
        const std::vector<uint8_t>& iv16
    );

private:
    static void writeU64LE(std::vector<uint8_t>& out, uint64_t v);
};

} // namespace bedrock
