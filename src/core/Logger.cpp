#include "core/Logger.h"
#include "core/StringUtil.h"

#include <fstream>

namespace pavm {

Logger::Logger(std::filesystem::path file) : file_(std::move(file)) {
    std::filesystem::create_directories(file_.parent_path());
}

void Logger::info(const std::string& message) { write("INFO", message); }
void Logger::warn(const std::string& message) { write("WARN", message); }
void Logger::error(const std::string& message) { write("ERROR", message); }
void Logger::appendRaw(const std::string& message) { write("PROC", message); }

std::vector<std::string> Logger::snapshot(std::size_t maximum) const {
    std::scoped_lock lock(mutex_);
    const std::size_t start = lines_.size() > maximum ? lines_.size() - maximum : 0;
    return {lines_.begin() + static_cast<std::ptrdiff_t>(start), lines_.end()};
}

void Logger::write(const char* level, const std::string& message) {
    const std::string line = "[" + timestampForLog() + "] [" + level + "] " + message;
    std::scoped_lock lock(mutex_);
    lines_.push_back(line);
    while (lines_.size() > 3000) {
        lines_.pop_front();
    }
    std::ofstream output(file_, std::ios::binary | std::ios::app);
    if (output) {
        output << line << '\n';
    }
}

} // namespace pavm
