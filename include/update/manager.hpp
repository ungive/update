#ifndef UNGIVE_UPDATE_MANAGER_H_
#define UNGIVE_UPDATE_MANAGER_H_

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "update/detail/common.h"
#include "update/internal/sentinel.h"

#ifdef WIN32
#include "update/internal/win/process.h"
#endif

#define DEFAULT_LATEST_DIRECTORY "current"

namespace update
{

// Represents a manager for automatic updates.
//
// Manages multiple version of the application in a "working directory",
// which could e.g. be "%LOCALAPPDATA%/yourapp" on Windows.
// Any newly downloaded version is placed in a directory
// with its respective version number, e.g. "1.2.5".
//
// There is also a special directory "latest", which contains the application,
// just like the version directories, but the main executable and the launcher
// always make sure that any latest update is placed into this directory.
// This serves two purposes:
// 1. The application's executable path always stays the same and
// 2. if your application has a tray icon, that icon will remain visible,
//    if the user chose to put it into the task bar.
// If we were to always launch the latest version from its version directory,
// e.g. an updated version "1.2.6", then if the user had moved the tray icon
// into the visible area before it would disappear once the update is started.
// This is often undesirable, therefore the use of a "latest" directory.
// Windows does not expose any publicly documented API
// for making a tray icon visible programmatically,
// so this is a necessary workaround.
//
// Any program that uses this library should therefore have two programs:
// the main executable and an accompanying launcher executable.
// The launcher executable should have no additional DDL dependencies
// and should be copyable elsewhere without exiting abnormally.
//
// The main executable is expected to download new versions in the background
// during normal usage of the application. The launcher should then be used
// to apply the latest update, by moving that update to the "latest" directory.
// Example code for the main and launcher executable can be found
// in the documentation of this class's methods.
class manager
{
public:
    manager(std::filesystem::path const& working_directory,
        version_number const& current_version)
        : m_working_directory{ working_directory },
          m_current_version{ current_version }
    {
    }

    // Returns the name of the latest directory.
    std::string const& latest_directory() const { return m_latest_directory; }

    // Sets the latest directory to a custom name.
    // Note that this method must be called before calling any other methods.
    void latest_directory(std::string const& name)
    {
        m_latest_directory = name;
    }

    // Returns the latest installed version in the manager's working directory,
    // excluding the version that might be present in the "latest" directory.
    // May throw an exception if any error occurs.
    std::optional<std::pair<version_number, std::filesystem::path>>
    latest_available_update() const
    {
        std::filesystem::create_directories(m_working_directory);
        auto it = std::filesystem::directory_iterator(m_working_directory);
        std::optional<std::pair<version_number, std::filesystem::path>> result;
        for (auto const& entry : it) {
            if (!entry.is_directory() || !entry.path().has_filename()) {
                continue;
            }
            auto filename = entry.path().filename().string();
            version_number directory_version;
            try {
                directory_version = version_number::from_string(filename);
            }
            catch (...) {
                // The path does not contain a valid version number.
                continue;
            }
            internal::sentinel sentinel(entry.path());
            if (!sentinel.read()) {
                // The sentinel does not exist or has an invalid format.
                continue;
            }
            if (directory_version != sentinel.version()) {
                // The sentinel file contains a different version.
                continue;
            }
            if (result.has_value() && directory_version == result->first) {
                // two directories represent the same version,
                // e.g. "2.1" and "2.1.0". this should not happen in practice,
                // but if it does, simply return nothing,
                // so that the caller clears and redownloads the newest version,
                // as the working directory is in an inconsistent state.
                return std::nullopt;
            }
            if (!result.has_value() || directory_version > result->first) {
                result = std::make_pair(directory_version, entry.path());
            }
        }
        return result;
    }

