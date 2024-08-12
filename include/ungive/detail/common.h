#pragma once

#include <fstream>
#include <functional>
#include <ios>
#include <string>
#include <unordered_map>

#include "ungive/detail/util.h"

namespace ungive::update
{

// Represents a downloaded file.
class downloaded_file
{
public:
    downloaded_file(std::string path) : m_path{ path } {}

    downloaded_file(std::filesystem::path path) : m_path{ path.string() } {}

    // The path of the file.
    std::string const& path() const { return m_path; }

    // Reads the entire file into a string.
    std::string read(std::ios::openmode mode = std::ios::in) const
    {
        if (!std::filesystem::exists(m_path)) {
            throw std::runtime_error("file to read does not exist: " + m_path);
        }
        std::ifstream t(m_path, mode);
        t.seekg(0, std::ios::end);
        size_t size = t.tellg();
        std::string buffer(size, '_');
        t.seekg(0);
        t.read(&buffer[0], size);
        return buffer;
    }

private:
    std::string m_path;
};

// Represents a generic version number.
class version_number
{
public:
    version_number(std::initializer_list<int> components)
        : m_components{ components }
    {
    }

    inline size_t size() const { return m_components.size(); }

    inline int at(size_t i) const { return m_components.at(i); }

    friend bool operator==(version_number const& lhs, version_number const& rhs)
    {
        return !(operator<(lhs, rhs)) && !(operator<(rhs, lhs));
    }

    friend bool operator<(version_number const& lhs, version_number const& rhs)
    {
        size_t n = std::min(lhs.size(), rhs.size());
        for (size_t i = 0; i < n; i++) {
            if (lhs.at(i) < rhs.at(i))
                return true;
            if (lhs.at(i) > rhs.at(i))
                return false;
        }
        bool is_left = lhs.size() > rhs.size();
        version_number const& rest = is_left ? lhs : rhs;
        for (size_t i = n; i < rest.size(); i++) {
            if (rest.at(i) < 0) {
                return is_left;
            }
            if (rest.at(i) > 0) {
                return !is_left;
            }
        }
        return false;
    }

    friend bool operator!=(version_number const& lhs, version_number const& rhs)
    {
        return !(operator==(lhs, rhs));
    }

    friend bool operator>(version_number const& lhs, version_number const& rhs)
    {
        return operator<(rhs, lhs);
    }

    friend bool operator<=(version_number const& lhs, version_number const& rhs)
    {
        return !(operator>(lhs, rhs));
    }

    friend bool operator>=(version_number const& lhs, version_number const& rhs)
    {
        return !(operator<(lhs, rhs));
    }

private:
    std::vector<int> m_components;
};

class file_url
{
public:
    file_url(std::string const& url) : m_url{ url }
    {
        auto [base, path] = internal::split_host_path(url);
        auto index = path.rfind('/');
        if (index != std::string::npos) {
            m_base_url = base + path.substr(0, index + 1);
            m_filename = path.substr(index + 1);
        } else {
            m_base_url = base;
            m_filename = path;
        }
    }

    std::string const& filename() const { return m_filename; }

    std::string const& base_url() const { return m_base_url; }

    std::string const& url() const { return m_url; }

private:
    std::string m_filename{};
    std::string m_base_url{};
    std::string m_url{};
};

} // namespace ungive::update
