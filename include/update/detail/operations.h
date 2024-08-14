#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "update/internal/types.h"
#include "update/internal/win/startmenu.h"
#include "update/internal/zip.h"

namespace update::operations
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

class create_start_menu_shortcut
    : public internal::types::post_update_operation_interface
{
public:
    // Installs a start menu shortcut to the target executable.
    // If the target executable path is a relative path,
    // the executable path is resolved to be within the directory
    // of the extracted and installed update.
    // The category name is optional and resembles a subfolder
    // in which the shortcut is placed.
    // Optionally, the start menu shortcut can only be updated,
    // if it exists, and otherwise not be created.
    create_start_menu_shortcut(std::filesystem::path const& target_executable,
        std::string const& link_name,
        std::optional<std::string> const& category_name = std::nullopt,
        bool only_update = false)
        : m_target_executable{ target_executable }, m_link_name{ link_name },
          m_category_name{ category_name }, m_only_update{ only_update }
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
        if (m_only_update &&
            !internal::win::has_start_menu_entry(
                target_executable, m_link_name, m_category_name.value_or(""))) {
            // Start menu entry should only be updated but does not exist.
            return;
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
    bool m_only_update;
};

class update_start_menu_shortcut : public create_start_menu_shortcut
{
public:
    // Works the same as create_start_menu_shortcut,
    // but only updates the shortcut if it already exists.
    update_start_menu_shortcut(std::filesystem::path const& target_executable,
        std::string const& link_name,
        std::optional<std::string> const& category_name = std::nullopt)
        : create_start_menu_shortcut(
              target_executable, link_name, category_name, true)
    {
    }
};

} // namespace update::operations
