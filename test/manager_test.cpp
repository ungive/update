#include <iostream>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update/updater.hpp"

using namespace update;

// Used test files:
// - https://ungive.github.io/update_test/github-api-mock/latest-simple.json
// Repository: https://github.com/ungive/update_test
// Test releases: https://github.com/ungive/update_test/releases

#ifdef WIN32
const char* UPDATE_WORKING_DIR =
    "C:\\Users\\Jonas\\AppData\\Local\\ungive_update_test_dir";
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

TEST(latest_retriever, YieldsLatestVersionWhenUsingOldGitHubUsername)
{
    // This leads to a redirect with the GitHub API, make sure that works!
    // A user should be able to rename their GitHub account
    // and still be able to ship new updates to old versions of the application
    // which still have the older username hardcoded.
    const auto old_username = "jonasberge";
    const auto current_username = "ungive";
    github_api_latest_retriever latest(old_username, "discord-music-presence");
    auto latest_release = latest(std::regex("^.*$"));
    EXPECT_EQ(3, latest_release.first.size());
    EXPECT_NE(
        std::string::npos, latest_release.second.url().find(current_username));
}

TEST(latest_retriever, YieldsLatestVersionWhenUsingOldRepositoryName)
{
    // This leads to a redirect with the GitHub API, make sure that works!
    // A user should be able to rename their GitHub repository
    // and still be able to ship new updates to old versions of the application
    // which still have the older repository name hardcoded.
    const auto old_username = "jonasberge";
    const auto old_repository_name = "TIDAL-Discord-Rich-Presence-UNOFFICIAL";
    const auto current_username = "ungive";
    const auto current_repository_name = "discord-music-presence";
    github_api_latest_retriever latest(old_username, old_repository_name);
    auto latest_release = latest(std::regex("^.*$"));
    EXPECT_EQ(3, latest_release.first.size());
    EXPECT_NE(std::string::npos,
        latest_release.second.url().find(
            std::string(current_username) + "/" + current_repository_name));
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

static auto PREVIOUS_VERSION = version_number(1, 2, 2);
static auto UPDATED_VERSION = version_number(1, 2, 3);
static auto PATTERN_ZIP = "^release-\\d+.\\d+.\\d+.zip$";
static auto PATTERN_ZIP_SUB = "^release-\\d+.\\d+.\\d+-subfolder.zip$";

::updater create_updater(
    std::string const& release_filename_pattern = PATTERN_ZIP,
    version_number const& current_version = PREVIOUS_VERSION)
{
    ::updater updater(UPDATE_WORKING_DIR, current_version);
    updater.update_source(mock_github_api_latest_retriever());
    updater.download_filename_pattern(release_filename_pattern);
    updater.archive_type(archive_type::zip_archive);
    updater.add_update_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    updater.add_update_verification(verifiers::message_digest(
        "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519", PUBLIC_KEY));
    return updater;
}

TEST(updater, StatusIsUpToDateWhenVersionIsIdentical)
{
    ::updater updater = create_updater(PATTERN_ZIP_SUB, UPDATED_VERSION);
    auto result = updater.update();
    EXPECT_EQ(update_status::up_to_date, result.status);
}

TEST(updater, StatusIsLatestIsOlderWhenVersionIsLower)
{
    ::updater updater =
        create_updater(PATTERN_ZIP_SUB, version_number(1, 3, 0));
    auto result = updater.update();
    EXPECT_EQ(update_status::latest_is_older, result.status);
}

inline ::manager to_manager(::updater const& updater)
{
    return manager(updater.working_directory(), updater.current_version());
}

void updater_update_test(::updater& updater,
    std::filesystem::path const& expected_extracted_file,
    bool should_throw = false)
{
    std::filesystem::remove_all(updater.working_directory());
    std::optional<update_result> result;
    EXPECT_EQ(std::nullopt, to_manager(updater).latest_available_version());
    if (should_throw) {
        EXPECT_ANY_THROW(result = updater.update());
        std::filesystem::remove_all(updater.working_directory());
        return;
    } else {
        EXPECT_NO_THROW(result = updater.update());
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(update_status::update_downloaded, result->status);
    ASSERT_TRUE(result->downloaded_directory.has_value());
    EXPECT_EQ(updater.working_directory() / result->version.string(),
        result->downloaded_directory);
    EXPECT_TRUE(std::filesystem::exists(result->downloaded_directory.value()));
    EXPECT_TRUE(std::filesystem::exists(
        result->downloaded_directory.value() / expected_extracted_file));
    auto latest_installed = to_manager(updater).latest_available_version();
    ASSERT_TRUE(latest_installed.has_value());
    EXPECT_EQ(version_number(1, 2, 3), result->version);
    EXPECT_EQ(result->version, latest_installed->first);
    EXPECT_EQ(result->downloaded_directory, latest_installed->second);
    std::filesystem::remove_all(updater.working_directory());
}

TEST(updater, UpdateIsInstalledWhenZipHasSubfolder)
{
    ::updater updater = create_updater(PATTERN_ZIP_SUB, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3/release-1.2.3.txt");
}

TEST(updater, UpdateIsInstalledWhenZipHasSubfolderAndIsFlattened)
{
    ::updater updater = create_updater(PATTERN_ZIP_SUB, PREVIOUS_VERSION);
    updater.add_post_update_operation(
        operations::flatten_extracted_directory());
    updater_update_test(updater, "release-1.2.3.txt");
}

TEST(updater, UpdateIsInstalledWhenZipHasNoSubfolderToFlatten)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
}

TEST(updater, UpdateFailsWhenZipHasNoSubfolderButIsFlattened)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater.add_post_update_operation(
        operations::flatten_extracted_directory());
    updater_update_test(updater, "release-1.2.3.txt", true);
}

TEST(updater, UpdateSucceedsWhenZipHasNoSubfolderButIsFlattenedSilently)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater.add_post_update_operation(
        operations::flatten_extracted_directory(false));
    updater_update_test(updater, "release-1.2.3.txt", false);
}

