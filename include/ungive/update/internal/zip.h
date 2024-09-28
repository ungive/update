#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <stringapiset.h>

namespace ungive::update::internal
{

#ifdef WIN32
// Source: https://stackoverflow.com/a/3999597/6748004
inline std::string utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0],
        size_needed, NULL, NULL);
    return strTo;
}

// Source: https://stackoverflow.com/a/3999597/6748004
inline std::wstring utf8_decode(const std::string& str)
{
    if (str.empty())
        return std::wstring();
    int size_needed =
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(
        CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
#endif // WIN32

// Extracts a ZIP file to a given target directory.
inline void zip_extract(std::filesystem::path const& zip_path,
    std::filesystem::path const& target_directory)
{
    void* reader = NULL;
    int32_t err = MZ_OK;
    reader = mz_zip_reader_create();
    if (!reader) {
        throw std::runtime_error("failed to create zip reader");
    }
    auto zip_path_utf8 = utf8_encode(zip_path);
    err = mz_zip_reader_open_file(reader, zip_path_utf8.c_str());
    if (err != MZ_OK) {
        throw std::runtime_error(
            "failed to open zip file: " + std::to_string(err));
    } else {
        auto target_directory_utf8 = utf8_encode(target_directory);
        err = mz_zip_reader_save_all(reader, target_directory_utf8.c_str());
        if (err != MZ_OK) {
            throw std::runtime_error(
                "failed to save zip entries to disk: " + std::to_string(err));
        }
    }
    err = mz_zip_reader_close(reader);
    if (err != MZ_OK) {
        throw std::runtime_error(
            "failed to close archive for reading: " + std::to_string(err));
    }
    mz_zip_reader_delete(&reader);
}

// Flattens a single subdirectory within a directory
// by copying all files from that subdirectory into the directory
// and deleting the then empty subdirectory.
// Useful for flattening a ZIP archive that contains a single folder.
inline bool flatten_root_directory(std::filesystem::path const& directory)
{
    namespace fs = std::filesystem;
    std::optional<fs::path> subdir;
    for (auto const& entry : fs::directory_iterator(directory)) {
        if (subdir.has_value()) {
            return false;
        }
        if (!entry.is_directory()) {
            return false;
        }
        subdir = entry.path();
    }
    if (!subdir.has_value()) {
        return false;
    }
    if (!subdir->has_parent_path()) {
        return false;
    }
    auto temp_name = internal::random_string(24);
    fs::path new_dir = subdir->parent_path() / temp_name;
    fs::rename(subdir.value(), new_dir);
    for (auto const& entry : fs::directory_iterator(new_dir)) {
        if (!entry.path().has_filename()) {
            assert(false);
            continue;
        }
        fs::rename(entry.path(), directory / entry.path().filename());
    }
    auto count = fs::remove_all(new_dir);
    assert(count == 1);
    return true;
}

} // namespace ungive::update::internal
