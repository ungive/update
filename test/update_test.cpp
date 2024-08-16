#include <iostream>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update/manager.hpp"
#include "ungive/update/updater.hpp"

using namespace ungive::update;
using namespace std::chrono_literals;

// Used test files:
// - https://ungive.github.io/update_test/github-api-mock/latest-simple.json
// Repository: https://github.com/ungive/update_test
// Test releases: https://github.com/ungive/update_test/releases

#ifdef WIN32
const std::filesystem::path UPDATE_WORKING_DIR =
    internal::win::local_appdata_path("ungive_update_test_dir").value();
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
const char* LATEST_DIRECTORY = "current";

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

::manager create_manager(
    version_number const& current_version = PREVIOUS_VERSION)
{
    return ::manager(UPDATE_WORKING_DIR, current_version);
}

::updater internal_create_updater(
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

::updater create_updater(
    std::string const& release_filename_pattern = PATTERN_ZIP,
    version_number const& current_version = PREVIOUS_VERSION)
{
    std::filesystem::remove_all(UPDATE_WORKING_DIR);
    return internal_create_updater(release_filename_pattern, current_version);
}

::updater recreate_updater(
    std::string const& release_filename_pattern = PATTERN_ZIP,
    version_number const& current_version = PREVIOUS_VERSION)
{
    return internal_create_updater(release_filename_pattern, current_version);
}

::manager to_manager(::updater const& updater)
{
    return ::manager(updater.working_directory(), updater.current_version());
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

void write_sentinel(
    std::filesystem::path const& directory, version_number const& version)
{
    internal::sentinel sentinel(directory);
    sentinel.version(version);
    sentinel.write();
}

std::optional<version_number> read_sentinel(
    std::filesystem::path const& directory)
{
    internal::sentinel sentinel(directory);
    if (!sentinel.read()) {
        return std::nullopt;
    }
    return sentinel.version();
}

std::optional<update_result> updater_update_test(::updater& updater,
    std::filesystem::path const& expected_extracted_file,
    bool should_throw = false)
{
    std::filesystem::remove_all(updater.working_directory());
    std::optional<update_result> result;
    EXPECT_EQ(std::nullopt, to_manager(updater).latest_available_update());
    if (should_throw) {
        EXPECT_ANY_THROW(result = updater.update());
        std::filesystem::remove_all(updater.working_directory());
        return std::nullopt;
    } else {
        EXPECT_NO_THROW(result = updater.update());
    }
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(update_status::update_downloaded, result->status);
    EXPECT_TRUE(result->downloaded_directory.has_value());
    EXPECT_EQ(updater.working_directory() / result->version.string(),
        result->downloaded_directory);
    EXPECT_TRUE(std::filesystem::exists(result->downloaded_directory.value()));
    EXPECT_TRUE(std::filesystem::exists(
        result->downloaded_directory.value() / expected_extracted_file));
    auto latest_installed = to_manager(updater).latest_available_update();
    EXPECT_TRUE(latest_installed.has_value());
    EXPECT_EQ(version_number(1, 2, 3), result->version);
    EXPECT_EQ(result->version, latest_installed->first);
    EXPECT_EQ(result->downloaded_directory, latest_installed->second);
    EXPECT_EQ(
        result->version, read_sentinel(result->downloaded_directory.value()));
    return result;
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

TEST(updater, LatestAvailableUpdateReturnsNothingWhenSentinelMismatches)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    auto result = updater_update_test(updater, "release-1.2.3.txt");
    write_sentinel(
        result->downloaded_directory.value(), version_number(1, 3, 0));
    auto latest = to_manager(updater).latest_available_update();
    EXPECT_EQ(std::nullopt, latest);
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
    write_sentinel(updater.working_directory() / PREVIOUS_VERSION.string(),
        PREVIOUS_VERSION);
    auto latest_installed = to_manager(updater).latest_available_update();
    EXPECT_EQ(PREVIOUS_VERSION, latest_installed->first);
    auto result = updater.update();
    latest_installed = to_manager(updater).latest_available_update();
    ASSERT_TRUE(latest_installed.has_value());
    EXPECT_EQ(UPDATED_VERSION, result.version);
    auto sentinel_version =
        read_sentinel(updater.working_directory() / result.version.string());
    EXPECT_EQ(result.version, sentinel_version);
}

TEST(updater, OldVersionIsDeletedAfterPruneIsCalledWithNewVersion)
{
    ::updater updater = create_updater();
    updater_prune_preparation(updater);
    // Recreate the updater since the preparation step installed an update.
    updater = recreate_updater(PATTERN_ZIP, UPDATED_VERSION);
    to_manager(updater).prune();
    EXPECT_FALSE(
        read_sentinel(updater.working_directory() / PREVIOUS_VERSION.string())
            .has_value());
    EXPECT_TRUE(
        read_sentinel(updater.working_directory() / UPDATED_VERSION.string())
            .has_value());
}

TEST(updater, OldVersionStillExistsAfterPruneIsCalledWithOldVersion)
{
    ::updater updater = create_updater();
    updater_prune_preparation(updater);
    to_manager(updater).prune();
    EXPECT_TRUE(
        read_sentinel(updater.working_directory() / PREVIOUS_VERSION.string())
            .has_value());
    EXPECT_TRUE(
        read_sentinel(updater.working_directory() / UPDATED_VERSION.string())
            .has_value());
}

TEST(sentinel, ReadingWorksWhenSentinelHasUnknownKeys)
{
    auto directory = internal::create_temporary_directory();
    std::ostringstream oss;
    oss << "version=1.1.2\n";
    oss << "another_key=hello\n";
    oss << "\n"; // empty line
    internal::write_file(directory / internal::sentinel_filename(), oss.str());
    internal::sentinel sentinel(directory);
    EXPECT_TRUE(sentinel.read());
    EXPECT_EQ(version_number(1, 1, 2), sentinel.version());
}

TEST(sentinel, ReadingFailsWhenSentinelHasNoVersionKey)
{
    auto directory = internal::create_temporary_directory();
    std::ostringstream oss;
    oss << "another_key=hello\n";
    oss << "\n"; // empty line
    internal::write_file(directory / internal::sentinel_filename(), oss.str());
    internal::sentinel sentinel(directory);
    EXPECT_FALSE(sentinel.read());
}

class test_launcher
{
public:
    test_launcher(version_number const& version = PREVIOUS_VERSION)
    {
        m_executable = internal::win::current_process_executable();
        m_temp_directory = internal::create_temporary_directory();
        m_output_file = m_temp_directory / "out.txt";
        m_version = version;
    }

    ~test_launcher()
    {
        if (std::filesystem::exists(m_temp_directory)) {
            std::filesystem::remove_all(m_temp_directory);
        }
    }

    void executable(std::filesystem::path const& path) { m_executable = path; }

    std::filesystem::path const& executable() const { return m_executable; }

    std::vector<std::string> print_args(std::string const& text) const
    {
        return { "--print", m_output_file.string(), text };
    }

    std::vector<std::string> sleep_args(
        std::chrono::milliseconds duration) const
    {
        return { "--sleep", m_output_file.string(),
            std::to_string(duration.count()) };
    }

    std::vector<std::string> apply_latest_args() const
    {
        return { "--apply-latest", m_output_file.string(), m_version.string() };
    }

    std::vector<std::string> apply_and_start_latest_args() const
    {
        return { "--apply-and-start-latest", m_output_file.string(),
            m_version.string() };
    }

    void run_print(std::string const& text) const
    {
        internal::win::start_process_detached(executable(), print_args(text));
    }

    void run_apply_latest() const
    {
        internal::win::start_process_detached(
            executable(), apply_latest_args());
    }

    void run_apply_and_start_latest() const
    {
        internal::win::start_process_detached(
            executable(), apply_and_start_latest_args());
    }

    void run_sleep(std::chrono::milliseconds duration) const
    {
        internal::win::start_process_detached(
            executable(), sleep_args(duration));
    }

    std::string wait_for_output(std::chrono::milliseconds timeout = 10s) const
    {
        // Note: somehow writing an empty output makes this time out.
        static const auto interval = 250ms;
        // Wait a bit in case we are interrupting the writing process.
        static const auto wait_count = 2;
        int waited_for = 0;
        auto steps = static_cast<size_t>(timeout / interval);
        for (size_t i = 0; i < steps + wait_count + 1; i++) {
            std::this_thread::sleep_for(interval);
            try {
                auto result = internal::read_file(m_output_file);
                if (waited_for >= wait_count) {
                    return result;
                }
                if (result.size() > 0) {
                    waited_for++;
                }
            }
            catch (...) {
            }
        }
        throw std::runtime_error("waiting for launcher output timed out");
    }

private:
    std::filesystem::path m_executable;
    std::filesystem::path m_output_file;
    std::filesystem::path m_temp_directory;
    std::vector<std::string> m_apply_latest_args;
    version_number m_version;
};

int main(int argc, char* argv[])
{
    try {
        if (argc == 4 && std::string(argv[1]) == "--print") {
            std::filesystem::path output_file(argv[2]);
            internal::write_file(output_file, argv[3]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--sleep") {
            std::filesystem::path output_file(argv[2]);
            auto duration_millis = std::stoi(argv[3]);
            std::this_thread::sleep_for(
                std::chrono::milliseconds{ duration_millis });
            internal::write_file(output_file, "ok");
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--apply-latest") {
            std::filesystem::path output_file(argv[2]);
            auto current_version = version_number::from_string(argv[3]);
            ::manager manager(UPDATE_WORKING_DIR, current_version);
            manager.apply_latest();
            internal::write_file(output_file, "ok");
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--apply-and-start-latest") {
            std::filesystem::path output_file(argv[2]);
            auto current_version = version_number::from_string(argv[3]);
            ::manager manager(UPDATE_WORKING_DIR, current_version);
            manager.apply_latest();
            test_launcher nested_launcher(current_version);
            manager.start_latest(nested_launcher.executable().filename(),
                nested_launcher.print_args("success"));
            auto output = nested_launcher.wait_for_output();
            internal::write_file(output_file, "start_latest: " + output);
            return 0;
        }
    }
    catch (std::exception const& e) {
        internal::write_file(argv[2], std::string("error: ") + e.what());
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(manager, OnlyLatestDirectoryExistsAfterApplyLatestIsCalled)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    auto manager = to_manager(updater);
    std::optional<version_number> apply_result;
    EXPECT_NO_THROW(apply_result = manager.apply_latest());
    EXPECT_EQ(UPDATED_VERSION, apply_result);
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    internal::sentinel latest_sentinel(
        updater.working_directory() / LATEST_DIRECTORY);
    EXPECT_TRUE(latest_sentinel.read());
    EXPECT_EQ(UPDATED_VERSION, latest_sentinel.version());
}

TEST(manager, LauncherAppliesLatestAfterLauncherIsManuallyStarted)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    test_launcher launcher(PREVIOUS_VERSION);
    EXPECT_NO_THROW(launcher.run_apply_latest());
    auto output = launcher.wait_for_output();
    EXPECT_EQ("ok", output);
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    internal::sentinel latest_sentinel(
        updater.working_directory() / LATEST_DIRECTORY);
    EXPECT_TRUE(latest_sentinel.read());
    EXPECT_EQ(UPDATED_VERSION, latest_sentinel.version());
}

TEST(manager, LauncherIsStartedAndAppliesLatestAfterLaunchLatestIsCalled)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    test_launcher launcher(PREVIOUS_VERSION);
    auto manager = to_manager(updater);
    bool result = false;
    EXPECT_NO_THROW(result = manager.launch_latest(
                        launcher.executable(), launcher.apply_latest_args()));
    EXPECT_TRUE(result);
    auto output = launcher.wait_for_output();
    EXPECT_EQ("ok", output);
    EXPECT_FALSE(std::filesystem::exists(
        updater.working_directory() / UPDATED_VERSION.string()));
    EXPECT_TRUE(std::filesystem::exists(
        updater.working_directory() / LATEST_DIRECTORY));
    internal::sentinel latest_sentinel(
        updater.working_directory() / LATEST_DIRECTORY);
    EXPECT_TRUE(latest_sentinel.read());
    EXPECT_EQ(UPDATED_VERSION, latest_sentinel.version());
}

TEST(manager, LauncherIsStartedWhenLauncherPathIsRelativeToTestExecutable)
{
    auto process_executable = internal::win::current_process_executable();
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    test_launcher launcher(PREVIOUS_VERSION);
    auto manager = to_manager(updater);
    bool result = false;
    EXPECT_NO_THROW(result = manager.launch_latest(
                        std::filesystem::relative(launcher.executable(),
                            process_executable.parent_path()),
                        launcher.apply_latest_args()));
    EXPECT_TRUE(result);
    auto output = launcher.wait_for_output();
    EXPECT_EQ("ok", output);
}

TEST(manager, ExceptionThrownWhenApplyLatestDoesNotKillButLatestHasProcess)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    // Copy the test executable to the latest directory and make it sleep.
    auto process_executable = internal::win::current_process_executable();
    auto latest_directory = updater.working_directory() / LATEST_DIRECTORY;
    auto latest_executable = latest_directory / process_executable.filename();
    EXPECT_TRUE(std::filesystem::create_directories(latest_directory));
    std::filesystem::copy(process_executable, latest_executable);
    test_launcher launcher(PREVIOUS_VERSION);
    launcher.executable(latest_executable);
    EXPECT_NO_THROW(launcher.run_sleep(10s));
    // Apply the latest update.
    auto manager = to_manager(updater);
    std::optional<version_number> apply_result{};
    // Don't kill processes for this test (false).
    EXPECT_ANY_THROW(apply_result = manager.apply_latest(false));
    EXPECT_EQ(std::nullopt, apply_result);
    // Check that the process exits properly by having output.
    auto output = launcher.wait_for_output(15s);
    EXPECT_EQ("ok", output);
}

TEST(manager, LatestIsStartedWhenLauncherAppliesAndStartsLatestUpdate)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    updater_update_test(updater, "release-1.2.3.txt");
    test_launcher launcher(PREVIOUS_VERSION);
    // Copy the test executable to the update directory.
    auto process_executable = internal::win::current_process_executable();
    auto update_executable = updater.working_directory() /
        UPDATED_VERSION.string() / process_executable.filename();
    std::filesystem::copy(process_executable, update_executable);
    // Now try starting the executable by first applying the update
    // with the launcher and then starting the executable with the launcher,
    // which should now be in the latest directory.
    auto manager = to_manager(updater);
    bool result = false;
    EXPECT_NO_THROW(result = manager.launch_latest(launcher.executable(),
                        launcher.apply_and_start_latest_args()));
    EXPECT_TRUE(result);
    auto output = launcher.wait_for_output();
    EXPECT_EQ("start_latest: success", output);
    EXPECT_TRUE(std::filesystem::exists(updater.working_directory() /
        LATEST_DIRECTORY / process_executable.filename()));
}

