#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <yhirose/httplib.h>

#include "ungive/update/internal/types.h"
#include "ungive/update/internal/util.h"

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
            // Make sure the destructor does not throw.
            try {
                std::filesystem::remove_all(m_temp_dir);
            }
            catch (...) {
            }
        }
    }

    std::string const& base_url() const { return m_base_url; }

    void base_url(std::string const& base_url)
    {
        m_base_url = base_url;
        auto p = this->split_url(base_url);
        m_host = p.first;
        m_base_path = p.second;
    }

    // Adds a verification step for each download that is made with get().
    template <typename V,
        typename std::enable_if<
            std::is_base_of<types::verifier, V>::value>::type* = nullptr>
    void add_verification(V const& verifier)
    {
        m_verification_funcs.push_back(verifier);
        for (auto const& file : verifier.files()) {
            m_additional_files.insert(file);
        }
    }

    void override_file_url(std::string const& filename, std::string const& url)
    {
        m_file_url_overrides[filename] = url;
    }

    // Downloads the given path from the server to disk.
    // The path is appended to the base URL that was passed in the constructor.
    // Any verification steps that were added before the invocation of get()
    // are executed in the order they were added.
    // Fails if any of the verification steps failed
    // or if any of the files could not be stored on disk.
    // Returns the downloaded file.
    // This method is not thread-safe.
    downloaded_file get(std::string const& path = "")
    {
        if (m_base_url.empty()) {
            throw std::runtime_error("downloader base url cannot be empty");
        }
        httplib::Client cli(m_host);
        // Always follow redirects.
        cli.set_follow_location(true);
        // Get the additional files first, as they are usually smaller
        // and faster to download. If one of them does not exist on the remote,
        // the download operation will fail sooner and we won't
        // have unnecessarily downloaded a possibly large file.
        for (auto const& additional_path : m_additional_files) {
            auto it = m_file_url_overrides.find(additional_path);
            if (it != m_file_url_overrides.end()) {
                get_external_file(additional_path, it->second);
            } else {
                get_file(cli, additional_path);
            }
        }
        auto result = get_file(cli, path);
        for (auto const& verify : m_verification_funcs) {
            verify(types::verification_payload(path, m_downloaded_files));
        }
        return result;
    }

    // Sets the cancellation state for any current or future downloads.
    // Must be manually reset if downloading should not be cancelled anymore.
    // Returns the old state value.
    // This method is thread-safe.
    bool cancel(bool state) { return m_cancel_all.exchange(state); }

    // Reads the current cancellation state.
    // This method is thread-safe.
    bool cancel() const { return m_cancel_all.load(); }

protected:
    // Downloads a file once and returns the local path to it.
    // If the file is already downloaded it returns the path to it instead.
    downloaded_file const& get_file(httplib::Client& cli,
        std::string const& filename, std::string const& path)
    {
        auto it = m_downloaded_files.find(filename);
        if (it != m_downloaded_files.end()) {
            return it->second;
        }
        auto local_path = cwd() / internal::random_string(8);
        if (filename.size() > 0) {
            local_path = local_path / internal::strip_leading_slash(filename);
        }
        download_to_file(cli, path, local_path);
        m_downloaded_files.emplace(filename, downloaded_file(local_path));
        return m_downloaded_files.at(filename);
    }

    downloaded_file const& get_file(
        httplib::Client& cli, std::string const& filename)
    {
        auto path = filename;
        if (m_base_path != "/") {
            path = internal::ensure_nonempty_prefix(path, '/');
        }
        return get_file(cli, filename, m_base_path + path);
    }

    downloaded_file const& get_external_file(
        std::string const& filename, std::string const& external_url)
    {
        auto [external_host, external_path] = this->split_url(external_url);
        httplib::Client cli(external_host);
        return get_file(cli, filename, external_path);
    }

    // Downloads a path and saves it in the given output file.
    void download_to_file(httplib::Client& cli, std::string path,
        std::filesystem::path const& output_file)
    {
        if (output_file.has_parent_path()) {
            std::filesystem::create_directories(output_file.parent_path());
        }
        std::ofstream out(output_file, std::ios::out | std::ios::binary);
        auto res = cli.Get(
            internal::ensure_nonempty_prefix(path, '/'), httplib::Headers(),
            [&](const httplib::Response& response) {
                if (m_cancel_all.load()) {
                    return false;
                }
                return response.status == httplib::StatusCode::OK_200;
            },
            [&](const char* data, size_t data_length) {
                if (m_cancel_all.load()) {
                    return false;
                }
                out.write(data, data_length);
                return !out.fail();
            });
        if (!res) {
            auto err = res.error();
            throw std::runtime_error("failed to download " + m_host + path +
                ": " + httplib::to_string(err));
        }
    }

    inline std::filesystem::path cwd()
    {
        if (m_temp_dir.empty()) {
            m_temp_dir = internal::create_temporary_directory();
        }
        return m_temp_dir;
    }

    std::pair<std::string, std::string> split_url(std::string const& url)
    {
#ifdef LIBUPDATE_ALLOW_INSECURE_HTTP
        if (url.rfind("http", 0) != 0) {
            throw std::invalid_argument("the base url must be an HTTP url");
        }
#else
        if (url.rfind("https", 0) != 0) {
            throw std::invalid_argument("the base url must be an HTTPS url");
        }
#endif
        auto p = internal::split_host_path(url);
        auto host = p.first;
        auto path = p.second;
        // Checking greater 1, since we don't want to remove the leading slash.
        while (path.size() > 1 && path.back() == '/') {
            path.resize(path.size() - 1);
        }
        return std::make_pair(host, path);
    }

    std::string m_host;
    std::string m_base_path;
    std::string m_base_url;

    std::filesystem::path m_temp_dir{};
    std::unordered_set<std::string> m_additional_files{};
    std::vector<internal::types::verifier_func> m_verification_funcs{};
    std::unordered_map<std::string, downloaded_file> m_downloaded_files{};
    std::atomic<bool> m_cancel_all{ false };
    std::unordered_map<std::string, std::string> m_file_url_overrides;
};

} // namespace ungive::update
