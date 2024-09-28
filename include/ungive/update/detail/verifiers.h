#pragma once

#include "ungive/update/detail/common.h"
#include "ungive/update/internal/crypto.h"
#include "ungive/update/internal/types.h"
#include "ungive/update/internal/util.h"

namespace ungive::update::verifiers
{

class verification_failed : public std::runtime_error
{
public:
    using runtime_error::runtime_error;
};

// Verifier for message digests for authentication.
class message_digest : public internal::types::base_verifier
{
public:
    message_digest(std::string const& message_filename,
        std::string const& digest_filename, std::string const& key_format,
        std::string const& key_type,
        std::vector<std::string> const& encoded_public_keys)
        : base_verifier({ message_filename, digest_filename }),
          m_message_filename{ message_filename },
          m_digest_filename{ digest_filename },
          m_encoded_public_keys{ encoded_public_keys },
          m_key_format{ key_format }, m_key_type{ key_type }
    {
    }

    message_digest(std::string const& message_filename,
        std::string const& digest_filename, std::string const& key_format,
        std::string const& key_type, std::string const& encoded_public_key)
        : message_digest(message_filename, digest_filename, key_format,
              key_type, std::vector<std::string>{ encoded_public_key })
    {
    }

    void operator()(types::verification_payload const& payload) const override
    {
        bool has_valid = false;
        for (auto const& encoded_public_key : m_encoded_public_keys) {
            auto key = internal::crypto::parse_public_key(
                encoded_public_key, m_key_format, m_key_type);
            auto valid_signature = internal::crypto::verify_signature(key.get(),
                payload.additional_files.at(m_digest_filename)
                    .read(std::ios::binary),
                payload.additional_files.at(m_message_filename)
                    .read(std::ios::binary));
            if (valid_signature) {
                has_valid = valid_signature;
                break;
            }
        }
        if (!has_valid) {
            throw verification_failed("invalid " + m_key_type + " signature");
        }
        logger()(log_level::info,
            "file authenticity OK, " + m_key_type + " signatures match");
    }

private:
    std::string m_message_filename;
    std::string m_digest_filename;
    std::vector<std::string> m_encoded_public_keys;
    std::string m_key_format;
    std::string m_key_type;
};

// Verifier for "SHA256SUMS" type of files.
class sha256sums : public internal::types::base_verifier
{
public:
    sha256sums(std::string const& shasums_filename)
        : base_verifier(shasums_filename), m_sums_filename{ shasums_filename }
    {
    }

    void operator()(types::verification_payload const& payload) const override
    {
        auto it = payload.additional_files.find(m_sums_filename);
        if (it == payload.additional_files.end()) {
            throw std::runtime_error("sha256sums file not available");
        }
        auto sums = internal::crypto::parse_sha256sums(it->second.read());
        auto found = payload.additional_files.end();
        std::string expected_hash;
        for (auto const& pair : sums) {
            auto verify_path = std::filesystem::path(pair.second);
            if (!verify_path.has_filename()) {
                continue;
            }
            auto sums_path = std::filesystem::path(m_sums_filename);
            if (sums_path.has_parent_path()) {
                verify_path = sums_path.parent_path() / verify_path;
            }
            if (std::filesystem::absolute(verify_path) ==
                std::filesystem::absolute(payload.file)) {
                found = payload.additional_files.find(payload.file);
                expected_hash = pair.first;
                break;
            }
        }
        if (found == payload.additional_files.end()) {
            throw std::runtime_error(
                "file to verify not present in shasums file: " + payload.file);
        }
        auto actual_hash = internal::crypto::sha256_file(found->second.path());
        if (actual_hash != expected_hash) {
            throw verification_failed("SHA256 hashes for file " + payload.file +
                " do not match: expected " + expected_hash + ", got " +
                actual_hash);
        }
        logger()(log_level::info,
            "file integrity OK, SHA256 hashes match: expected " +
                expected_hash + ", got " + actual_hash);
    }

private:
    inline bool verify_hash(
        std::string const& hash, downloaded_file const& file) const
    {
        return hash == internal::crypto::sha256_file(file.path());
    }

    std::string m_sums_filename;
};

} // namespace ungive::update::verifiers
