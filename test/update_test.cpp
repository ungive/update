#include <iostream>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update.hpp"

using namespace ungive::update;

// Used test files:
// - https://ungive.github.io/update_test/github-api-mock/latest-simple.json
// Repository: https://github.com/ungive/update_test
// Test releases: https://github.com/ungive/update_test/releases

#ifdef WIN32
const char* UPDATE_WORKING_DIR =
    "C:\\Users\\Jonas\\AppData\\Local\\update_test_dir";
#endif

const char* PUBLIC_KEY = R"key(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAIcbwANvTnDDB6KqmrL64/jEApW41sA//feKQYQMjGeU=
-----END PUBLIC KEY-----
)key";
const char* BAD_PUBLIC_KEY = R"key(
-----BEGIN PUBLIC KEY-----
NCowBQYDK2VwAyEAIcbwANvTnDDB6KqmrL64/jEApW41sA//feKQYQMjGeU=
-----END PUBLIC KEY-----
)key";

struct downloader_inject : public http_downloader
{
    using http_downloader::http_downloader;

    void set_content(std::string const& path, std::string const& content)
    {
        auto full_path = cwd() / internal::random_string(8) /
            internal::strip_leading_slash(path);
        if (full_path.has_parent_path()) {
            std::filesystem::create_directories(full_path.parent_path());
        }
        std::ofstream out(full_path, std::ios::out | std::ios::binary);
        out.write(content.data(), content.size());
        out.flush();
        out.close();
        m_downloaded_files.emplace(path, downloaded_file(full_path.string()));
    }
};

TEST(split_host_path, SecondContainsPathWithLeadingSlashWhenPathIsPresent)
{
    auto p = internal::split_host_path("https://example.com/foo/bar");
    EXPECT_EQ("https://example.com", p.first);
    EXPECT_EQ("/foo/bar", p.second);
}

TEST(split_host_path, SecondIsEmptyWhenNoPathIsPresent)
{
    auto p = internal::split_host_path("https://example.com");
    EXPECT_EQ("https://example.com", p.first);
    EXPECT_EQ("", p.second);
}

TEST(split_host_path, SplitsCorrectlyWhenNoSchemeIsPresent)
{
    auto p = internal::split_host_path("example.com/foo");
    EXPECT_EQ("example.com", p.first);
    EXPECT_EQ("/foo", p.second);
}

TEST(http_downloader, CanPassUrlWithPathAsBaseUrl)
{
    http_downloader downloader("https://ungive.github.io");
    std::optional<downloaded_file> latest;
    // missing leading slash in path.
    // Test needed since httplib doesn't allow this out of the box.
    EXPECT_NO_THROW(latest = downloader.get(
                        "update_test/github-api-mock/latest-simple.json"));
    auto j = nlohmann::json::parse(latest->read());
    std::string tag = j["tag_name"];
    EXPECT_EQ("v1.2.3", tag);
}

TEST(http_downloader, CanPassPartOfPathInBaseUrl)
{
    // Part of the path is in the base URL.
    // Test needed since httplib doesn't allow this in the Client constructor.
    http_downloader downloader(
        "https://ungive.github.io/update_test/github-api-mock///");
    std::optional<downloaded_file> latest;
    EXPECT_NO_THROW(latest = downloader.get("/latest-simple.json"));
}

TEST(http_downloader, SavesDownloadedFilesToDisk)
{
    http_downloader downloader(
        "https://ungive.github.io/update_test/github-api-mock");
    std::optional<downloaded_file> latest;
    EXPECT_NO_THROW(latest = downloader.get("latest-simple.json"));
    EXPECT_TRUE(std::filesystem::exists(latest->path()));
}

