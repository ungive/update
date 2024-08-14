#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update/updater.hpp"

using namespace update;

extern const char* PUBLIC_KEY;

TEST(integration, DownloadAndVerifyLatestRelease)
{
    // Get the latest release from the GitHub API.
    github_api_latest_retriever latest("ungive", "update_test");
    auto latest_release = latest(std::regex("^release-\\d+.\\d+.\\d+.txt$"));
    EXPECT_EQ(version_number({ 1, 2, 3 }), latest_release.first);
    // Download the release including hash and signature verification.
    http_downloader downloader(latest_release.second.base_url());
    downloader.add_verification(verifiers::sha256sums("SHA256SUMS.txt"));
    downloader.add_verification(verifiers::message_digest(
        "SHA256SUMS.txt", "SHA256SUMS.txt.sig", "PEM", "ED25519", PUBLIC_KEY));
    std::optional<downloaded_file> release_file;
    EXPECT_NO_THROW(
        release_file = downloader.get(latest_release.second.filename()));
    ASSERT_TRUE(release_file.has_value());
    EXPECT_TRUE(std::filesystem::exists(release_file->path()));
}
