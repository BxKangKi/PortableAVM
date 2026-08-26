#include "core/StringUtil.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pavm {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

std::vector<std::string> split(std::string_view value, char delimiter, bool keepEmpty) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t next = value.find(delimiter, start);
        const std::size_t end = next == std::string_view::npos ? value.size() : next;
        if (keepEmpty || end > start) {
            result.emplace_back(value.substr(start, end - start));
        }
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }
    return result;
}

std::string join(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << separator;
        }
        out << values[i];
    }
    return out.str();
}

std::string replaceAll(std::string value, std::string_view needle, std::string_view replacement) {
    if (needle.empty()) {
        return value;
    }
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return value;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open file for reading: " + pathToUtf8(path));
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

void writeTextFileAtomic(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp-" + randomToken();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Cannot open temporary file for writing: " + temporary);
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error("Failed while writing: " + temporary);
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace file: " + pathToUtf8(path) + ": " + ec.message());
    }
}

std::string pathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return wideToUtf8(path.native());
#else
    return path.string();
#endif
}

std::filesystem::path pathFromUtf8(std::string_view value) {
#ifdef _WIN32
    return std::filesystem::path(utf8ToWide(value));
#else
    return std::filesystem::path(value);
#endif
}

std::string quoteForLog(std::string_view value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

std::optional<int> parseInt(std::string_view value) {
    int parsed = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, parsed);
    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return parsed;
}

bool parseBool(std::string_view value, bool fallback) {
    const std::string lower = toLowerAscii(trim(value));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }
    return fallback;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string timestampForLog() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string randomToken() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream out;
    out << std::hex << distribution(generator);
    return out.str();
}

#ifdef _WIN32
std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("Invalid UTF-8 string");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::runtime_error("Invalid UTF-16 string");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}
#endif

} // namespace pavm
