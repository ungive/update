#ifndef UNGIVE_UPDATE_UPDATER_H_
#define UNGIVE_UPDATE_UPDATER_H_

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ungive/update/detail/common.h"
#include "ungive/update/detail/downloader.h"
#include "ungive/update/detail/github.h"
#include "ungive/update/detail/operations.h"
#include "ungive/update/detail/types.h"
#include "ungive/update/detail/verifiers.h"
#include "ungive/update/internal/sentinel.h"
#include "ungive/update/internal/util.h"
#include "ungive/update/manager.hpp"

#ifdef WIN32
#include "ungive/update/internal/zip.h"
#endif

namespace ungive::update::internal
{

inline std::regex filename_contains_version_pattern(
    std::string const& version_string)
{
    // The filename must contain the version string, e.g. "1.2.3"
    // and there may not be any additional digits
    // or periods followed or preceded by digits
    // to the left or right of the expected version string,
    // otherwise a version like "12.2.3" or "1.2.3.4" would be valid.

    return std::regex(R"((^|^[^0-9]|[^0-9]\.|[^.0-9]))" + version_string +
        R"(([^.0-9]|\.[^0-9]|[^0-9]$|$))");
}

} // namespace ungive::update::internal

namespace ungive::update
{

class updater
{
public:
    // Creates an updater from the given manager.
    updater(std::shared_ptr<manager> manager)
        : m_manager{ manager },
          m_downloader{ std::make_shared<http_downloader>() }
    {
    }

    // Returns the manager associated with this updater.
    std::shared_ptr<ungive::update::manager> manager() { return m_manager; }

    // Returns the manager associated with this updater.
    std::shared_ptr<const ungive::update::manager> manager() const
    {
        return m_manager;
    }

    // Returns the working directory in which the updater operates.
    std::filesystem::path const& working_directory() const
    {
        return m_manager->working_directory();
    }

    // Returns the current version with which the updater was initialized.
    version_number const& current_version() const
    {
        return m_manager->current_version();
    }

    // Set a source from which the latest version is retrieved.
    template <typename L,
        typename std::enable_if<std::is_base_of<types::latest_retriever,
            L>::value>::type* = nullptr>
    inline void update_source(L const& latest_retriever)
    {
        m_latest_retriever_func = latest_retriever;
        if (!m_download_url_pattern.has_value()) {
            m_download_url_pattern = latest_retriever.url_pattern();
        }
    }

    // Set the archive type to indicate what extraction algorithm to use.
    inline void archive_type(update::archive_type type)
    {
        m_archive_type = type;
    }

