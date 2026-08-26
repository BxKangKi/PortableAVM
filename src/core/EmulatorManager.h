#pragma once

#include "core/Config.h"
#include "core/Logger.h"
#include "core/Process.h"
#include "core/PortablePaths.h"
#include "core/SdkManager.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace pavm {

class EmulatorManager {
public:
    EmulatorManager(const PortablePaths& paths, SdkManager& sdk, Logger& logger);
    ~EmulatorManager();

    std::uint64_t start(const AppConfig& config);
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint64_t pid() const;
    [[nodiscard]] std::string accelerationDescription(const AppConfig& config) const;

private:
    const PortablePaths& paths_;
    SdkManager& sdk_;
    Logger& logger_;
    mutable std::mutex mutex_;
    ChildProcess process_;
#ifdef _WIN32
    mutable bool windowShown_ = false;
#endif
};

} // namespace pavm
