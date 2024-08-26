#pragma once

#include <functional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ungive/update/detail/common.h"

namespace ungive::update::types
{

struct verification_payload
{
    std::string const& file;
    std::unordered_map<std::string, downloaded_file> const& additional_files;

    verification_payload(
        decltype(file) file, decltype(additional_files) additional_files)
        : file{ file }, additional_files{ additional_files }
    {
    }

    verification_payload(verification_payload const&) = delete;
};

class verifier
{
public:
    // Verifies the given path using the additional files.
    // Must throw an exception if verification failed, explaining why.
    // If no exception is thrown, then verification is considered successful.
    virtual void operator()(verification_payload const& payload) const = 0;

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

class content_operation
{
public:
    // Performs an operation on extracted update content.
    // The parameter is the directory to which the update has been extracted.
    virtual void operator()(
        std::filesystem::path const& extracted_directory) = 0;
};

} // namespace ungive::update::types
