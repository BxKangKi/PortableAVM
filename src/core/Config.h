#pragma once

#include <filesystem>
#include <string>

namespace pavm {

struct AppConfig {
    bool sdkDownloadConsent = false;
    std::string consentDocument = "Android SDK Terms";
    std::string consentTimestamp;

    int apiLevel = 30;
    std::string imageTag = "google_apis_playstore";
    std::string abi;
    std::string avdName = "PortableAVM_Game";
    std::string hardwareProfile = "pixel_7";

    int ramMb = 6144;
    int cpuCores = 4;
    int dataPartitionGb = 64;
    int screenWidth = 1080;
    int screenHeight = 2400;
    int densityDpi = 420;

    std::string gpuMode = "host";
    bool hardwareAcceleration = true;
    bool coldBoot = false;
    bool wipeDataNextBoot = false;
    bool saveSnapshot = true;
    bool noAudio = false;
    bool noBootAnimation = true;
    int adbPort = 5038;

    std::string apkPath;
    std::string jdkSourcePath;
    std::string commandLineToolsArchivePath;
    std::string language = "ko";

    void normalize();
    [[nodiscard]] std::string systemImagePackage() const;
    [[nodiscard]] bool isArmImage() const;
    [[nodiscard]] bool isPlayStoreImage() const;

    static AppConfig load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

bool isSafeAvdName(const std::string& value);
bool isForbiddenIdentityProperty(const std::string& key);

} // namespace pavm
