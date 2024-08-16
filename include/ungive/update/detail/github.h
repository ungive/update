#pragma once

#include <regex>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ungive/update/detail/common.h"
#include "ungive/update/internal/types.h"

namespace ungive::update
{
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

} // namespace ungive::update