TEST(manager, LaunchLatestReturnsFalseWhenThereIsNothingToLaunch)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    auto manager = to_manager(updater);
    bool result = false;
    test_launcher launcher(PREVIOUS_VERSION);
    EXPECT_NO_THROW(result = manager.launch_latest(
                        launcher.executable(), launcher.print_args("ok")));
    EXPECT_FALSE(result);
}

void manager_launch_latest_return_value_test(
    version_number const& existing_version, bool return_value,
    bool use_latest_directory_name = false)
{
    ::updater updater = create_updater(PATTERN_ZIP, PREVIOUS_VERSION);
    auto directory = updater.working_directory() /
        (use_latest_directory_name ? LATEST_DIRECTORY
                                   : existing_version.string());
    std::filesystem::create_directories(directory);
    internal::sentinel sentinel(directory);
    sentinel.version(existing_version);
    sentinel.write();
    auto manager = to_manager(updater);
    bool result = false;
    test_launcher launcher(PREVIOUS_VERSION);
    EXPECT_NO_THROW(result = manager.launch_latest(launcher.executable(),
                        launcher.print_args("launched")));
    EXPECT_EQ(return_value, result);
    if (result) {
        auto output = launcher.wait_for_output();
        EXPECT_EQ("launched", output);
    }
}

