#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pavm {

class LanguageManager {
public:
    explicit LanguageManager(std::filesystem::path directory);

    void setLanguage(const std::string& code);
    [[nodiscard]] std::string language() const;
    [[nodiscard]] std::string text(const std::string& key) const;
    [[nodiscard]] std::string languageName(const std::string& code) const;
    [[nodiscard]] std::vector<std::string> availableLanguages() const;

private:
    using Table = std::map<std::string, std::string>;
    [[nodiscard]] Table load(const std::string& code) const;

    std::filesystem::path directory_;
    mutable std::mutex mutex_;
    std::string language_ = "ko";
    Table english_;
    Table current_;
};

} // namespace pavm
