#pragma once

#include <filesystem>
#include <optional>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shlobj.h>

namespace ungive::update::internal::win
{

inline std::optional<std::filesystem::path> csidl_path(int csidl_value)
{
    WCHAR path[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, csidl_value, NULL, 0, path);
    if (SUCCEEDED(result)) {
        return std::filesystem::path(path);
    }
    return std::nullopt;
}

inline std::optional<std::filesystem::path> local_appdata_path()
{
    return csidl_path(CSIDL_LOCAL_APPDATA);
}

inline std::optional<std::filesystem::path> appdata_path()
{
    return csidl_path(CSIDL_APPDATA);
}

inline std::optional<std::filesystem::path> programs_path()
{
    return csidl_path(CSIDL_PROGRAMS);
}

// Creates a Windows start menu entry, may throw an exception.
inline bool has_start_menu_entry(std::filesystem::path const& target_path,
    std::filesystem::path const& link_name,
    std::filesystem::path const& category_name)
{
    if (link_name.has_parent_path()) {
        throw std::invalid_argument(
            "the start menu entry link name cannot have a parent path");
    }
    if (category_name.has_parent_path()) {
        throw std::invalid_argument(
            "the start menu entry category cannot have a parent path");
    }
    auto path = programs_path();
    if (!path.has_value()) {
        return false;
    }
    auto directory = path.value() / category_name;
    std::filesystem::path filename = link_name;
    filename += std::filesystem::path{ L".lnk" };
    auto link_path = directory / filename;
    return std::filesystem::exists(link_path);
}

// Creates a Windows start menu entry, may throw an exception.
inline bool create_start_menu_entry(std::filesystem::path const& target_path,
    std::filesystem::path const& link_name,
    std::filesystem::path const& category_name)
{
    if (link_name.has_parent_path()) {
        throw std::invalid_argument(
            "the start menu entry link name cannot have a parent path");
    }
    if (category_name.has_parent_path()) {
        throw std::invalid_argument(
            "the start menu entry category cannot have a parent path");
    }
    auto path = programs_path();
    if (!path.has_value()) {
        throw std::runtime_error(
            "failed to retrieve the start menu shortcuts location");
    }
    auto directory = path.value() / category_name;
    std::filesystem::create_directories(directory);
    std::filesystem::path filename = link_name;
    filename += std::filesystem::path{ L".lnk" };
    auto link_path = directory / filename;
    if (std::filesystem::exists(link_path)) {
        std::filesystem::remove(link_path);
    }
    // Source: https://stackoverflow.com/a/33841912/6748004
    CoInitialize(NULL);
    IShellLinkW* link = NULL;
    auto result = CoCreateInstance(
        CLSID_ShellLink, NULL, CLSCTX_ALL, IID_IShellLinkW, (void**)&link);
    if (SUCCEEDED(result) && link != NULL) {
        link->SetPath(target_path.c_str());
        // link->SetDescription(L"Shortcut Description");
        link->SetIconLocation(target_path.c_str(), 0);
        IPersistFile* persist;
        result = link->QueryInterface(IID_IPersistFile, (void**)&persist);
        if (SUCCEEDED(result) && persist != NULL) {
            result = persist->Save(link_path.c_str(), TRUE);
            persist->Release();
        } else {
            link->Release();
            return false;
        }
        link->Release();
    } else {
        return false;
    }
    return true;
}

} // namespace ungive::update::internal::win
