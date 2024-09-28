#pragma once

#include <functional>
#include <string>

namespace ungive::update
{

// Log level for update logging.
// Currently only the warning level is used.
enum class log_level
{
    verbose = 0,
    debug,
    info,
    warning,
    error,
    fatal
};

using logger_func =
    std::function<void(log_level level, std::string const& message)>;

// Get or set the update logger instance.
// The logger is used for message that cannot be communicated with exceptions,
// e.g. because exceptions are caught and ignored.
// Must be set before any update operations are performed in other threads
// any may not be updated while update operations could be in progress.
inline logger_func& logger()
{
    // No-op by default.
    static logger_func logger = [](auto level, auto message) {};
    return logger;
}

} // namespace ungive::update
