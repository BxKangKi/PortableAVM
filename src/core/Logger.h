#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pavm {

class Logger {
public:
    explicit Logger(std::filesystem::path file);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void appendRaw(const std::string& message);
    [[nodiscard]] std::vector<std::string> snapshot(std::size_t maximum = 500) const;

private:
    void write(const char* level, const std::string& message);

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
};

} // namespace pavm
