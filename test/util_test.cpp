#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update.hpp"

using namespace ungive::update;

namespace fs = std::filesystem;

static auto TEST_FILES = fs::path(__FILE__).parent_path() / "test_files";

struct temp_dir
{
    temp_dir() : m_path{ internal::create_temporary_directory() } {}

    ~temp_dir() { fs::remove_all(m_path); }

    fs::path path() const { return m_path; }

    std::string string() const { return m_path.string(); }

private:
    fs::path m_path;
};

inline std::string read(fs::path const& path)
{
    return downloaded_file(path).read();
}

// Windows tests
#ifdef WIN32
static auto ZIP_SUBFOLDER = "release-1.2.3";
static auto ZIP_FILENAME = "release-1.2.3.txt";
static auto ZIP_CONTENT = "Release file for version 1.2.3";

TEST(util, FileExistsWhenExtractingZip)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3.zip";
    EXPECT_NO_THROW(util::zip_extract(zip.string(), dir.string()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_FILENAME));
}

TEST(util, FileExistsWhenExtractingZipWithSubfolder)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3-subfolder.zip";
    EXPECT_NO_THROW(util::zip_extract(zip.string(), dir.string()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_SUBFOLDER / ZIP_FILENAME));
}

TEST(util, FileIsMovedWhenExtractingZipAndFlatteningRootDirectory)
{
    temp_dir dir;
    auto zip = TEST_FILES / "release-1.2.3-subfolder.zip";
    EXPECT_NO_THROW(util::zip_extract(zip.string(), dir.string()));
    EXPECT_NO_THROW(util::flatten_root_directory(dir.string()));
    EXPECT_EQ(ZIP_CONTENT, read(dir.path() / ZIP_FILENAME));
}
#endif // WIN32
