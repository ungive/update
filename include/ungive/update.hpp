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

#include <nlohmann/json.hpp>
#include <yhirose/httplib.h>

#include "detail/common.h"
#include "detail/util.h"
#include "detail/verifiers.h"

#ifdef WIN32
#include "detail/win/startmenu.h"
#include "detail/zip.h"
#endif

namespace ungive::update
{

// A downloader for files hosted on an HTTP server.
class http_downloader
{
public:
    http_downloader() = default;

    http_downloader(std::string const& base_url) { this->base_url(base_url); }

    ~http_downloader()
    {
        // Delete all downloaded files once destructed.
        if (!m_temp_dir.empty()) {
            std::filesystem::remove_all(m_temp_dir);
        }
    }

    std::string const& base_url() const { return m_base_url; }

    void base_url(std::string const& base_url)
    {
        m_host = "";
        m_base_path = "";
        m_base_url = "";
        if (base_url.rfind("https", 0) != 0) {
            throw std::invalid_argument("the base url must be a HTTPS url");
        }
        m_base_url = base_url;
        auto p = internal::split_host_path(base_url);
        m_host = p.first;
        m_base_path = p.second;
        while (m_base_path.size() > 0 && m_base_path.back() == '/') {
            m_base_path.resize(m_base_path.size() - 1);
        }
    }

    // Adds a verification step for each download that is made with get().
    template <typename V,
        typename std::enable_if<std::is_base_of<
            internal::types::verifier_interface, V>::value>::type* = nullptr>
    void add_verification(V const& verifier)
    {
        m_verification_funcs.push_back(verifier);
        for (auto const& file : verifier.files()) {
            m_additional_files.insert(file);
        }
    }

    // Downloads the given path from the server to disk.
    // The path is appended to the base URL that was passed in the constructor.
    // Any verification steps that were added before the invocation of get()
    // are executed in the order they were added.
    // Fails if any of the verification steps failed
    // or if any of the files could not be stored on disk.
    // Returns the downloaded file.
    downloaded_file get(std::string const& path = "")
    {
        if (m_base_url.empty()) {
            throw std::runtime_error("downloader base url cannot be empty");
        }
        httplib::Client cli(m_host);
        // Always follow redirects.
        cli.set_follow_location(true);
        auto result = get_file(cli, path);
        for (auto const& additional_path : m_additional_files) {
            get_file(cli, additional_path);
        }
        for (auto const& verify : m_verification_funcs) {
            if (!verify(path, m_downloaded_files)) {
                std::runtime_error("verification failed");
            }
        }
        return result;
    }

protected:
    // Downloads a file once and returns the local path to it.
    // If the file is already downloaded it returns the path to it instead.
    downloaded_file const& get_file(httplib::Client& cli, std::string path)
    {
        auto it = m_downloaded_files.find(path);
        if (it != m_downloaded_files.end()) {
            return it->second;
        }
        auto full_path = cwd() / internal::random_string(8);
        if (path.size() > 0) {
            full_path = full_path / internal::strip_leading_slash(path);
        }
        download_to_file(cli, path, full_path);
        m_downloaded_files.emplace(path, downloaded_file(full_path.string()));
        return m_downloaded_files.at(path);
    }

    // Downloads a path and saves it in the given output file.
    void download_to_file(httplib::Client& cli, std::string path,
        std::filesystem::path const& output_file)
    {
        if (output_file.has_parent_path()) {
            std::filesystem::create_directories(output_file.parent_path());
        }
        std::ofstream out(output_file, std::ios::out | std::ios::binary);
        if (path.size() > 0 && path.at(0) != '/') {
            path = '/' + path;
        }
        auto res = cli.Get(
            m_base_path + path, httplib::Headers(),
            [&](const httplib::Response& response) {
                return response.status == httplib::StatusCode::OK_200;
            },
            [&](const char* data, size_t data_length) {
                out.write(data, data_length);
                return !out.fail();
            });
        if (!res) {
            auto err = res.error();
            throw std::runtime_error(
                "failed to download file: " + httplib::to_string(err));
        }
    }

    inline std::filesystem::path cwd()
    {
        if (m_temp_dir.empty()) {
            m_temp_dir = internal::create_temporary_directory();
        }
        return m_temp_dir;
    }

    std::string m_host;
    std::string m_base_path;
    std::string m_base_url;

