#pragma once

#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <fileapi.h>

namespace ungive::update::internal::win
{

class lock_file
{
public:
    lock_file(std::filesystem::path const& filename)
        : m_filename{ filename.wstring() }
    {
        if (std::filesystem::is_directory(filename) ||
            !filename.has_parent_path()) {
            throw std::runtime_error("file must not be a directory");
        }
        std::filesystem::create_directories(filename.parent_path());
        m_handle = CreateFileW(m_filename.data(), GENERIC_READ, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD err = GetLastError();
        if (m_handle == NULL || m_handle == INVALID_HANDLE_VALUE ||
            err == ERROR_FILE_NOT_FOUND) {
            m_handle = CreateFileW(m_filename.data(), GENERIC_READ, 0, NULL,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (m_handle == NULL || m_handle == INVALID_HANDLE_VALUE) {
            err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION) {
                throw std::runtime_error(
                    "failed to open lock file: sharing violation");
            }
            throw std::runtime_error(
                "failed to open lock file: " + std::to_string(err));
        }
    }

    ~lock_file()
    {
        if (m_handle != NULL && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            DeleteFileW(m_filename.data());
            m_handle = NULL;
        }
    }

private:
    std::wstring m_filename{};
    HANDLE m_handle{ NULL };
};

} // namespace ungive::update::internal::win
