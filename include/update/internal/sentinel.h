#pragma once

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include "update/detail/common.h"
#include "update/internal/util.h"

#define SENTINEL_FILENAME ".sentinel"

namespace update::internal
{

// Returns the name of the file that must be present in the root
// of an extracted update's directory for it to be considered valid.
static inline const char* sentinel_filename() { return SENTINEL_FILENAME; }

class sentinel
{
public:
    sentinel(std::filesystem::path const& location)
    {
        if (std::filesystem::is_directory(location) ||
            location.filename() != sentinel_filename()) {
            std::filesystem::create_directories(location);
            m_location = location / sentinel_filename();
        } else {
            m_location = location;
        }
    }

    inline void version(version_number const& version) { m_version = version; }

    inline version_number version() const
    {
        if (!m_version.has_value()) {
            throw std::runtime_error("no version information available");
        }
        return m_version.value();
    }

    // Attempts to read the sentinel's contents.
    // Can be used to check if a version's directory is valid.
    // Returns if the operation was successful, does not throw.
    bool read()
    {
        if (!std::filesystem::exists(m_location)) {
            return false;
        }
        try {
            decode(internal::read_file(m_location));
            return true;
        }
        catch (...) {
            return false;
        }
    }

    // Writes the contents for the sentinel file.
    // May throw an exception if information is missing or writing failed.
    void write()
    {
        if (!m_version.has_value()) {
            throw std::runtime_error("missing version information");
        }
        internal::write_file(m_location, "version=" + m_version->string());
    }

private:
    // Encodes all fields.
    std::string encode()
    {
        // Just one line at the moment.
        return "version=" + m_version->string();
    }

    // Decodes encoded fields, may throw an exception.
    void decode(std::string const& content)
    {
        // Fields
        std::optional<version_number> version{};

        std::istringstream iss(content);
        for (std::string line; std::getline(iss, line);) {
            auto index = line.find_first_of('=');
            if (index == std::string::npos) {
                continue;
            }
            if (line.substr(0, index) == "version") {
                auto x =
                    line.substr(index + 1, line.find_first_of('\n', index + 1));
                version = version_number::from_string(x);
            }
        }
        if (!version.has_value()) {
            throw std::runtime_error("missing version field in sentinel file");
        }

        // All decoded correctly, update fields.
        m_version = version.value();
    }

    std::filesystem::path m_location;

    std::optional<version_number> m_version{};
};

} // namespace update::internal

#undef SENTINEL_FILENAME
