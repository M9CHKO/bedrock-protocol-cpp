#include "bedrock/BedrockEncryption.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cstring>

namespace bedrock {


BedrockAesGcmStream::BedrockAesGcmStream(
    const std::vector<uint8_t>& secretKeyBytes,
    const std::vector<uint8_t>& iv16,
    Mode mode
) : mode_(mode) {
    if (secretKeyBytes.size() != 32) {
        throw BedrockEncryptionError("BedrockAesGcmStream secretKeyBytes must be 32 bytes");
    }

    if (iv16.size() < 12) {
        throw BedrockEncryptionError("BedrockAesGcmStream iv16 must contain at least 12 bytes");
    }

    std::vector<uint8_t> iv12(
        iv16.begin(),
        iv16.begin() + 12
    );

    ctx_ = EVP_CIPHER_CTX_new();

    if (!ctx_) {
        throw BedrockEncryptionError("EVP_CIPHER_CTX_new stream failed");
    }

    if (mode_ == Mode::Encrypt) {
        if (EVP_EncryptInit_ex(ctx_, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_EncryptInit_ex stream cipher failed");
        }

        if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_CTRL_GCM_SET_IVLEN encrypt stream failed");
        }

        if (EVP_EncryptInit_ex(ctx_, nullptr, nullptr, secretKeyBytes.data(), iv12.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_EncryptInit_ex stream key/iv failed");
        }
    } else {
        if (EVP_DecryptInit_ex(ctx_, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_DecryptInit_ex stream cipher failed");
        }

        if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_CTRL_GCM_SET_IVLEN decrypt stream failed");
        }

        if (EVP_DecryptInit_ex(ctx_, nullptr, nullptr, secretKeyBytes.data(), iv12.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw BedrockEncryptionError("EVP_DecryptInit_ex decrypt stream key/iv failed");
        }
    }
}

BedrockAesGcmStream::~BedrockAesGcmStream() {
    if (ctx_) {
        EVP_CIPHER_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

std::vector<uint8_t> BedrockAesGcmStream::process(
    const std::vector<uint8_t>& input
) {
    if (!ctx_) {
        throw BedrockEncryptionError("BedrockAesGcmStream context is null");
    }

    std::vector<uint8_t> out;
    out.resize(input.size() + 16);

    int len = 0;

    if (mode_ == Mode::Encrypt) {
        if (!input.empty()) {
            if (EVP_EncryptUpdate(
                ctx_,
                out.data(),
                &len,
                input.data(),
                static_cast<int>(input.size())
            ) != 1) {
                throw BedrockEncryptionError("EVP_EncryptUpdate stream failed");
            }
        }
    } else {
        if (!input.empty()) {
            if (EVP_DecryptUpdate(
                ctx_,
                out.data(),
                &len,
                input.data(),
                static_cast<int>(input.size())
            ) != 1) {
                throw BedrockEncryptionError("EVP_DecryptUpdate stream failed");
            }
        }
    }

    out.resize(static_cast<size_t>(len));
    return out;
}


void BedrockEncryption::writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
}

std::vector<uint8_t> BedrockEncryption::computeChecksum(
    const std::vector<uint8_t>& packetPlaintext,
    uint64_t sendCounter,
    const std::vector<uint8_t>& secretKeyBytes
) {
    if (secretKeyBytes.size() != 32) {
        throw BedrockEncryptionError("secretKeyBytes must be 32 bytes");
    }

    std::vector<uint8_t> counter;
    writeU64LE(counter, sendCounter);

    SHA256_CTX ctx;

    if (SHA256_Init(&ctx) != 1) {
        throw BedrockEncryptionError("SHA256_Init failed");
    }

    if (SHA256_Update(&ctx, counter.data(), counter.size()) != 1) {
        throw BedrockEncryptionError("SHA256_Update counter failed");
    }

    if (!packetPlaintext.empty()) {
        if (SHA256_Update(&ctx, packetPlaintext.data(), packetPlaintext.size()) != 1) {
            throw BedrockEncryptionError("SHA256_Update packet failed");
        }
    }

    if (SHA256_Update(&ctx, secretKeyBytes.data(), secretKeyBytes.size()) != 1) {
        throw BedrockEncryptionError("SHA256_Update secret failed");
    }

    uint8_t hash[SHA256_DIGEST_LENGTH];

    if (SHA256_Final(hash, &ctx) != 1) {
        throw BedrockEncryptionError("SHA256_Final failed");
    }

    return std::vector<uint8_t>(hash, hash + 8);
}

std::vector<uint8_t> BedrockEncryption::makeAesPlaintext(
    const std::vector<uint8_t>& packetPlaintext,
    uint64_t sendCounter,
    const std::vector<uint8_t>& secretKeyBytes
) {
    auto check = computeChecksum(
        packetPlaintext,
        sendCounter,
        secretKeyBytes
    );

    std::vector<uint8_t> out;
    out.reserve(packetPlaintext.size() + check.size());

    out.insert(out.end(), packetPlaintext.begin(), packetPlaintext.end());
    out.insert(out.end(), check.begin(), check.end());

    return out;
}

std::vector<uint8_t> BedrockEncryption::aes256GcmEncryptNoTag(
    const std::vector<uint8_t>& secretKeyBytes,
    const std::vector<uint8_t>& iv12,
    const std::vector<uint8_t>& plaintext
) {
    if (secretKeyBytes.size() != 32) {
        throw BedrockEncryptionError("secretKeyBytes must be 32 bytes for AES-256-GCM");
    }

    if (iv12.size() != 12) {
        throw BedrockEncryptionError("iv12 must be 12 bytes for AES-GCM");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

    if (!ctx) {
        throw BedrockEncryptionError("EVP_CIPHER_CTX_new encrypt failed");
    }

    std::vector<uint8_t> out;
    out.resize(plaintext.size() + 16);

    int len = 0;
    int total = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_EncryptInit_ex cipher failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_CTRL_GCM_SET_IVLEN failed");
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, secretKeyBytes.data(), iv12.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_EncryptInit_ex key/iv failed");
    }

    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(
            ctx,
            out.data(),
            &len,
            plaintext.data(),
            static_cast<int>(plaintext.size())
        ) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw BedrockEncryptionError("EVP_EncryptUpdate failed");
        }

        total += len;
    }

    if (EVP_EncryptFinal_ex(ctx, out.data() + total, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_EncryptFinal_ex failed");
    }

    total += len;

    out.resize(static_cast<size_t>(total));

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> BedrockEncryption::aes256GcmDecryptNoTag(
    const std::vector<uint8_t>& secretKeyBytes,
    const std::vector<uint8_t>& iv12,
    const std::vector<uint8_t>& encrypted
) {
    if (secretKeyBytes.size() != 32) {
        throw BedrockEncryptionError("secretKeyBytes must be 32 bytes for AES-256-GCM");
    }

    if (iv12.size() != 12) {
        throw BedrockEncryptionError("iv12 must be 12 bytes for AES-GCM");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

    if (!ctx) {
        throw BedrockEncryptionError("EVP_CIPHER_CTX_new decrypt failed");
    }

    std::vector<uint8_t> out;
    out.resize(encrypted.size() + 16);

    int len = 0;
    int total = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_DecryptInit_ex cipher failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_CTRL_GCM_SET_IVLEN decrypt failed");
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, secretKeyBytes.data(), iv12.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw BedrockEncryptionError("EVP_DecryptInit_ex key/iv decrypt failed");
    }

    if (!encrypted.empty()) {
        if (EVP_DecryptUpdate(
            ctx,
            out.data(),
            &len,
            encrypted.data(),
            static_cast<int>(encrypted.size())
        ) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw BedrockEncryptionError("EVP_DecryptUpdate failed");
        }

        total += len;
    }

    // Bedrock-protocol uses streaming GCM here and does not append/check an auth tag.
    out.resize(static_cast<size_t>(total));

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> BedrockEncryption::encryptMcpePayloadGcm(
    const std::vector<uint8_t>& packetPlaintext,
    uint64_t sendCounter,
    const std::vector<uint8_t>& secretKeyBytes,
    const std::vector<uint8_t>& iv16
) {
    if (iv16.size() < 12) {
        throw BedrockEncryptionError("iv16 must contain at least 12 bytes");
    }

    auto aesPlaintext = makeAesPlaintext(
        packetPlaintext,
        sendCounter,
        secretKeyBytes
    );

    std::vector<uint8_t> iv12(
        iv16.begin(),
        iv16.begin() + 12
    );

    auto encrypted = aes256GcmEncryptNoTag(
        secretKeyBytes,
        iv12,
        aesPlaintext
    );

    std::vector<uint8_t> out;
    out.reserve(1 + encrypted.size());

    out.push_back(0xfe);
    out.insert(out.end(), encrypted.begin(), encrypted.end());

    return out;
}

std::vector<uint8_t> BedrockEncryption::decryptMcpePayloadGcm(
    const std::vector<uint8_t>& encryptedMcpePayload,
    uint64_t receiveCounter,
    const std::vector<uint8_t>& secretKeyBytes,
    const std::vector<uint8_t>& iv16
) {
    if (encryptedMcpePayload.empty()) {
        throw BedrockEncryptionError("empty encrypted MCPE payload");
    }

    if (encryptedMcpePayload[0] != 0xfe) {
        throw BedrockEncryptionError("encrypted MCPE payload missing 0xfe header");
    }

    if (iv16.size() < 12) {
        throw BedrockEncryptionError("iv16 must contain at least 12 bytes");
    }

    std::vector<uint8_t> encrypted(
        encryptedMcpePayload.begin() + 1,
        encryptedMcpePayload.end()
    );

    std::vector<uint8_t> iv12(
        iv16.begin(),
        iv16.begin() + 12
    );

    auto aesPlaintext = aes256GcmDecryptNoTag(
        secretKeyBytes,
        iv12,
        encrypted
    );

    if (aesPlaintext.size() < 8) {
        throw BedrockEncryptionError("decrypted payload too small for checksum");
    }

    std::vector<uint8_t> packetPlaintext(
        aesPlaintext.begin(),
        aesPlaintext.end() - 8
    );

    std::vector<uint8_t> receivedChecksum(
        aesPlaintext.end() - 8,
        aesPlaintext.end()
    );

    auto expectedChecksum = computeChecksum(
        packetPlaintext,
        receiveCounter,
        secretKeyBytes
    );

    if (receivedChecksum != expectedChecksum) {
        throw BedrockEncryptionError("encrypted payload checksum mismatch");
    }

    return packetPlaintext;
}

} // namespace bedrock
