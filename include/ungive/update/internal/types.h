#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ungive/update/detail/common.h"
#include "ungive/update/detail/types.h"

namespace ungive::update::internal::types
{

using verifier_func = std::function<bool(std::string const& path,
    std::unordered_map<std::string, downloaded_file> const&)>;

using latest_extractor_func = std::function<std::pair<version_number, file_url>(
    downloaded_file const& file)>;

using latest_retriever_func = std::function<std::pair<version_number, file_url>(
    std::regex filename_pattern)>;

class base_verifier : public ungive::update::types::verifier
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
