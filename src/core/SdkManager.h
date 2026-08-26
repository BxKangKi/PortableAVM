#pragma once

#include "core/Logger.h"
#include "core/PortablePaths.h"
#include "core/Process.h"
#include "core/RepositoryParser.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pavm {

struct InstalledSdkPackage {
    std::string packagePath;
    std::string displayName;
    std::string revision;
    std::string kind;
    std::filesystem::path directory;
};

class SdkManager {
public:
    SdkManager(const PortablePaths& paths, Logger& logger, int adbPort);

    [[nodiscard]] bool commandLineToolsInstalled() const;
    [[nodiscard]] bool emulatorInstalled() const;
    [[nodiscard]] bool platformToolsInstalled() const;
    [[nodiscard]] bool jdkInstalled() const;
    [[nodiscard]] bool packageInstalled(const std::string& package) const;

    void importCommandLineToolsArchive(const std::filesystem::path& archive);
    void installPackages(const std::vector<std::string>& packages);
    void uninstallPackage(const std::string& package);
    [[nodiscard]] std::vector<InstalledSdkPackage> installedPackages() const;
    std::vector<std::string> availableSystemImages();
    std::vector<HardwareProfileInfo> availableHardwareProfiles();
    bool openLicenseTerminal();
    void importJdk(const std::filesystem::path& source);

    [[nodiscard]] std::filesystem::path sdkManagerExecutable() const;
    [[nodiscard]] std::filesystem::path avdManagerExecutable() const;
    [[nodiscard]] std::filesystem::path emulatorExecutable() const;
    [[nodiscard]] std::filesystem::path adbExecutable() const;
    [[nodiscard]] ProcessEnvironment environment() const;

private:
    void requireJdk() const;
    void requireCommandLineTools() const;
    void logProcessChunk(const std::string& chunk);

    const PortablePaths& paths_;
    Logger& logger_;
    int adbPort_;
};

} // namespace pavm
