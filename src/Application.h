#pragma once

#include "core/AdbManager.h"
#include "core/ApkInspector.h"
#include "core/AvdManager.h"
#include "core/Config.h"
#include "core/EmulatorManager.h"
#include "core/Logger.h"
#include "core/LanguageManager.h"
#include "core/PortablePaths.h"
#include "core/SdkManager.h"
#include "core/TaskQueue.h"
#include "ui/ImmediateUi.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

namespace pavm {

class Application {
public:
    Application();
    ~Application();
    int run();

private:
    void enqueue(const std::string& label, std::function<void()> task);
    void setStatus(const std::string& value);
    std::string status() const;
    void saveConfig();
    [[nodiscard]] std::string tr(const std::string& key) const;
    [[nodiscard]] std::string trf(const std::string& key, const std::vector<std::string>& values) const;
    void renderSetup(ImmediateUi& ui);
    void renderDevices(ImmediateUi& ui);
    void renderSdkManagement(ImmediateUi& ui);
    void renderApps(ImmediateUi& ui);
    void renderLogs(ImmediateUi& ui);
    void renderLanguage(ImmediateUi& ui);
    void renderEmulatorControls(ImmediateUi& ui);
    void refreshHardwareProfiles();
    void refreshDeviceList();
    void refreshSdkPackageList();
    void selectDevice(const std::string& name);
    void prepareNewDevice();
    void applyCompatibilityDefaults();
    [[nodiscard]] bool launchReady() const;
    [[nodiscard]] std::string launchReadinessText() const;
    [[nodiscard]] bool confirmCloseIfEmulatorRunning();
    [[nodiscard]] bool confirmDelete(const std::string& title, const std::string& message);

    PortablePaths paths_;
    AppConfig config_;
    Logger logger_;
    LanguageManager language_;
    SdkManager sdk_;
    AvdManager avd_;
    EmulatorManager emulator_;
    AdbManager adb_;
    TaskQueue tasks_;

    mutable std::mutex stateMutex_;
    std::string status_;
    ApkInspection apkInspection_;
    std::string adbDevices_;
    std::vector<HardwareProfileInfo> hardwareProfiles_;
    std::vector<AvdInfo> avdInfos_;
    std::vector<InstalledSdkPackage> sdkPackages_;
    bool hardwareProfilesLoaded_ = false;
    bool deviceListLoaded_ = false;
    bool sdkPackageListLoaded_ = false;
    std::string selectedAvdName_;
    std::string selectedSdkPackage_;
    std::atomic_bool refreshDevicesRequested_{false};
    std::atomic_bool refreshSdkRequested_{false};
    std::string selectedTab_ = "devices";
    SDL_Window* window_ = nullptr;
#ifdef _WIN32
    void* nativeWindowIcon_ = nullptr;
#endif
};

} // namespace pavm
