#pragma once

#include "core/Config.h"
#include "core/Logger.h"
#include "core/PortablePaths.h"
#include "core/SdkManager.h"

#include <string>
#include <vector>

namespace pavm {

struct AvdInfo {
    std::string name;
    std::string systemImagePackage;
    std::string hardwareProfile;
    std::string abi;
    int apiLevel = 0;
    int ramMb = 0;
    int cpuCores = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    int densityDpi = 0;
    bool launchable = false;
    std::string problem;
};

class AvdManager {
public:
    AvdManager(const PortablePaths& paths, SdkManager& sdk, Logger& logger);

    void createOrReplace(const AppConfig& config);
    void deleteAvd(const std::string& name);
    void applyHardwareConfig(const AppConfig& config);
    [[nodiscard]] std::vector<std::string> listAvds() const;
    [[nodiscard]] std::vector<AvdInfo> listAvdInfos() const;
    [[nodiscard]] AppConfig loadConfig(const std::string& name, const AppConfig& defaults) const;
    [[nodiscard]] bool systemImageInUse(const std::string& package) const;
    [[nodiscard]] bool exists(const std::string& name) const;
    [[nodiscard]] bool launchable(const AppConfig& config) const;
    [[nodiscard]] std::string launchProblem(const AppConfig& config) const;
    [[nodiscard]] std::filesystem::path avdDirectory(const std::string& name) const;
    [[nodiscard]] std::filesystem::path systemImageDirectory(const AppConfig& config) const;

private:
    const PortablePaths& paths_;
    SdkManager& sdk_;
    Logger& logger_;
};

} // namespace pavm