TEST(http_downloader, DeletesDownloadedFilesWhenDownloaderIsDestroyed)
{
    std::string path;
    {
        http_downloader downloader(
            "https://ungive.github.io/update_test/github-api-mock");
        std::optional<downloaded_file> latest;
        EXPECT_NO_THROW(latest = downloader.get("latest-simple.json"));
        EXPECT_TRUE(std::filesystem::exists(latest->path()));
    }
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(http_downloader, SumsValidWhenVerifyingSha256Sums)
{
    http_downloader downloader("https://ungive.github.io/update_test");
    downloader.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    EXPECT_NO_THROW(downloader.get("release-1.2.3.txt"));
}

TEST(http_downloader, SumsValidWhenVerifyingSha256SumsWithDirectoryInPath)
{
    http_downloader downloader("https://ungive.github.io");
    downloader.add_verification(
        verifiers::sha256sums("update_test/SHA256SUMS.txt"));
    EXPECT_NO_THROW(downloader.get("update_test/release-1.2.3.txt"));
}

TEST(http_downloader, SignatureValidWhenVerifyingEd25519Signature)
{
    http_downloader downloader("https://ungive.github.io/update_test");
    downloader.add_verification(verifiers::message_digest(
        "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519", PUBLIC_KEY));
    EXPECT_NO_THROW(downloader.get("SHA256SUMS.txt"));
}

TEST(http_downloader, SignatureInvalidWhenVerifyingEd25519SignatureWithBadKey)
{
    http_downloader downloader("https://ungive.github.io/update_test");
    downloader.add_verification(verifiers::message_digest("SHA256SUMS.txt",
        "SHA256SUMS.txt.sig", "PEM", "ED25519", BAD_PUBLIC_KEY));
    EXPECT_ANY_THROW(downloader.get("SHA256SUMS.txt"));
}

TEST(http_downloader, SignatureInvalidWhenVerifyingBadEd25519Signature)
{
    http_downloader downloader("https://ungive.github.io/update_test");
    downloader.add_verification(verifiers::message_digest("SHA256SUMS.txt",
        "SHA256SUMS.txt.badsig", "PEM", "ED25519", PUBLIC_KEY));
    EXPECT_THROW(
        downloader.get("release-1.2.3.txt"), verifiers::verification_failed);
}

TEST(http_downloader, ValidatesWhenVerifyingSha256SumsAndEd25519Signature)
{
    http_downloader downloader("https://ungive.github.io/update_test");
    downloader.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    downloader.add_verification(verifiers::message_digest(
        "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519", PUBLIC_KEY));
    EXPECT_NO_THROW(downloader.get("release-1.2.3.txt"));
}

TEST(http_downloader, FailsWhenVerifyingSha256SumsWithMalformedContent)
{
    downloader_inject downloader("https://ungive.github.io/update_test");
    downloader.set_content("release-1.2.3.txt",
        "Release file for version 1.2.3 with malicious code");
    downloader.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    EXPECT_THROW(
        downloader.get("release-1.2.3.txt"), verifiers::verification_failed);
}

TEST(latest_extractor, YieldsLatestVersionWhenRequestingMockGitHubApi)
{
    http_downloader downloader(
        "https://ungive.github.io/update_test/github-api-mock");
    std::optional<downloaded_file> latest;
    EXPECT_NO_THROW(latest = downloader.get("latest-simple.json"));
    github_api_latest_extractor extractor(
        std::regex("^release-\\d+.\\d+.\\d+.txt$"));
    auto result = extractor(latest.value());
    EXPECT_EQ(version_number({ 1, 2, 3 }), result.first);
    EXPECT_EQ("https://github.com/ungive/update_test/releases/download/v1.2.3/"
              "release-1.2.3.txt",
        result.second.url());
    EXPECT_EQ("https://github.com/ungive/update_test/releases/download/v1.2.3/",
        result.second.base_url());
    EXPECT_EQ("release-1.2.3.txt", result.second.filename());
}

class mock_github_api_latest_retriever : public github_api_latest_retriever
{
public:
    mock_github_api_latest_retriever()
        : github_api_latest_retriever("ungive", "update_test")
    {
        inject_api_url("https://ungive.github.io/update_test/github-api-mock/"
                       "latest-simple.json");
    }
};

update_manager create_update_manager()
{
    update_manager manager(UPDATE_WORKING_DIR);
    manager.set_source(mock_github_api_latest_retriever());
    manager.download_filename_pattern("^release-\\d+.\\d+.\\d+-subfolder.zip$");
    manager.set_archive_type(archive_type::zip_archive);
    manager.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    manager.add_verification(verifiers::message_digest(
        "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519", PUBLIC_KEY));
    return manager;
}

TEST(update_manager, StatusIsUpToDateWhenVersionIsIdentical)
{
    auto current_version = version_number(1, 2, 3);
    update_manager manager = create_update_manager();
    auto result = manager.update(current_version);
    EXPECT_EQ(update_status::up_to_date, result.status);
}

TEST(update_manager, StatusIsLatestIsOlderWhenVersionIsLower)
{
    auto current_version = version_number(1, 3, 0);
    update_manager manager = create_update_manager();
    auto result = manager.update(current_version);
    EXPECT_EQ(update_status::latest_is_older, result.status);
}

TEST(update_manager, UpdateIsInstalledAndReadyWhenVersionIsOlder)
{
    auto current_version = version_number(1, 2, 2);
    update_manager manager = create_update_manager();
    std::filesystem::remove_all(manager.working_directory());
    EXPECT_EQ(std::nullopt, manager.latest_installed_version());
    auto result = manager.update(current_version);
    EXPECT_EQ(update_status::update_downloaded, result.status);
    ASSERT_TRUE(result.downloaded_directory.has_value());
    EXPECT_EQ(manager.working_directory() / result.version.string(),
        result.downloaded_directory);
    EXPECT_TRUE(std::filesystem::exists(result.downloaded_directory.value()));
    auto latest_installed = manager.latest_installed_version();
    ASSERT_TRUE(latest_installed.has_value());
    EXPECT_EQ(version_number(1, 2, 3), result.version);
    EXPECT_EQ(result.version, latest_installed->first);
    EXPECT_EQ(result.downloaded_directory, latest_installed->second);
}

TEST(update_manager, OldVersionIsDeletedAfterPruneIsCalled)
{
    auto current_version = version_number(1, 2, 2);
    auto expected_version = version_number(1, 2, 3);
    update_manager manager = create_update_manager();
    std::filesystem::remove_all(manager.working_directory());
    std::filesystem::create_directories(manager.working_directory());
    // Pretend there is an older version available.
    internal::touch_file(manager.working_directory() /
        current_version.string() / manager.sentinel_filename());
    auto latest_installed = manager.latest_installed_version();
    EXPECT_EQ(current_version, latest_installed->first);
    auto result = manager.update(current_version);
    latest_installed = manager.latest_installed_version();
    ASSERT_TRUE(latest_installed.has_value());
    EXPECT_EQ(expected_version, result.version);
    EXPECT_TRUE(std::filesystem::exists(manager.working_directory() /
        result.version.string() / manager.sentinel_filename()));
    // Note that this should only be done when:
    // 1. the application has checked for updates and there are none, OR
    // 2. the application has updated, restarted and is now running the update.
    // either way the newest and currently running version shoule be the same.
    manager.prune(expected_version.string());
    EXPECT_FALSE(std::filesystem::exists(manager.working_directory() /
        current_version.string() / manager.sentinel_filename()));
    EXPECT_TRUE(std::filesystem::exists(manager.working_directory() /
        result.version.string() / manager.sentinel_filename()));
}