TEST(updater, StartMenuShortcutExistsAfterAddingRespectiveOperation)
{
    auto directory = internal::win::programs_path("ungive_update_test");
    ASSERT_TRUE(directory.has_value());
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater.add_post_update_operation(operations::create_start_menu_shortcut(
        "release-1.2.3.txt", "Release 1.2.3", "ungive_update_test"));
    updater_update_test(updater, "release-1.2.3.txt", false);
    EXPECT_TRUE(
        std::filesystem::exists(directory.value() / "Release 1.2.3.lnk"));
    std::filesystem::remove_all(directory.value());
}

TEST(updater, StartMenuShortcutDoesNotExistWhenOnlyUpdatingIt)
{
    auto directory = internal::win::programs_path("ungive_update_test");
    ASSERT_TRUE(directory.has_value());
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater.add_post_update_operation(operations::update_start_menu_shortcut(
        "release-1.2.3.txt", "Release 1.2.3", "ungive_update_test"));
    updater_update_test(updater, "release-1.2.3.txt", false);
    EXPECT_FALSE(std::filesystem::exists(directory.value()));
    EXPECT_FALSE(
        std::filesystem::exists(directory.value() / "Release 1.2.3.lnk"));
    std::filesystem::remove_all(directory.value());
}

TEST(updater, StartMenuShortcutChangedWhenItExistedAndItIsUpdated)
{
    auto directory = internal::win::programs_path("ungive_update_test");
    ASSERT_TRUE(directory.has_value());
    auto link_path = directory.value() / "Release 1.2.3.lnk";
    // Pretend that it already exists, but as an empty file.
    internal::touch_file(link_path);
    ASSERT_EQ(0, std::filesystem::file_size(link_path));
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater.add_post_update_operation(operations::update_start_menu_shortcut(
        "release-1.2.3.txt", "Release 1.2.3", "ungive_update_test"));
    updater_update_test(updater, "release-1.2.3.txt", false);
    EXPECT_TRUE(std::filesystem::exists(link_path));
    EXPECT_GT(std::filesystem::file_size(link_path), 0);
    std::filesystem::remove_all(directory.value());
}

void updater_prune_preparation(::updater& updater)
{
    std::filesystem::remove_all(updater.working_directory());
    std::filesystem::create_directories(updater.working_directory());
    // Pretend there is an older version available.
    internal::touch_file(updater.working_directory() /
        PREVIOUS_VERSION.string() / sentinel_filename());
    auto latest_installed = to_manager(updater).latest_available_version();
    EXPECT_EQ(PREVIOUS_VERSION, latest_installed->first);
    auto result = updater.update();
    latest_installed = to_manager(updater).latest_available_version();
    ASSERT_TRUE(latest_installed.has_value());
    EXPECT_EQ(UPDATED_VERSION, result.version);
    EXPECT_TRUE(std::filesystem::exists(updater.working_directory() /
        result.version.string() / sentinel_filename()));
}

TEST(updater, OldVersionIsDeletedAfterPruneIsCalledWithNewVersion)
{
    ::updater updater = create_updater();
    updater_prune_preparation(updater);
    // Recreate the manager since the preparation step installed an update.
    updater = create_updater(PATTERN_ZIP, UPDATED_VERSION);
    to_manager(updater).prune();
    EXPECT_FALSE(std::filesystem::exists(updater.working_directory() /
        PREVIOUS_VERSION.string() / sentinel_filename()));
    EXPECT_TRUE(std::filesystem::exists(updater.working_directory() /
        UPDATED_VERSION.string() / sentinel_filename()));
}

TEST(updater, OldVersionStillExistsAfterPruneIsCalledWithOldVersion)
{
    ::updater updater = create_updater();
    updater_prune_preparation(updater);
    to_manager(updater).prune();
    EXPECT_TRUE(std::filesystem::exists(updater.working_directory() /
        PREVIOUS_VERSION.string() / sentinel_filename()));
    EXPECT_TRUE(std::filesystem::exists(updater.working_directory() /
        UPDATED_VERSION.string() / sentinel_filename()));
}
