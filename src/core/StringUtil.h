#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pavm {

std::string trim(std::string_view value);
std::string toLowerAscii(std::string value);
bool startsWith(std::string_view value, std::string_view prefix);
bool endsWith(std::string_view value, std::string_view suffix);
std::vector<std::string> split(std::string_view value, char delimiter, bool keepEmpty = false);
std::string join(const std::vector<std::string>& values, std::string_view separator);
std::string replaceAll(std::string value, std::string_view needle, std::string_view replacement);
std::string readTextFile(const std::filesystem::path& path);
void writeTextFileAtomic(const std::filesystem::path& path, std::string_view content);
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(std::string_view value);
std::string quoteForLog(std::string_view value);
std::optional<int> parseInt(std::string_view value);
bool parseBool(std::string_view value, bool fallback = false);
std::string boolText(bool value);
std::string timestampForLog();
std::string randomToken();

#ifdef _WIN32
std::wstring utf8ToWide(std::string_view value);
std::string wideToUtf8(std::wstring_view value);
#endif

} // namespace pavm
