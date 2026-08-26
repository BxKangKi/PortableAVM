#include "core/LanguageManager.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pavm {
namespace {

std::string unescape(std::string value) {
    value = replaceAll(std::move(value), "\\n", "\n");
    value = replaceAll(std::move(value), "\\t", "\t");
    return value;
}

} // namespace

LanguageManager::LanguageManager(std::filesystem::path directory)
    : directory_(std::move(directory)), english_(load("en")), current_(load("ko")) {
    if (current_.empty()) current_ = english_;
}

LanguageManager::Table LanguageManager::load(const std::string& code) const {
    Table table;
    const auto path = directory_ / (code + ".lang");
    std::ifstream input(path, std::ios::binary);
    if (!input) return table;
    std::string line;
    bool first = true;
    while (std::getline(input, line)) {
        if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = trim(std::string_view(line).substr(0, equal));
        if (key.empty()) continue;
        table[key] = unescape(trim(std::string_view(line).substr(equal + 1)));
    }
    return table;
}

void LanguageManager::setLanguage(const std::string& code) {
    Table selected = load(code);
    std::string selectedCode = code;
    if (selected.empty()) {
        selected = english_;
        selectedCode = "en";
    }
    if (selected.empty()) {
        selected = load("ko");
        selectedCode = "ko";
    }
    std::scoped_lock lock(mutex_);
    language_ = selectedCode;
    current_ = std::move(selected);
}

std::string LanguageManager::language() const {
    std::scoped_lock lock(mutex_);
    return language_;
}

std::string LanguageManager::text(const std::string& key) const {
    std::scoped_lock lock(mutex_);
    if (const auto it = current_.find(key); it != current_.end()) return it->second;
    if (const auto it = english_.find(key); it != english_.end()) return it->second;
    return key;
}

std::string LanguageManager::languageName(const std::string& code) const {
    const Table target = load(code);
    if (const auto it = target.find("language.name"); it != target.end()) return it->second;
    std::scoped_lock lock(mutex_);
    const std::string key = "language." + code;
    if (const auto it = current_.find(key); it != current_.end()) return it->second;
    if (const auto it = english_.find(key); it != english_.end()) return it->second;
    return code;
}

std::vector<std::string> LanguageManager::availableLanguages() const {
    std::vector<std::string> result;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory_, ec)) return {"ko", "en"};
    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".lang") result.push_back(entry.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (std::find(result.begin(), result.end(), "ko") == result.end()) result.push_back("ko");
    if (std::find(result.begin(), result.end(), "en") == result.end()) result.push_back("en");
    return result;
}

} // namespace pavm
