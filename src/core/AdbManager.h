#pragma once

#include "core/Logger.h"
#include "core/SdkManager.h"

#include <filesystem>
#include <string>

namespace pavm {

class AdbManager {
public:
    AdbManager(SdkManager& sdk, Logger& logger);

    void installApk(const std::filesystem::path& apk);
    std::string devices();
    std::string shell(const std::string& command);
    void stopServer();

private:
    SdkManager& sdk_;
    Logger& logger_;
};

} // namespace pavm
