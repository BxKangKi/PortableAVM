#pragma once

#include <filesystem>

namespace pavm {

class SingleInstance {
public:
    explicit SingleInstance(const std::filesystem::path& lockFile);
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    [[nodiscard]] bool acquired() const;

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace pavm
