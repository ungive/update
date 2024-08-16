#pragma once

#include <filesystem>
#include <optional>

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

inline std::optional<std::filesystem::path> local_appdata_path(
    std::string const& application_id = "")
{
    auto path = csidl_path(CSIDL_LOCAL_APPDATA);
    if (!path.has_value()) {
        return std::nullopt;
    }
    if (application_id.empty()) {
        return path.value();
    }
    return path.value() / application_id;
}

inline std::optional<std::filesystem::path> appdata_path(
    std::string const& application_id = "")
{
    auto path = csidl_path(CSIDL_APPDATA);
    if (!path.has_value()) {
        return std::nullopt;
    }
    if (application_id.empty()) {
        return path.value();
    }
    return path.value() / application_id;
}

inline std::optional<std::filesystem::path> programs_path(
    std::string const& application_id = "")
{
    auto path = csidl_path(CSIDL_PROGRAMS);
    if (!path.has_value()) {
        return std::nullopt;
    }
    if (application_id.empty()) {
        return path.value();
    }
    return path.value() / application_id;
}

inline bool has_start_menu_entry(std::filesystem::path const& target_path,
    std::string const& link_name, std::string const& application_id = "")
{
    auto directory = programs_path(application_id);
    if (!directory.has_value()) {
        return false;
    }
    auto link_path = directory.value() / (link_name + ".lnk");
    return std::filesystem::exists(link_path);
}

inline bool create_start_menu_entry(std::filesystem::path const& target_path,
    std::string const& link_name, std::string const& application_id = "")
{
    auto directory = programs_path(application_id);
    if (!directory.has_value()) {
        return false;
    }
    std::filesystem::create_directories(directory.value());
    auto link_path = directory.value() / (link_name + ".lnk");
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