    // Sets whether the filename must contain the version number.
    //
    // If set, verifies that the update file yielded by a latest retriever
    // actually resembles the version that the latest retriever claims it is.
    // When hash verification is used in conjunction with a message digest,
    // then the filename is authenticated and therefore the version too.
    // This should always be used with hash and message digest verification.
    //
    // It is assumed that a downloaded file resembles a specific version
    // if the name of that file contains the respective version string.
    //
    inline void filename_contains_version(bool state)
    {
        m_filename_contains_version = state;
    }

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
        typename std::enable_if<
            std::is_base_of<types::verifier, V>::value>::type* = nullptr>
    inline void add_update_verification(V const& verifier)
    {
        m_downloader->add_verification(verifier);
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

    // Perform an update by retrieving the latest version and downloading it.
    // Returns the directory to which the update has been extracted.
    std::filesystem::path update()
    {
        auto latest = get_latest();
        switch (latest.state()) {
        case state::new_version_available:
            return update(latest.version(), latest.url());
        case state::up_to_date:
            throw std::runtime_error("the application is already up to date");
        case state::latest_is_older:
            throw std::runtime_error(
                "the latest version is older than the current version");
        default:
            assert(false);
            throw std::runtime_error("unknown state");
        }
    }

    // Perform an update by downloading and extracting from the given URL.
    // Returns the directory to which the update has been extracted.
    // This method is not thread-safe.
    std::filesystem::path update(
        version_number const& version, file_url const& url)
    {
        check_url(url, version);
        m_downloader->base_url(url.base_url());
        auto latest_release = m_downloader->get(url.filename());
        return extract_archive(version, latest_release.path());
    }

    // Perform an update using the return value from get_latest() directly.
    // Returns the directory to which the update has been extracted.
    // This method is not thread-safe.
    std::filesystem::path update(update_info const& info)
    {
        return update(info.version(), info.url());
    }

    // Returns update information for the latest available version
    // using the configured update source.
    // Pass the returned value to update() to download the update.
    update_info get_latest()
    {
        if (!m_latest_retriever_func)
            throw std::runtime_error("missing latest retriever");
        if (m_download_filename_pattern.empty())
            throw std::runtime_error("missing download filename pattern");
        if (!m_download_url_pattern.has_value())
            throw std::runtime_error("missing download url pattern");

        // Validate the URL.
        std::regex filename_pattern(m_download_filename_pattern);
        auto [version, url] = m_latest_retriever_func(filename_pattern);
        check_url(url, version);

        // Check if the latest installed version is the new version,
        // such that we don't install the latest update again.
        auto latest_installed = m_manager->latest_available_update();
        if (latest_installed.has_value() &&
            version == latest_installed->first) {
            return update_info(state::update_already_installed, version, url);
        }
        // Otherwise compare it to the current version.
        if (version == m_manager->current_version()) {
            return update_info(state::up_to_date, version, url);
        }
        if (version < m_manager->current_version()) {
            return update_info(state::latest_is_older, version, url);
        }
        return update_info(state::new_version_available, version, url);
    }

    // Sets the cancellation state for any current or future updates.
    // Must be manually reset if updates should not be cancelled anymore.
    // Returns the old state value.
    // This method is thread-safe.
    bool cancel(bool state) { return m_downloader->cancel(state); }

    // Reads the current cancellation state.
    // This method is thread-safe.
    bool cancel() const { return m_downloader->cancel(); }

private:
    void check_url(file_url const& url, version_number const& version)
    {
        if (m_filename_contains_version.has_value()) {
            if (m_filename_contains_version.value()) {
                verify_filename_contains_version(url.filename(), version);
            }
        } else {
            // This setting must be set explicitly by the user.
            throw std::runtime_error(
                "missing whether the filename should contain the version");
        }
        if (m_download_url_pattern.has_value()) {
            std::regex filename_pattern(m_download_filename_pattern);
            if (!std::regex_match(url.filename(), filename_pattern)) {
                throw std::runtime_error(
                    "the download filename pattern does not match");
            }
        }
        if (m_download_url_pattern.has_value()) {
            auto url_pattern = m_download_url_pattern.value();
            if (!std::regex_match(url.url(), url_pattern)) {
                throw std::runtime_error(
                    "the download url pattern does not match");
            }
        }
    }

    void verify_filename_contains_version(
        std::string const& filename, version_number const& version)
    {
        auto expected = version.string();
        auto pattern = internal::filename_contains_version_pattern(expected);
        if (!internal::regex_contains(filename, pattern)) {
            throw std::runtime_error(
                "the filename does not contain the correct version " +
                expected + ": " + filename);
        }
    }

    std::filesystem::path extract_archive(
        version_number const& version, std::string const& archive_path) const
    {
        auto output_directory =
            m_manager->working_directory() / version.string();
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
            create_sentinel_file(output_directory, version);
            break;
#endif
        default:
            throw std::runtime_error("archive type not supported yet");
        }
        return output_directory;
    }

    inline void create_sentinel_file(
        std::filesystem::path directory, version_number const& version) const
    {
        internal::sentinel sentinel(directory);
        sentinel.version(version);
        sentinel.write();
    }

    std::shared_ptr<ungive::update::manager> m_manager;
    std::shared_ptr<http_downloader> m_downloader;

    update::archive_type m_archive_type{ archive_type::unknown };
    std::string m_download_filename_pattern{};
    std::optional<std::regex> m_download_url_pattern{};
    internal::types::latest_retriever_func m_latest_retriever_func{};
    std::vector<internal::types::post_update_operation_func>
        m_post_update_operations{};
    std::optional<bool> m_filename_contains_version{};
};

} // namespace ungive::update

#endif // UNGIVE_UPDATE_UPDATER_H_
