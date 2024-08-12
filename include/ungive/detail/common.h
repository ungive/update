#pragma once

#include <fstream>
#include <functional>
#include <ios>
#include <sstream>
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

    template <typename Iterator>
    version_number(Iterator begin, Iterator end) : m_components{ begin, end }
    {
    }

    template <typename... Args>
    version_number(Args... args) : version_number({ args... })
    {
    }

    static version_number from_string(
        std::string const& version, std::string const& prefix = "")
    {
        auto p = version.find(prefix, 0);
        if (p == std::string::npos) {
            throw std::runtime_error("prefix not found");
        }
        auto i = prefix.size();
        size_t k = 0;
        std::vector<int> components;
        while (true) {
            k = version.find('.', i);
            int n = 0;
            size_t end = std::min(version.size(), k);
            for (size_t j = i; j < end; j++) {
                char c = version.at(j);
                if (c < '0' || c > '9') {
                    throw std::runtime_error(
                        "version string contains non-digits");
                }
                n *= 10;
                n += c - '0';
            }
            components.push_back(n);
            if (k == std::string::npos) {
                break;
            }
            i = k + 1;
        }
        return version_number(components.begin(), components.end());
    }

    std::string string(std::string const& separator = ".") const
    {
        std::ostringstream oss;
        bool dot = false;
        for (auto component : m_components) {
            if (dot)
                oss << separator;
            oss << component;
            dot = true;
        }
        return oss.str();
    }

    inline size_t size() const { return m_components.size(); }

    inline int at(size_t i) const { return m_components.at(i); }

    inline std::vector<int>::const_iterator begin() const
    {
        return m_components.cbegin();
    }

    inline std::vector<int>::const_iterator end() const
    {
        return m_components.cend();
    }

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
