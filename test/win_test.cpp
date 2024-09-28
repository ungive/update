#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update/internal/win/process.h"
#include "ungive/update/internal/win/startmenu.h"
#include "ungive/update/updater.hpp"

using namespace ungive::update;

namespace fs = std::filesystem;

static auto TEST_FILES = fs::path(__FILE__).parent_path() / "test_files";

struct temp_dir
{
    temp_dir() : m_path{ internal::create_temporary_directory() } {}

    ~temp_dir() { fs::remove_all(m_path); }

    fs::path path() const { return m_path; }

private:
    fs::path m_path;
};

inline std::string read(fs::path const& path)
{
    return downloaded_file(path).read();
}

static auto ZIP_SUBFOLDER = "release-1.2.3";
static auto ZIP_FILENAME = "release-1.2.3.txt";
static auto ZIP_CONTENT = "Release file for version 1.2.3";

TEST(zip, FileExistsWhenExtractingZip)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3.zip";
    EXPECT_NO_THROW(internal::zip_extract(zip, dir.path()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_FILENAME));
}

TEST(zip, FileExistsWhenExtractingZipWithSubfolder)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3-subfolder.zip";
    EXPECT_NO_THROW(internal::zip_extract(zip, dir.path()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_SUBFOLDER / ZIP_FILENAME));
}

TEST(zip, FileIsMovedWhenExtractingZipAndFlatteningRootDirectory)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3-subfolder.zip";
    EXPECT_NO_THROW(internal::zip_extract(zip, dir.path()));
    EXPECT_NO_THROW(internal::flatten_root_directory(dir.path()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_FILENAME));
}

TEST(startmenu, StartMenuEntryExistsAfterCallingCreateStartMenuEntry)
{
    auto expected_directory =
        internal::win::programs_path("ungive_update_test");
    ASSERT_TRUE(expected_directory.has_value());
    auto result = internal::win::create_start_menu_entry(
        "C:\\Program Files\\Mozilla Firefox\\firefox.exe", "Firefox",
        "ungive_update_test");
    ASSERT_TRUE(result);
    auto expected_file = expected_directory.value() / "Firefox.lnk";
    ASSERT_TRUE(std::filesystem::exists(expected_file));
    std::filesystem::remove_all(expected_directory.value());
}

TEST(process, ExceptionIsThrownWhenLaunchingBadExecutable)
{
    EXPECT_ANY_THROW(internal::win::start_process_detached(
        internal::win::current_process_executable().parent_path() /
        "madeupexecutable.exe"));
}
