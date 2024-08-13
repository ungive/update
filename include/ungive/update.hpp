#ifndef UNGIVE_LIBUPDATE_H_
#define UNGIVE_LUBUPDATE_H_

#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ungive/detail/common.h"
#include "ungive/detail/downloader.h"
#include "ungive/detail/github.h"
#include "ungive/detail/operations.h"
#include "ungive/detail/verifiers.h"
#include "ungive/internal/util.h"

#ifdef WIN32
#include "ungive/internal/zip.h"
#endif

#define UNGIVE_UPDATE_SENTINEL ".complete"

namespace ungive::update
{

class update_manager
{
public:
    update_manager(std::string const& working_directory)
        : m_working_directory{ working_directory }
    {
    }

    // Returns the working directory in which the update manager operates.
    std::filesystem::path const& working_directory() const
    {
        return m_working_directory;
    }

    // Set a source from which the latest version is retrieved.
    template <typename L,
        typename std::enable_if<std::is_base_of<
            internal::types::latest_retriever_interface, L>::value>::type* =
            nullptr>
    inline void set_source(L const& latest_retriever)
    {
        m_latest_retriever_func = latest_retriever;
        if (!m_download_url_pattern.has_value()) {
            m_download_url_pattern = latest_retriever.url_pattern();
        }
    }

    // Set the archive type to indicate what extraction algorithm to use.
    inline void set_archive_type(archive_type type) { m_archive_type = type; }

    // Set the filename pattern to specify which file to download.
    inline void download_filename_pattern(std::string const& pattern)
    {
        m_download_filename_pattern = pattern;
    }

    // Overwrite the download url pattern supplied by the latest retriever,
    // to verify that any download is made from the correct domain and path.
    // Usually this does not need to be set manually,
    // as the latest retriever should supply this pattern already.
    inline void download_url_pattern(std::string const& pattern)
    {
        m_download_url_pattern = pattern;
    }

    // Add any number of verification steps for downloaded update files.
    template <typename V,
        typename std::enable_if<std::is_base_of<
            internal::types::verifier_interface, V>::value>::type* = nullptr>
    inline void add_verification(V const& verifier)
    {
        m_downloader.add_verification(verifier);
    }

    // Add any number of post-update operations to perform after an update.
    template <typename O,
        typename std::enable_if<
            std::is_base_of<internal::types::post_update_operation_interface,
                O>::value>::type* = nullptr>
    void add_post_update_operation(O const& operation)
    {
        m_post_update_operations.push_back(operation);
    }

    // Returns the latest installed version in the manager's working directory.
    std::optional<std::pair<version_number, std::filesystem::path>>
    latest_installed_version() const
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
    // as indicated by the return value of latest_installed_version().
    void prune(version_number const& current_version) const
    {
        std::unordered_set<std::filesystem::path> exclude_directories;
        exclude_directories.insert(current_version.string());
        auto latest_installed = latest_installed_version();
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

    // Perform an update by retrieving the latest release.
    update_result update(version_number const& current_version)
    {
        if (!m_latest_retriever_func)
            throw std::runtime_error("missing latest retriever");
        if (m_download_filename_pattern.empty())
            throw std::runtime_error("missing download filename pattern");
        if (!m_download_url_pattern.has_value())
            throw std::runtime_error("missing download url pattern");

        std::regex filename_pattern(m_download_filename_pattern);
        auto [version, url] = m_latest_retriever_func(filename_pattern);
        if (version == current_version) {
            return update_result(update_status::up_to_date, version);
        }
        if (version < current_version) {
            return update_result(update_status::latest_is_older, version);
        }
        assert(std::regex_match(url.filename(), filename_pattern));
        auto url_pattern = m_download_url_pattern.value();
        if (!std::regex_match(url.url(), url_pattern)) {
            throw std::runtime_error("the download url pattern does not match");
        }
        m_downloader.base_url(url.base_url());
        auto latest_release = m_downloader.get(url.filename());
        auto output_directory = extract_archive(version, latest_release.path());
        return update_result(
            update_status::update_downloaded, version, output_directory);
    }

    // Returns the name of the file that must be present in the root
    // of an extracted update's directory for it to be considered usable.
    static inline const char* sentinel_filename()
    {
        return UNGIVE_UPDATE_SENTINEL;
    }

private:
    std::filesystem::path extract_archive(
        version_number const& version, std::string const& archive_path) const
    {
        auto output_directory = m_working_directory / version.string();
        if (std::filesystem::exists(output_directory)) {
            std::filesystem::remove_all(output_directory);
        }
        if (std::filesystem::exists(output_directory)) {
            throw std::runtime_error("update directory could not be cleared");
        }
        std::filesystem::create_directories(output_directory);
        switch (m_archive_type) {
#ifdef WIN32
        case archive_type::zip_archive:
            internal::zip_extract(archive_path, output_directory.string());
            for (auto const& operation : m_post_update_operations) {
                try {
                    operation(output_directory);
                }
                catch (std::exception const& e) {
                    throw std::runtime_error(
                        std::string("post-update operation failed: ") +
                        e.what());
                }
            }
            create_sentinel_file(output_directory);
            break;
#endif
        default:
            throw std::runtime_error("archive type not supported yet");
        }
        return output_directory;
    }

    inline void create_sentinel_file(std::filesystem::path directory) const
    {
        internal::touch_file(directory / sentinel_filename());
    }

    std::filesystem::path m_working_directory;

    archive_type m_archive_type{ archive_type::unknown };
    std::string m_download_filename_pattern{};
    std::optional<std::regex> m_download_url_pattern{};
    internal::types::latest_retriever_func m_latest_retriever_func{};
    std::vector<internal::types::post_update_operation_func>
        m_post_update_operations{};
    http_downloader m_downloader{};
};

} // namespace ungive::update

#undef UNGIVE_UPDATE_SENTINEL

#endif // UNGIVE_LUBUPDATE_H_