TEST(manager, LaunchLatestReturnsTrueWhenThereIsANewerVersion)
{
    manager_launch_latest_return_value_test(UPDATED_VERSION, true);
    manager_launch_latest_return_value_test(UPDATED_VERSION, true, true);
}

TEST(manager, LaunchLatestReturnsFalseWhenThereIsAnIdenticalVersion)
{
    manager_launch_latest_return_value_test(PREVIOUS_VERSION, false);
    manager_launch_latest_return_value_test(PREVIOUS_VERSION, false, true);
}

TEST(manager, LaunchLatestReturnsFalseWhenThereIsAnOlderVersion)
{
    manager_launch_latest_return_value_test(version_number(0, 0, 1), false);
    manager_launch_latest_return_value_test(
        version_number(0, 0, 1), false, true);
}

TEST(updater, ThrowsExceptionWhenCancelStateIsSetToTrue)
{
    ::updater updater = create_updater(PATTERN_ZIP_SUB, PREVIOUS_VERSION);
    updater.cancel(true);
    updater_update_test(updater, "release-1.2.3/release-1.2.3.txt", true);
}

TEST(manager, MissingSentinelIsCreatedWhenManagerIsCreatedAndLatestExists)
{
    // Note that the sentinel file is only created automatically,
    // when the current process is executing an executable
    // that is located in the latest directory.
    // This is the case here.
    std::filesystem::remove_all(UPDATE_WORKING_DIR);
    auto expected_version = version_number(4, 3, 1);
    auto process_executable = internal::win::current_process_executable();
    auto latest_directory = UPDATE_WORKING_DIR / LATEST_DIRECTORY;
    auto latest_executable = latest_directory / process_executable.filename();
    EXPECT_TRUE(std::filesystem::create_directories(latest_directory));
    std::filesystem::copy(process_executable, latest_executable);
    test_launcher launcher(expected_version);
    launcher.executable(latest_executable);
    EXPECT_NO_THROW(launcher.run_apply_latest());
    EXPECT_EQ("ok", launcher.wait_for_output());
    internal::sentinel sentinel(UPDATE_WORKING_DIR / LATEST_DIRECTORY);
    EXPECT_TRUE(sentinel.read());
    EXPECT_EQ(expected_version, sentinel.version());
}

