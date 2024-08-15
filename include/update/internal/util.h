#pragma once

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <utility>

namespace update::internal
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
        path = tmp_dir / ss.str();
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
    std::ifstream t(path, mode);
    t.seekg(0, std::ios::end);
    size_t size = t.tellg();
    std::string buffer(size, '_');
    t.seekg(0);
    t.read(&buffer[0], size);
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

// Splits host and path components of a URL.
inline std::pair<std::string, std::string> split_host_path(
    std::string const& url)
{
    bool slash = false;
    size_t i = 0;
    for (; i < url.size(); i++) {
        char c = url[i];
        if (c == '/') {
            if (slash) {
                // scheme
                slash = false;
                continue;
            }
            slash = true;
            continue;
        }
        if (slash) {
            // split on previous index
            i = i - 1;
            break;
        }
    }
    return std::make_pair(url.substr(0, i), url.substr(i));
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

} // namespace update::internal
