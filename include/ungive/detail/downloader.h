#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <yhirose/httplib.h>

#include "ungive/internal/types.h"
#include "ungive/internal/util.h"

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

} // namespace ungive::update
