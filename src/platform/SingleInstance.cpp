#include "platform/SingleInstance.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace pavm {

SingleInstance::SingleInstance(const std::filesystem::path& lockFile) {
    std::filesystem::create_directories(lockFile.parent_path());
#ifdef _WIN32
    handle_ = CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) handle_ = nullptr;
#else
    fd_ = open(lockFile.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

SingleInstance::~SingleInstance() {
#ifdef _WIN32
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#else
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
    }
#endif
}

bool SingleInstance::acquired() const {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

} // namespace pavm
