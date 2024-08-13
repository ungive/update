#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "ungive/internal/types.h"
#include "ungive/internal/win/startmenu.h"
#include "ungive/internal/zip.h"

namespace ungive::update::operations
{
class flatten_extracted_directory
    : public internal::types::post_update_operation_interface
{
public:
    // Flattens the extracted directory if the ZIP file contains
    // a single directory. Optionally fails if flattening could not occur
    // because the root of the ZIP file contained other files or no directory.
    flatten_extracted_directory(bool fail_if_not_flattened = true)
        : m_fail_if_not_flattened{ fail_if_not_flattened }
    {
    }

    void operator()(std::filesystem::path const& extracted_directory) override
    {
        auto result =
            internal::flatten_root_directory(extracted_directory.string());
        if (m_fail_if_not_flattened && !result) {
            throw std::runtime_error("failed to flatten root directory");
        }
    }

private:
    bool m_fail_if_not_flattened;
};

class install_start_menu_shortcut
    : public internal::types::post_update_operation_interface
{
public:
    // Installs a start menu shortcut to the target executable.
    // If the target executable path is a relative path,
    // the executable path is resolved to be within the directory
    // of the extracted and installed update.
    // The category name is optional and resembles a subfolder
    // in which the shortcut is placed.
    install_start_menu_shortcut(std::filesystem::path const& target_executable,
        std::string const& link_name,
        std::optional<std::string> const& category_name = std::nullopt)
        : m_target_executable{ target_executable }, m_link_name{ link_name },
          m_category_name{ category_name }
    {
        if (link_name.empty()) {
            throw std::runtime_error("the link name cannot be empty");
        }
        if (category_name.has_value() && category_name->empty()) {
            throw std::runtime_error("the category name cannot be empty");
        }
    }

    void operator()(std::filesystem::path const& extracted_directory) override
    {
        auto target_executable = m_target_executable;
        if (target_executable.is_relative()) {
            target_executable = extracted_directory / target_executable;
        }
        if (!std::filesystem::exists(target_executable)) {
            throw std::runtime_error("the target executable does not exist");
        }
        auto result = internal::win::create_start_menu_entry(
            target_executable, m_link_name, m_category_name.value_or(""));
        if (!result) {
            throw std::runtime_error("failed to create start menu shortcut");
        }
    }

private:
    std::filesystem::path m_target_executable;
    std::string m_link_name;
    std::optional<std::string> m_category_name;
};

} // namespace ungive::update::operations