    std::filesystem::path m_temp_dir{};
    std::unordered_set<std::string> m_additional_files{};
    std::vector<internal::types::verifier_func> m_verification_funcs{};
    std::unordered_map<std::string, downloaded_file> m_downloaded_files{};
};

struct github_api_latest_extractor
    : public internal::types::latest_extractor_interface
{
    github_api_latest_extractor(std::regex release_filename_pattern)
        : m_release_filename_pattern{ release_filename_pattern }
    {
    }

    std::pair<version_number, file_url> operator()(
        downloaded_file const& file) const override
    {
        const auto npos = std::string::npos;
        auto j = nlohmann::json::parse(file.read());
        std::string tag = j["tag_name"];
        auto version = version_number::from_string(tag, "v");
        std::string url{};
        for (auto const& asset : j["assets"]) {
            std::string name = asset["name"];
            std::smatch match;
            if (std::regex_match(name, match, m_release_filename_pattern)) {
                url = asset["browser_download_url"];
            }
        }
        if (url.empty()) {
            throw std::runtime_error("could not find any matching asset");
        }
        return std::make_pair(version, url);
    }

private:
    std::regex m_release_filename_pattern;
};

class github_api_latest_retriever
    : public internal::types::latest_retriever_interface
{
public:
    github_api_latest_retriever(
        std::string const& username, std::string const& repository)
        : m_username{ username }, m_repository{ repository }
    {
    }

    std::pair<version_number, file_url> operator()(
        std::regex filename_pattern) const override
    {
        const auto url = "https://api.github.com/repos/" + m_username + "/" +
            m_repository + "/releases/latest";
#ifdef TEST_BUILD
        http_downloader api_downloader(m_injected_api_url.value_or(url));
#else
        http_downloader api_downloader(url);
#endif
        auto release_info = api_downloader.get();
        github_api_latest_extractor extractor(filename_pattern);
        auto result = extractor(release_info);
        const auto npos = std::string::npos;
        if (result.second.url().rfind("https://github.com", 0) == npos) {
            throw std::runtime_error("the release url ist not a github url");
        }
        return result;
    }

    inline std::regex url_pattern() const override
    {
        return std::regex("^https://github.com/" + m_username + "/" +
            m_repository + "/releases/download/.*");
    }

protected:
#ifdef TEST_BUILD
    inline void inject_api_url(std::string const& url)
    {
        m_injected_api_url = url;
    }

    std::optional<std::string> m_injected_api_url;
#endif

private:
    std::string m_username;
    std::string m_repository;
};

#ifdef WIN32
struct platform_options
{
};
#endif

enum class update_status
{
    up_to_date,
    latest_is_older,
    update_downloaded,
};

struct update_result
{
    update_result(update_status status, version_number const& version)
        : status{ status }, version{ version }
    {
        assert(status != update_status::update_downloaded);
    }

    update_result(update_status status, version_number const& version,
        std::filesystem::path downloaded_directory)
        : status{ status }, version{ version },
          downloaded_directory{ downloaded_directory }
    {
        assert(status == update_status::update_downloaded);
    }

    update_status status;
    version_number version;
    std::optional<std::filesystem::path> downloaded_directory{};
};

enum class archive_type
{
    unknown,
    zip_archive,
    mac_disk_image,
};

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
            if (!std::filesystem::exists(entry.path() / SENTINEL_FILENAME)) {
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
    // except the given subdirectory, which should be the subdirectory
    // of the currently running version (which shouldn't be deleted).
    // That version and the newest version should be confirmed to be identical
    // before calling this method.
    void prune(std::string const& except_directory) const
    {
        std::filesystem::create_directories(m_working_directory);
        auto it = std::filesystem::directory_iterator(m_working_directory);
        std::optional<std::pair<version_number, std::filesystem::path>> result;
        for (auto const& entry : it) {
            if (!entry.is_directory() || !entry.path().has_filename()) {
                std::filesystem::remove_all(entry.path());
                continue;
            }
            auto filename = entry.path().filename().string();
            if (filename == except_directory) {
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
    const char* sentinel_filename() const { return SENTINEL_FILENAME; }

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
            internal::flatten_root_directory(output_directory.string());
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
        internal::touch_file(directory / SENTINEL_FILENAME);
    }

    const char* SENTINEL_FILENAME = ".complete";

    std::filesystem::path m_working_directory;

    archive_type m_archive_type{ archive_type::unknown };
    std::string m_download_filename_pattern{};
    std::optional<std::regex> m_download_url_pattern{};
    internal::types::latest_retriever_func m_latest_retriever_func{};
    http_downloader m_downloader{};
};

// inline void test_mac()
// {
//     auto public_key = "...";

//     update_manager manager(
//         "/Users/Jonas/Library/Application Support/update_pwd");
//     manager.download_filename_pattern("^release-\\d+.\\d+.\\d+.dmg$");
//     manager.download_url_pattern(
//         "^https://github.com/ungive/update_test/releases/download");
//     manager.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
//     manager.add_verification(verifiers::message_digest(
//         "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519",
//         public_key));
// }

// template <typename L,
//     typename
//     std::enable_if<std::is_base_of<types::latest_retriever_interface,
//         L>::value>::type* = nullptr>
// class updater
// {
// public:
//     updater(L const& latest_retriever, std::string const& filename_pattern)
//         : m_latest_retriever{ latest_retriever },
//           m_filename_pattern{ filename_pattern }
//     {
//     }

//     // Verify that an update file's URL matches this pattern.
//     inline void url_pattern(std::string const& pattern)
//     {
//         m_url_pattern = pattern;
//     }

//     // Add verification steps for downloaded update files.
//     template <typename V,
//         typename std::enable_if<std::is_base_of<types::verifier_interface,
//             V>::value>::type* = nullptr>
//     void add_verification(V const& verifier)
//     {
//         m_verification_funcs.push_back(verifier);
//         for (auto const& file : verifier.files()) {
//             m_additional_files.insert(file);
//         }
//     }

//     // Download the latest update
//     std::pair<version_number, std::filesystem::path> download_latest(
//         version_number const& current_version)
//     {
//         m_latest_retriever(m_filename_pattern);
//     }

// private:
//     L m_latest_retriever;
//     std::string m_filename_pattern;

//     std::string m_url_pattern{};
//     std::vector<types::verifier_func> m_verification_funcs{};
// };

} // namespace ungive::update

#endif // UNGIVE_LUBUPDATE_H_
