#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>

#include <winrt/base.h>

// TODO: this is only required on Windows, not other platforms (probably).

namespace ungive::update
{

// Represents a launcher and all its DLL dependencies,
// which it needs if it is copied to another directory.
// DLLs are only copied from the directory
// in which the launcher executable is located.
// DLLs that do not exist in that directory are ignored.
class launcher
{
public:
    // Constructs a new launcher instance.
    // The executable must be either an absolute path and
    // the DLLs must be located in the same directory as the executable,
    // or a relative path and the directory must be set explicitly
    // by calling the working_directory() method.
    // DLLs may not be relative paths.
    // The DLL names are case-insensitive on Windows.
    launcher(std::filesystem::path const& executable_path,
        std::vector<std::filesystem::path> const& dlls)
        : m_dlls{ dlls }
    {
        if (!executable_path.has_filename()) {
            throw std::invalid_argument("executable does not have a filename");
        }
        if (executable_path.has_parent_path()) {
            m_working_directory = executable_path.parent_path();
        }
        m_executable = executable_path.filename();
        for (auto const& dll : m_dlls) {
            if (dll.has_parent_path()) {
                throw std::runtime_error("dlls may not have a parent paths");
            }
        }
    }

    // Returns the executable path of this launcher,
    // which may be relative or absolute,
    // depending on whether a working directory has been set.
    inline std::filesystem::path executable() const
    {
        if (m_working_directory.has_value()) {
            return m_working_directory.value() / m_executable;
        }
        return m_executable;
    }

    // Sets the current working directory of the executable,
    // which is the directory where the executable is expected to be
    // and where the DLL dependencies are expected to be located.
    // If the executable path that was given to the constructor
    // was an absolute path, then this method replaces its parent path.
    inline void working_directory(std::filesystem::path const& directory)
    {
        m_working_directory = directory;
    }

    // Returns the current working directory.
    // May be empty if the initial launcher executable was a relative path
    // and the working directory has not been set explicitly.
    inline std::optional<std::filesystem::path> const& working_directory() const
    {
        return m_working_directory;
    }

    // Copies the launcher executable and existing DLL dependencies
    // into the specified directory, and returns the absolute path
    // to the executable in the location it was copied to.
    // DLL dependencies that do not exist in the workin directory
    // are not copied and are ignored.
    // They are expected to be present on the system,
    // or they should have been shipped with the software.
    // The working directory must have been set implicitly
    // via the constructor by passing an absolute executable path,
    // or explicitly by having called the working_directory() method first.
    std::filesystem::path copy_to(std::filesystem::path const& directory)
    {
        std::filesystem::create_directories(directory);
        auto target = copy_file(m_executable, directory);
        for (auto const& dll : m_dlls) {
            copy_file(dll, directory, false);
        }
        return target;
    }

private:
    inline std::filesystem::path copy_file(
        std::filesystem::path const& relative_source,
        std::filesystem::path const& target_directory)
    {
        return copy_file(relative_source, target_directory, true).value();
    }

    std::optional<std::filesystem::path> copy_file(
        std::filesystem::path const& relative_source,
        std::filesystem::path const& target_directory, bool must_exist)
    {
        if (!m_working_directory.has_value()) {
            throw std::runtime_error("unknown working directory");
        }
        auto source = m_working_directory.value() / relative_source;
        if (!std::filesystem::exists(source)) {
            if (must_exist) {
                throw std::runtime_error("launcher file does not exist: " +
                    winrt::to_string(source.wstring()));
            }
            return std::nullopt;
        }
        // Get the proper casing of the filename.
        auto canonical = std::filesystem::canonical(source);
        if (!canonical.has_filename()) {
            throw std::runtime_error("source does not have a filename");
        }
        auto target = target_directory / canonical.filename();
        std::filesystem::copy(source, target);
        return target;
    }

    std::optional<std::filesystem::path> m_working_directory;
    std::filesystem::path m_executable{};
    std::vector<std::filesystem::path> m_dlls;
};

} // namespace ungive::update