TEST(manager, SentinelIsOverwrittenWhenManagerIsCreatedAndSentinelHasBadVersion)
{
    // Note that the sentinel file is only created automatically,
    // when the current process is executing an executable
    // that is located in the latest directory.
    // This is the case here.
    std::filesystem::remove_all(UPDATE_WORKING_DIR);
    auto expected_version = version_number(4, 3, 1);
    auto other_version = version_number(1, 1, 1);
    auto process_executable = internal::win::current_process_executable();
    auto latest_directory = UPDATE_WORKING_DIR / LATEST_DIRECTORY;
    auto latest_executable = latest_directory / process_executable.filename();
    EXPECT_TRUE(std::filesystem::create_directories(latest_directory));
    std::filesystem::copy(process_executable, latest_executable);
    // The sentinel exists and has a different version.
    internal::sentinel sentinel1(latest_directory);
    sentinel1.version(other_version);
    sentinel1.write();
    test_launcher launcher(expected_version);
    launcher.executable(latest_executable);
    EXPECT_NO_THROW(launcher.run_apply_latest());
    EXPECT_EQ("ok", launcher.wait_for_output());
    internal::sentinel sentinel2(latest_directory);
    EXPECT_TRUE(sentinel2.read());
    EXPECT_EQ(expected_version, sentinel2.version());
}
