#pragma once

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <utility>

namespace ungive::update::internal
{

// Creates a uniquely-named, temporary directory.
inline std::filesystem::path create_temporary_directory(
    unsigned long long max_tries = 256)
{
    // Source: https://stackoverflow.com/a/58454949/6748004
    auto tmp_dir = std::filesystem::temp_directory_path();
    unsigned long long i = 0;
    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<uint64_t> rand(0);
    std::filesystem::path path;
    while (true) {
        std::stringstream ss;
        ss << std::hex << rand(prng);
        std::filesystem::path name = ss.str();
#ifdef LIBUPDATE_TEST_BUILD
        // Test the implementation with characters
        // that are represented differently on Windows with UTF-16.
        name += std::filesystem::path{ L"_\u00E9" };
#endif
        path = tmp_dir / name;
        if (std::filesystem::create_directory(path)) {
            break;
        }
        if (i == max_tries) {
            throw std::runtime_error("unable to create temporary directory");
        }
        i++;
    }
    return path;
}

// Creates a file with a specific name.
inline void touch_file(std::filesystem::path path)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f.close();
}

// Writes a string to a file.
inline void write_file(std::filesystem::path path, std::string const& content,
    std::ios::openmode mode = std::ios::out | std::ios::binary)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, mode);
    f.write(content.data(), content.size());
    f.flush();
    f.close();
}

// Reads the contents of a file into a string.
inline std::string read_file(
    std::filesystem::path path, std::ios::openmode mode = std::ios::in)
{
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "file to read does not exist: " + path.string());
    }
    std::ifstream ifd(path, std::ios::binary | std::ios::ate);
    std::streampos size = ifd.tellg();
    ifd.seekg(0, std::ios::beg);
    std::string buffer(size, '_');
    ifd.read(buffer.data(), size);
    return buffer;
}

// Generates a random, alphanumeric string.
inline std::string random_string(std::size_t length)
{
    static const std::string alphabet =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device dev;
    std::mt19937 generator(dev());
    std::uniform_int_distribution<> distribution(
        0, static_cast<int>(alphabet.size()) - 1);
    std::string str(length, ' ');
    for (std::size_t i = 0; i < length; ++i) {
        str[i] = alphabet[distribution(generator)];
    }
    return str;
}

// Ensures that the given text has the given prefix, if it isn't empty
inline std::string ensure_nonempty_prefix(std::string const& text, char prefix)
{
    if (text.size() > 0 && text.at(0) != prefix) {
        return prefix + text;
    }
    return text;
}

// Splits host and path components of a URL.
// The returned path component is guaranteed to start with a slash.
inline std::pair<std::string, std::string> split_host_path(
    std::string const& url)
{
    size_t i = 0;
    for (; i < url.size(); i++) {
        char c = url[i];
        if (url[i] == '/' && i + 1 < url.size() && url[i + 1] == '/') {
            // scheme
            i += 1;
            continue;
        }
        if (url[i] == '/') {
            // path slash
            break;
        }
    }
    auto host = url.substr(0, i);
    auto path = url.substr(i);
    return std::make_pair(host, ensure_nonempty_prefix(path, '/'));
}

// Strips leading slashes from a path.
inline std::string strip_leading_slash(std::string const& path)
{
    size_t i = 0;
    for (; i < path.size(); i++) {
        if (path.at(i) != '/') {
            break;
        }
    }
    return path.substr(i);
}

// Tests whether a string ends with another string.
inline bool string_ends_with(std::string const& text, std::string const& suffix)
{
    if (suffix.length() > text.length()) {
        return false;
    }
    auto offset = text.length() - suffix.length();
    return text.compare(offset, suffix.length(), suffix) == 0;
}

// Source: https://stackoverflow.com/a/74916567/6748004
inline bool is_subpath(
    const std::filesystem::path& path, const std::filesystem::path& base)
{
    const auto mismatch_pair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch_pair.second == base.end();
}

// Checks if a regex matches anywhere in a string.
inline bool regex_contains(std::string const& s, std::regex const& pattern)
{
    auto it = std::sregex_iterator(s.begin(), s.end(), pattern);
    return std::distance(it, std::sregex_iterator()) > 0;
}

} // namespace ungive::update::internal
