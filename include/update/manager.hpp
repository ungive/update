#ifndef UNGIVE_UPDATE_MANAGER_H_
#define UNGIVE_UPDATE_MANAGER_H_

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "update/detail/common.h"

namespace update
{

class manager
{
public:
    manager(std::filesystem::path const& working_directory,
        version_number const& current_version)
        : m_working_directory{ working_directory },
          m_current_version{ current_version }
    {
    }

    // Returns the latest installed version in the manager's working directory.
    std::optional<std::pair<version_number, std::filesystem::path>>
    latest_available_version() const
    {
        std::filesystem::create_directories(m_working_directory);
        auto it = std::filesystem::directory_iterator(m_working_directory);
        std::optional<std::pair<version_number, std::filesystem::path>> result;
        for (auto const& entry : it) {
            if (!entry.is_directory() || !entry.path().has_filename()) {
                continue;
            }
            if (!std::filesystem::exists(entry.path() / sentinel_filename())) {
                continue;
            }
            auto filename = entry.path().filename().string();
            version_number version;
            try {
                version = version_number::from_string(filename);
            }
            catch (...) {
                continue;
            }
            if (result.has_value() && version == result->first) {
                // two directories represent the same version,
                // e.g. "2.1" and "2.1.0". this should not happen in practice,
                // but if it does, simply return nothing,
                // so that the caller clears and redownloads the newest version,
                // as the working directory is in an inconsistent state.
                return std::nullopt;
            }
            if (!result.has_value() || version > result->first) {
                result = std::make_pair(version, entry.path());
            }
        }
        return result;
    }

    // Prunes all files in the manager's working directory
    // except the subdirectory for the given current version
    // and the subdirectory for the newest installed version,
    // as indicated by the return value of latest_available_version().
    void prune() const
    {
        std::unordered_set<std::filesystem::path> exclude_directories;
        exclude_directories.insert(m_current_version.string());
        auto latest_installed = latest_available_version();
        if (latest_installed.has_value()) {
            exclude_directories.insert(latest_installed->first.string());
        }
        std::filesystem::create_directories(m_working_directory);
        auto it = std::filesystem::directory_iterator(m_working_directory);
        std::optional<std::pair<version_number, std::filesystem::path>> result;
        for (auto const& entry : it) {
            if (!entry.is_directory() || !entry.path().has_filename()) {
                std::filesystem::remove_all(entry.path());
                continue;
            }
            auto filename = entry.path().filename().string();
            if (exclude_directories.find(filename) !=
                exclude_directories.end()) {
                continue;
            }
            std::filesystem::remove_all(entry.path());
        }
    }

private:
    std::filesystem::path m_working_directory;
    version_number m_current_version;
};

} // namespace update

#endif // UNGIVE_UPDATE_MANAGER_H_
