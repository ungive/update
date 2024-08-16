#pragma once

#include <filesystem>
#include <optional>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winrt/base.h>
// #include <handleapi.h>
// #include <processthreadsapi.h>
#include <Psapi.h>
// #include <tchar.h>

#include "ungive/update/internal/util.h"

#pragma comment(lib, "psapi.lib")

#define CLOSE_WAIT_TIMEOUT_MILLIS 2500

namespace ungive::update::internal::win
{

inline std::filesystem::path current_process_executable()
{
    wchar_t executable_path[MAX_PATH];
    GetModuleFileNameW(NULL, executable_path, MAX_PATH);
    return executable_path;
}

// Throws the last error that occured in the Windows library
// and wraps any HRESULT error into a standard library error,
// such that users of the library won't accidentally not catch
// any exceptions caused by Windows library functions.
inline void throw_last_error()
{
    try {
        winrt::throw_last_error();
    }
    catch (winrt::hresult_error const& err) {
        // Any HRESULT error is wrapped in a runtime error type.
        throw std::runtime_error(winrt::to_string(err.message()) + " (" +
            std::to_string(err.code()) + ")");
    }
    catch (std::exception const& err) {
        // Any standard library exception is simply rethrown.
        throw err;
    }
    catch (...) {
        // Any other type that is not understood
        // must also throw a standard exception error type.
        throw std::runtime_error("an unknown error occured");
    }
}

// Starts a process detached.
// May throw an exception if an error occured
inline void start_process_detached(std::filesystem::path const& executable,
    std::vector<std::string> const& arguments = {})
{
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    std::optional<std::filesystem::path> parent_path;
    if (executable.has_parent_path()) {
        parent_path = executable.parent_path();
    }
    std::wostringstream oss;
    oss << executable.c_str();
    for (auto const& argument : arguments) {
        oss << " " << argument.c_str();
    }
    auto args = oss.str();

    auto result = CreateProcessW(executable.c_str(), args.data(), NULL, NULL,
        NULL, DETACHED_PROCESS, NULL,
        parent_path.has_value() ? parent_path->c_str() : NULL, &StartupInfo,
        &ProcessInfo);
    if (result == 0) {
        throw_last_error();
    }
}

inline DWORD WaitForProcessExit(
    DWORD pid, DWORD timeoutMillis = CLOSE_WAIT_TIMEOUT_MILLIS)
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == NULL)
        throw_last_error();
    DWORD ret = WaitForSingleObject(process, timeoutMillis);
    CloseHandle(process);
    return ret;
}

inline BOOL CALLBACK CloseWindowsProc(HWND hwnd, LPARAM lParam)
{
    DWORD processId;
    if (GetWindowThreadProcessId(hwnd, &processId) == 0)
        throw_last_error();
    if (lParam == processId) {
        if (PostMessage(hwnd, WM_CLOSE, 0, 0) == 0)
            throw_last_error();
    }
    return TRUE;
}

inline DWORD close_pids_and_wait_for_exit(std::vector<DWORD> const& pids)
{
    for (auto const& pid : pids) {
        if (EnumWindows(&CloseWindowsProc, pid) == NULL) {
            throw_last_error();
        }
    }

    // Wait for all processes to have exited.
    for (auto pid : pids) {
        auto code = WaitForProcessExit(pid);
        if (code != WAIT_OBJECT_0) {
            if (code == WAIT_FAILED) {
                throw_last_error();
            }
            return code;
        }
    }

    return 0;
}

// Returns the PIDs of all process that either:
// - have their executable in the given directory, if the path is a directory,
// - or whose executable matches the path, if the path is not a directory.
// If exclude_current_process is set the current process will be excluded.
static std::vector<DWORD> get_running_pids(
    std::filesystem::path const& path_or_directory,
    bool exclude_current_process = true)
{
    std::vector<DWORD> pids;
    pids.reserve(16);

    // Make sure we don't kill this process, just to be absolutely sure.
    DWORD current_pid = GetCurrentProcessId();

    static constexpr DWORD PROCESS_BUFFER_SIZE = 2048;

    DWORD processes[PROCESS_BUFFER_SIZE];
    DWORD ps_size;
    if (!EnumProcesses(processes, sizeof(processes), &ps_size)) {
        throw_last_error();
    }

    DWORD count = ps_size / sizeof(DWORD);
    for (size_t i = 0; i < count; i++) {
        if (processes[i] == 0) {
            continue;
        }

        DWORD pid = processes[i];
        HANDLE process_handle = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (process_handle == NULL) {
            continue;
        }

        HMODULE module_handle;
        DWORD m_size;
        if (!EnumProcessModules(process_handle, &module_handle,
                sizeof(module_handle), &m_size)) {
            CloseHandle(process_handle);
            continue;
        }

        TCHAR process_name[MAX_PATH] = {};
        DWORD pn_size = GetModuleBaseName(process_handle, module_handle,
            process_name, sizeof(process_name) / sizeof(TCHAR));
        if (pn_size == 0) {
            CloseHandle(process_handle);
            continue;
        }

        TCHAR filepath[MAX_PATH];
        DWORD fn_size = GetModuleFileNameEx(
            process_handle, NULL, filepath, sizeof(filepath) / sizeof(TCHAR));
        if (fn_size == 0) {
            CloseHandle(process_handle);
            continue;
        }

        // Exclude the current process.
        if (exclude_current_process && pid == current_pid) {
            CloseHandle(process_handle);
            continue;
        }

        // The path is either a directory and the process's executable
        // lies in this directory or the path is an executable path.
        if (std::filesystem::is_directory(path_or_directory)) {
            if (internal::is_subpath(filepath, path_or_directory)) {
                pids.push_back(pid);
            }
        } else if (path_or_directory == filepath) {
            pids.push_back(pid);
        }

        CloseHandle(process_handle);
    }

    return pids;
}

// Kills any processes whose executables are located in the given directory,
// if the given path is a directory or whose executable path
// matches the given path exactly.
// Waits until all processes have exited.
// Returns the number of processes that have been killed
// or throws an exception if an error occured.
inline size_t kill_processes(std::filesystem::path path_or_directory)
{
    std::vector<DWORD> pids;
    DWORD code;
    try {
        pids = get_running_pids(path_or_directory);
        code = close_pids_and_wait_for_exit(pids);
    }
    catch (winrt::hresult_error const& err) {
        throw std::runtime_error(winrt::to_string(err.message()) + " (" +
            std::to_string(err.code()) + ")");
    }
    switch (code) {
    case 0:
        return pids.size();
    case WAIT_TIMEOUT:
        throw std::runtime_error(
            "failed to close processes: operation timed out (" +
            std::to_string(code) + ")");
    default:
        throw std::runtime_error(
            "failed to close processes: code " + std::to_string(code));
    }
}

#undef CLOSE_WAIT_TIMEOUT_MILLIS

} // namespace ungive::update::internal::win
