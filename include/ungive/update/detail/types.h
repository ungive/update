#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ungive/update/detail/common.h"

namespace ungive::update::types
{

class verifier
{
public:
    // Verifies the given path using the additional files.
    // Must return true if verification succeeded.
    // Should throw an exception if verification failed, explaining what
    // happened or return false.
    virtual bool operator()(std::string const& path,
        std::unordered_map<std::string, downloaded_file> const& files)
        const = 0;

    // Returns the additional files needed for this verification.
    virtual std::vector<std::string> const& files() const = 0;
};

class latest_extractor
{
public:
    // Extracts the version information and update file URL from the given file.
    // Returns the version number and the latest update file's URL.
    virtual std::pair<version_number, file_url> operator()(
        downloaded_file const& file) const = 0;
};

class latest_retriever
{
public:
    // Retrieves the URL for the latest update.
    // The update's version must be greater than the current version
    // and the filename must match the given pattern.
    virtual std::pair<version_number, file_url> operator()(
        std::regex filename_pattern) const = 0;

    // Returns a generic, constant pattern for this latest retriever
    // which all download URLs must match.
    virtual std::regex url_pattern() const = 0;
};

} // namespace ungive::update::types
