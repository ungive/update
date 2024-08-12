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

namespace ungive::update
{

// A downloader for files hosted on an HTTP server.
class http_downloader
{
public:
    http_downloader(std::string const& base_url)
    {
        if (base_url.rfind("https", 0) != 0) {
            throw std::invalid_argument("the base url must be a HTTPS url");
        }
        auto p = internal::split_host_path(base_url);
        m_host = p.first;
        m_base_path = p.second;
        while (m_base_path.size() > 0 && m_base_path.back() == '/') {
            m_base_path.resize(m_base_path.size() - 1);
        }
    }

    ~http_downloader()
    {
        // Delete all downloaded files once destructed.
        if (!m_temp_dir.empty()) {
            std::filesystem::remove_all(m_temp_dir);
        }
    }

    // Adds a verification step for each download that is made with get().
    template <typename V,
        typename std::enable_if<std::is_base_of<types::verifier_interface,
            V>::value>::type* = nullptr>
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
            if (!std::filesystem::create_directories(
                    output_file.parent_path())) {
                std::runtime_error("failed to create output directory");
            }
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

    std::filesystem::path m_temp_dir{};
    std::unordered_set<std::string> m_additional_files{};
    std::vector<types::verifier_func> m_verification_funcs{};
    std::unordered_map<std::string, downloaded_file> m_downloaded_files{};
};

struct github_api_latest_extractor : public types::latest_extractor_interface
{
    github_api_latest_extractor(std::regex release_filename_pattern)
        : m_release_filename_pattern{ release_filename_pattern }
    {
    }

    std::pair<version_number, file_url> operator()(
        downloaded_file const& file) override
    {
        const auto npos = std::string::npos;
        auto j = nlohmann::json::parse(file.read());
        std::string tag = j["tag_name"];
        auto v = tag.find('v', 0);
        auto a = tag.find('.', 1);
        auto b = a != npos ? tag.find('.', a + 1) : npos;
        if (v != 0 || a == npos || b == npos) {
            throw std::runtime_error("unexpected version tag format");
        }
        version_number version = {
            std::stoi(tag.substr(v + 1, a - (v + 1))),
            std::stoi(tag.substr(a + 1, b - (a + 1))),
            std::stoi(tag.substr(b + 1)),
        };
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

class github_api_latest_retriever : public types::latest_retriever_interface
{
public:
    github_api_latest_retriever(
        std::string const& username, std::string const& repository)
        : m_username{ username }, m_repository{ repository }
    {
    }

    file_url operator()(version_number const& current_version,
        std::regex filename_pattern) override
    {
        http_downloader api_downloader("https://api.github.com/repos/" +
            m_username + "/" + m_repository + "/releases/latest");
        auto release_info = api_downloader.get();
        github_api_latest_extractor extractor(filename_pattern);
        auto result = extractor(release_info);
        const auto npos = std::string::npos;
        if (result.second.url().rfind("https://github.com", 0) == npos) {
            throw std::runtime_error("the release url ist not a github url");
        }
        return result.second;
    }

private:
    std::string m_username;
    std::string m_repository;
};

} // namespace ungive::update

#endif // UNGIVE_LUBUPDATE_H_
