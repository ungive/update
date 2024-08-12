#pragma once

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <utility>

#include <openssl/sha.h>

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

} // namespace ungive::update::internal
