#pragma once

#include <string>

#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

namespace ungive::update::util
{

// Extracts a ZIP file to a given target directory.
inline void zip_extract(
    std::string const& zip_path, std::string const& target_directory)
{
    void* reader = NULL;
    int32_t err = MZ_OK;
    reader = mz_zip_reader_create();
    if (!reader) {
        throw std::runtime_error("failed to create zip reader");
    }
    err = mz_zip_reader_open_file(reader, zip_path.c_str());
    if (err != MZ_OK) {
        throw std::runtime_error(
            "failed to open zip file: " + std::to_string(err));
    } else {
        err = mz_zip_reader_save_all(reader, target_directory.c_str());
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
inline void flatten_root_directory(std::string const& directory)
{
    namespace fs = std::filesystem;
    std::optional<fs::path> subdir;
    for (auto const& entry : fs::directory_iterator(directory)) {
        if (subdir.has_value()) {
            throw std::runtime_error("root contains more than one file");
        }
        if (!entry.is_directory()) {
            throw std::runtime_error("root contains non-directory files");
        }
        subdir = entry.path();
    }
    if (!subdir.has_value()) {
        throw std::runtime_error("root does not contain any files");
    }
    if (!subdir->has_parent_path()) {
        throw std::runtime_error("subdirectory should have a parent");
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
}

} // namespace ungive::update::util
