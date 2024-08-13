#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ungive/detail/common.h"

namespace ungive::update::internal::types
{

using verifier_func = std::function<bool(std::string const& path,
    std::unordered_map<std::string, downloaded_file> const&)>;

using latest_extractor_func = std::function<std::pair<version_number, file_url>(
    downloaded_file const& file)>;

using latest_retriever_func = std::function<std::pair<version_number, file_url>(
    std::regex filename_pattern)>;

class verifier_interface
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

class latest_extractor_interface
{
public:
    // Extracts the version information and update file URL from the given file.
    // Returns the version number and the latest update file's URL.
    virtual std::pair<version_number, file_url> operator()(
        downloaded_file const& file) const = 0;
};

class latest_retriever_interface
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

class base_verifier : public types::verifier_interface
{
public:
    base_verifier(std::string const& required_file) : m_files({ required_file })
    {
    }

    base_verifier(std::vector<std::string> const& required_files)
        : m_files(required_files.begin(), required_files.end())
    {
    }

    std::vector<std::string> const& files() const override { return m_files; }

protected:
    std::vector<std::string> m_files;
};

using post_update_operation_func =
    std::function<void(std::filesystem::path const&)>;

class post_update_operation_interface
{
public:
    // Performs a post-update operation.
    // The parameter is the directory to which the downloaded update
    // has been extracted.
    virtual void operator()(
        std::filesystem::path const& extracted_directory) = 0;
};

} // namespace ungive::update::internal::types