    // Deletes all directories in the manager's working directory,
    // except the one of the current process.
    // This is useful if automatic updates should be disabled
    // and the application should not launch any new versions anymore.
    // Do not call this while an update is in progress.
    // May throw an exception if any error occurs.
    void unlink() const
    {
        std::unordered_set<std::filesystem::path> exclude_directories;
        // Exclude the directory in which the current process is executing.
        // Other than that we don't exclude any other directories.
        auto process = internal::win::current_process_executable();
        if (process.has_parent_path()) {
            auto path = std::filesystem::relative(process, m_working_directory);
            if (path.begin() != path.end()) {
                auto root_directory = *path.begin();
                exclude_directories.insert(root_directory);
            }
        }
        unlink_files(exclude_directories);
    }

    // Prunes all files in the manager's working directory
    // except the subdirectory for the given current version
    // the subdirectory for the newest installed version
    // and the subdirectory for the latest version
    // as indicated by the return value of latest_available_update().
    // May throw an exception if any error occurs.
    void prune()
    {
        std::unordered_set<std::filesystem::path> exclude_directories;
        exclude_directories.insert(m_latest_directory);
        exclude_directories.insert(m_current_version.string());
        auto latest_installed = latest_available_update();
        if (latest_installed.has_value()) {
            exclude_directories.insert(latest_installed->first.string());
        }
        // Exclude the directory in which the current process is executing.
        auto process = internal::win::current_process_executable();
        if (process.has_parent_path()) {
            auto path = std::filesystem::relative(process, m_working_directory);
            if (path.begin() != path.end()) {
                auto root_directory = *path.begin();
                exclude_directories.insert(root_directory);
            }
        }
        unlink_files(exclude_directories);
    }

    // Only call this method from the main executable.
    //
    // Launches the latest version of the application with the given launcher,
    // which must be the one that shipped with the executable
    // of the current process. If the path is relative, it will be resolved
    // relative to the current executable's directory,
    // otherwise the absolute path will be used.
    // This function returns true if the launcher was started
    // or false if there is no newer version than the one of the current process
    // or starting the launcher failed.
    // If true is returned, the application should terminate.
    //
    // The application is expected to ship with
    // a standalone launcher executable which is used to apply updates
    // and launch the latest version from a separate process.
    // The launcher must come without any external DLL dependencies
    // and should use this manager class to apply the latest update
    // and then launch the latest version, like so:
    //
    //  auto manager = create_manager();
    //  manager.apply_latest();
    //  manager.launch_latest();
    //
    // The launcher must know the working directory and the current version
    // of the application (e.g. via compile-time constants).
    // During the execution of launch_latest() the launcher executable
    // is copied to a temporary directory where it is executed,
    // it should therefore not depend on the process's working directory.
    //
    // Example usage of this method within the main executable:
    //
    //  manager.launch_latest("launcher.exe");
    //  std::cout << "latest version already running";
    //
    // May throw an exception if any error occurs.
    //
    bool launch_latest(std::filesystem::path const& launcher_executable,
        std::vector<std::string> const& launcher_arguments = {})
    {
        auto process = internal::win::current_process_executable();
        auto latest = internal::sentinel(latest_path());
        auto update = latest_available_update();

        bool is_process_latest = internal::is_subpath(process, latest_path());
        bool have_latest = latest.read();
        bool have_update = update.has_value();

        if (!have_latest && !have_update) {
            // There is nothing to launch, so return.
            return false;
        }
        // We have a newer version if:
        // - there is an update with a newer version number or
        // - we are not running the latest version and its version is newer.
        bool have_newer_version =
            (have_update && update->first > m_current_version) ||
            (!is_process_latest && have_latest &&
                latest.version() > m_current_version);
        // If there is a newer version, start the launcher and exit.
        if (have_newer_version) {
            auto executable = launcher_executable;
            if (executable.is_relative()) {
                executable = process.parent_path() / executable;
            }
            // Copy the launcher executable to a temporary directory,
            // since it might be sitting in a directory that will be
            // deleted or renamed by this launcher and it wouldn't be able
            // to do that if it were located in any of these directories.
            // Don't use a temporary directory of the system,
            // as we won't be able to clean that up ourself.
            // By using ".tmp" in the working directory of the manager,
            // the manager itself will clean this up soon enough.
            auto temp_directory =
                m_working_directory / ".tmp" / internal::random_string(8);
            std::filesystem::remove_all(temp_directory);
            std::filesystem::create_directories(temp_directory);
            auto copied_executable = temp_directory / executable.filename();
            std::filesystem::copy(executable, copied_executable);
            internal::win::start_process_detached(
                copied_executable, launcher_arguments);
            return true;
        }
        return false;
    }

