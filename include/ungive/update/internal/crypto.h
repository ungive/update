#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#include <openssl/decoder.h>
#include <openssl/evp.h>

namespace ungive::update::internal::crypto
{

using public_key = std::shared_ptr<EVP_PKEY>;
using digest_context = std::shared_ptr<EVP_MD_CTX>;

inline public_key parse_public_key(std::string const& input,
    std::string const& input_type, std::string const& key_type)
{
    OSSL_DECODER_CTX* dctx;
    EVP_PKEY* pkey;
    dctx = OSSL_DECODER_CTX_new_for_pkey(&pkey, input_type.c_str(), NULL,
        key_type.c_str(), OSSL_KEYMGMT_SELECT_PUBLIC_KEY, NULL, NULL);
    if (dctx == nullptr) {
        throw std::runtime_error("openssl: failed to create decoder");
    }
    auto key = reinterpret_cast<const unsigned char*>(input.c_str());
    auto key_len = input.size();
    auto result = OSSL_DECODER_from_data(dctx, &key, &key_len);
    OSSL_DECODER_CTX_free(dctx);
    if (result != 1) {
        throw std::runtime_error("openssl: failed to decode public key");
    }
    return std::shared_ptr<EVP_PKEY>(pkey, [](EVP_PKEY* key) {
        EVP_PKEY_free(key);
    });
}

inline digest_context create_digest_context()
{
    EVP_MD_CTX* mdctx;
    if ((mdctx = EVP_MD_CTX_create()) == nullptr)
        throw std::runtime_error("openssl: failed to create context");
    return std::shared_ptr<EVP_MD_CTX>(mdctx, [](EVP_MD_CTX* ctx) {
        EVP_MD_CTX_free(ctx);
    });
}

// Verifies the signature/message digest.
inline bool verify_signature(
    EVP_PKEY* pkey, std::string const& signature, std::string const& message)
{
    auto mdctx = create_digest_context();
    if (1 != EVP_DigestVerifyInit(mdctx.get(), NULL, NULL, NULL, pkey)) {
        throw std::runtime_error("openssl: failed to init digest verify");
    }
    auto result = EVP_DigestVerify(mdctx.get(),
        reinterpret_cast<const unsigned char*>(signature.data()),
        signature.size(),
        reinterpret_cast<const unsigned char*>(message.data()), message.size());
    if (result != 1 && result != 0) {
        throw std::runtime_error("openssl: failed to verify message digest");
    }
    return result == 1;
}

// Computes a SHA-256 hash of a file.
inline std::string sha256_file(std::filesystem::path const& path)
{
    unsigned char hash[SHA256_DIGEST_LENGTH] = {};
    auto mdctx = create_digest_context();
    const EVP_MD* md = EVP_MD_fetch(NULL, "SHA256", NULL);
    if (1 != EVP_DigestInit_ex(mdctx.get(), md, NULL))
        throw std::runtime_error("openssl: failed to init digest");
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "file to hash does not exist: " + path.string());
    }
    std::ifstream ifs(path, std::ifstream::binary);
    std::vector<char> buffer(1024 * 1024, 0);
    while (!ifs.eof()) {
        ifs.read(buffer.data(), buffer.size());
        std::streamsize n = ifs.gcount();
        if (n <= 0) {
            continue;
        }
        if (1 != EVP_DigestUpdate(mdctx.get(), buffer.data(), n))
            throw std::runtime_error("openssl: failed to update digest");
    }
    if (1 != EVP_DigestFinal_ex(mdctx.get(), hash, 0))
        throw std::runtime_error("openssl: failed to finalize digest");
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// Parses SHA256 checksums from a sha256sum file.
// Ideally this file should have been generated
// with the "sha256sum" linux command.
inline std::vector<std::pair<std::string, std::string>> parse_sha256sums(
    std::string const& data)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::ostringstream oss_hash;
    std::ostringstream oss_path;
    size_t state = 0;
    for (size_t i = 0; i < data.size(); i++) {
        char c = data.at(i);
        if ((c == '\r' || c == '\n') && state != 2) {
            state = 0;
            continue;
        }
        switch (state) {
        case 0:
            if (c == ' ') {
                state = 1;
                continue;
            }
            oss_hash << c;
            continue;
        case 1:
            if (c == '*') {
                state = 2;
            }
            continue;
        case 2:
            if (c == '\r' || c == '\n') {
                state = 0;
            } else {
                if (c == '/') {
                    oss_path << static_cast<char>(
                        std::filesystem::path::preferred_separator);
                } else {
                    oss_path << c;
                }
                continue;
            }
        }
        result.push_back(std::make_pair(oss_hash.str(), oss_path.str()));
        oss_hash = std::ostringstream();
        oss_path = std::ostringstream();
        state = 0;
    }
    return result;
}

} // namespace ungive::update::internal::crypto
