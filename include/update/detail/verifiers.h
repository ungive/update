#pragma once

#include "update/detail/common.h"
#include "update/internal/crypto.h"
#include "update/internal/types.h"
#include "update/internal/util.h"

namespace update::verifiers
{

class verification_failed : public std::runtime_error
{
public:
    using runtime_error::runtime_error;
};

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

    bool operator()(std::string const& path,
        std::unordered_map<std::string, downloaded_file> const& files)
        const override
    {
        bool has_valid = false;
        for (auto const& encoded_public_key : m_encoded_public_keys) {
            auto key = internal::crypto::parse_public_key(
                encoded_public_key, m_key_format, m_key_type);
            auto valid_signature = internal::crypto::verify_signature(key.get(),
                files.at(m_digest_filename).read(std::ios::binary),
                files.at(m_message_filename).read(std::ios::binary));
            if (valid_signature) {
                has_valid = valid_signature;
                break;
            }
        }
        if (!has_valid) {
            throw verification_failed("invalid signature");
        }
        return true;
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

    bool operator()(std::string const& path,
        std::unordered_map<std::string, downloaded_file> const& files)
        const override
    {
        auto it = files.find(m_sums_filename);
        if (it == files.end()) {
            throw std::runtime_error("sha256sums file not available");
        }
        auto sums = parse_sha256sums(it->second.read());
        auto found = files.end();
        std::string hash;
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
                std::filesystem::absolute(path)) {
                found = files.find(path);
                hash = pair.first;
                break;
            }
        }
        if (found == files.end()) {
            throw std::runtime_error(
                "file to verify not present in shasums file");
        }
        if (!verify_hash(hash, found->second)) {
            throw verification_failed("sha256 hashes do not match");
        }
        return true;
    }

private:
    inline bool verify_hash(
        std::string const& hash, downloaded_file const& file) const
    {
        return hash == internal::crypto::sha256_file(file.path());
    }

    std::vector<std::pair<std::string, std::string>> parse_sha256sums(
        std::string const& data) const
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

    std::string m_sums_filename;
};

} // namespace update::verifiers