    // Only call this method from the launcher executable.
    //
    // Applies the latest available update by moving it
    // into the latest directory and deleting the update directory.
    // Any executable that might be running in either of those directories
    // will be killed by this method if kill_processes is true.
    // Returns whether any update has been applied successfully.
    //
    // Usage example within the launcher executable:
    //
    //  auto manager = create_manager();
    //  manager.apply_latest();
    //  manager.launch_latest();
    //
    // May throw an exception if any error occurs.
    //
    bool apply_latest(bool kill_processes = true)
    {
        auto latest_directory = latest_path();
        auto latest = internal::sentinel(latest_directory);
        auto update = latest_available_update();
        if (update.has_value() &&
            (!latest.read() || latest.version() < update->first)) {
            // Kill any processes that were started in these directories.
            // Additionally delete the target directory.
            if (std::filesystem::exists(latest_directory)) {
                if (kill_processes) {
                    internal::win::kill_processes(latest_directory);
                }
                std::filesystem::remove_all(latest_directory);
            }
            if (update.has_value() && std::filesystem::exists(update->second)) {
                if (kill_processes) {
                    internal::win::kill_processes(update->second);
                }
            }
            // Rename update to the latest name and delete the update directory.
            std::filesystem::rename(update->second, latest_directory);
            std::filesystem::remove_all(update->second);
            return true;
        }
        return false;
    }

    // Only call this method from the launcher executable.
    //
    // Starts the version of the application that is in the latest directory,
    // without applying the latest update or doing any other checks.
    // Call this after having called apply_latest().
    //
    // The path to the main executable must be a relative path
    // which will be resolved in relation to the latest directory.
    // It must therefore be relative to the root of the application directory.
    //
    void start_latest(std::filesystem::path const& main_executable,
        std::vector<std::string> const& main_arguments = {})
    {
        if (!main_executable.is_relative()) {
            throw std::invalid_argument(
                "the main executable path must be relative");
        }
        if (!std::filesystem::exists(latest_path())) {
            throw std::runtime_error("there is no latest version installed");
        }
        auto executable_path = latest_path() / main_executable;
        if (!std::filesystem::exists(executable_path)) {
            throw std::runtime_error("the specified main executable does not "
                                     "exist in the latest directory");
        }
        internal::win::start_process_detached(executable_path, main_arguments);
    }

private:
    void unlink_files(
        std::unordered_set<std::filesystem::path> excluded = {}) const
    {
        std::filesystem::create_directories(m_working_directory);
        auto it = std::filesystem::directory_iterator(m_working_directory);
        std::optional<std::pair<version_number, std::filesystem::path>> result;
        for (auto const& entry : it) {
            // Kill any processes that might live in these directories.
            // This might be inefficient in a loop, but in practice
            // there won't be many files in the working directory.
            internal::win::kill_processes(entry.path());
            if (!entry.is_directory() || !entry.path().has_filename()) {
                std::filesystem::remove_all(entry.path());
                continue;
            }
            auto filename = entry.path().filename().string();
            if (excluded.find(filename) != excluded.end()) {
                continue;
            }
            std::filesystem::remove_all(entry.path());
        }
    }

    inline std::filesystem::path latest_path()
    {
        return m_working_directory / m_latest_directory;
    }

    std::filesystem::path m_working_directory;
    version_number m_current_version;

    std::string m_latest_directory{ DEFAULT_LATEST_DIRECTORY };
};

} // namespace update

#undef DEFAULT_LATEST_DIRECTORY

#endif // UNGIVE_UPDATE_MANAGER_H_
