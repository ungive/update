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

#include "update/detail/common.h"
#include "update/detail/downloader.h"
#include "update/detail/github.h"
#include "update/detail/operations.h"
#include "update/detail/verifiers.h"
#include "update/internal/sentinel.h"
#include "update/internal/util.h"

#ifdef WIN32
#include "update/internal/zip.h"
#endif

namespace update
{

class updater
{
public:
    updater(std::filesystem::path const& working_directory,
        version_number const& current_version)
        : m_working_directory{ working_directory },
          m_current_version{ current_version },
          m_downloader{ std::make_shared<http_downloader>() }
    {
    }

    // Returns the working directory in which the update manager operates.
    std::filesystem::path const& working_directory() const
    {
        return m_working_directory;
    }

    // Returns the current version with which the updater was initialized.
    version_number const& current_version() const { return m_current_version; }

    // Set a source from which the latest version is retrieved.
    template <typename L,
        typename std::enable_if<std::is_base_of<
            internal::types::latest_retriever_interface, L>::value>::type* =
            nullptr>
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

    // Perform an update by retrieving the latest release.
    // This method is not thread-safe.
    update_result update()
    {
        if (!m_latest_retriever_func)
            throw std::runtime_error("missing latest retriever");
        if (m_download_filename_pattern.empty())
            throw std::runtime_error("missing download filename pattern");
        if (!m_download_url_pattern.has_value())
            throw std::runtime_error("missing download url pattern");

        std::regex filename_pattern(m_download_filename_pattern);
        auto [version, url] = m_latest_retriever_func(filename_pattern);
        if (version == m_current_version) {
            return update_result(update_status::up_to_date, version);
        }
        if (version < m_current_version) {
            return update_result(update_status::latest_is_older, version);
        }
        assert(std::regex_match(url.filename(), filename_pattern));
        auto url_pattern = m_download_url_pattern.value();
        if (!std::regex_match(url.url(), url_pattern)) {
            throw std::runtime_error("the download url pattern does not match");
        }
        m_downloader->base_url(url.base_url());
        auto latest_release = m_downloader->get(url.filename());
        auto output_directory = extract_archive(version, latest_release.path());
        return update_result(
            update_status::update_downloaded, version, output_directory);
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

    std::filesystem::path m_working_directory;
    version_number m_current_version;
    std::shared_ptr<http_downloader> m_downloader;

    update::archive_type m_archive_type{ archive_type::unknown };
    std::string m_download_filename_pattern{};
    std::optional<std::regex> m_download_url_pattern{};
    internal::types::latest_retriever_func m_latest_retriever_func{};
    std::vector<internal::types::post_update_operation_func>
        m_post_update_operations{};
};

} // namespace update

#endif // UNGIVE_UPDATE_UPDATER_H_
