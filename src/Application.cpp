#include "Application.h"

#include "core/HostInfo.h"
#include "core/StringUtil.h"
#include "platform/NativeDialogs.h"
#include "ui/SkiaRenderer.h"

#include <SDL3/SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif


namespace pavm {
namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 820;
constexpr float kSidebarWidth = 164.0f;
constexpr float kStatusBarHeight = 44.0f;


} // namespace

Application::Application()
    : paths_(PortablePaths::discover()),
      config_(),
      logger_(paths_.logFile()),
      language_(paths_.lang),
      sdk_(paths_, logger_, 5038),
      avd_(paths_, sdk_, logger_),
      emulator_(paths_, sdk_, logger_),
      adb_(sdk_, logger_) {
    paths_.ensureLayout();
    config_ = AppConfig::load(paths_.settingsFile());
    selectedAvdName_ = config_.avdName;
    language_.setLanguage(config_.language);
    setStatus(tr("status.ready"));
    logger_.info("PortableAVM 시작. 루트: " + pathToUtf8(paths_.root));
    logger_.info("호스트: " + hostOsName() + " / " + hostArchitectureName());
}

Application::~Application() {
    tasks_.stop();
    emulator_.stop();
    try { adb_.stopServer(); } catch (...) {}
    try { saveConfig(); } catch (...) {}
}

int Application::run() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        throw std::runtime_error(std::string("SDL_Init 실패: ") + SDL_GetError());
    }
    window_ = SDL_CreateWindow("PortableAVM", kInitialWidth, kInitialHeight,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow 실패: ") + SDL_GetError());
    }
    SDL_SetWindowMinimumSize(window_, 960, 640);
#ifdef _WIN32
    {
        const auto iconPath = paths_.configs / "PortableAVM.ico";
        if (std::filesystem::is_regular_file(iconPath)) {
            const HICON icon = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 0, 0,
                                                              LR_LOADFROMFILE | LR_DEFAULTSIZE));
            if (icon) {
                const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
                HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
                if (hwnd) {
                    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                    nativeWindowIcon_ = icon;
                } else {
                    DestroyIcon(icon);
                }
            }
        }
    }
#endif
    SDL_StartTextInput(window_);

    SkiaRenderer renderer(window_);
    ImmediateUi navigationUi;
    ImmediateUi settingsUi;
    ImmediateUi statusUi;

    bool running = true;
    UiInput input;
    while (running) {
        input.mousePressed = false;
        input.mouseReleased = false;
        input.wheelY = 0;
        input.text.clear();
        input.backspace = false;
        input.deleteKey = false;
        input.moveLeft = false;
        input.moveRight = false;
        input.moveHome = false;
        input.moveEnd = false;
        input.enter = false;
        input.escape = false;
        input.copy = false;
        input.selectAll = false;
        bool closeRequested = false;

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    closeRequested = true;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    renderer.syncWindowMetrics();
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    renderer.windowToContent(event.motion.x, event.motion.y, input.mouseX, input.mouseY);
                    input.mouseDown = (event.motion.state & SDL_BUTTON_LMASK) != 0;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        input.mouseDown = true;
                        input.mousePressed = true;
                        renderer.windowToContent(event.button.x, event.button.y, input.mouseX, input.mouseY);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        input.mouseDown = false;
                        input.mouseReleased = true;
                        renderer.windowToContent(event.button.x, event.button.y, input.mouseX, input.mouseY);
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    input.wheelY += event.wheel.y;
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    input.text += event.text.text;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    input.backspace = input.backspace || event.key.key == SDLK_BACKSPACE;
                    input.deleteKey = input.deleteKey || event.key.key == SDLK_DELETE;
                    input.moveLeft = input.moveLeft || event.key.key == SDLK_LEFT;
                    input.moveRight = input.moveRight || event.key.key == SDLK_RIGHT;
                    input.moveHome = input.moveHome || event.key.key == SDLK_HOME;
                    input.moveEnd = input.moveEnd || event.key.key == SDLK_END;
                    input.enter = input.enter || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER;
                    input.escape = input.escape || event.key.key == SDLK_ESCAPE;
                    if ((event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
                        input.copy = input.copy || event.key.key == SDLK_C;
                        input.selectAll = input.selectAll || event.key.key == SDLK_A;
                    }
                    break;
                default:
                    break;
            }
        }

        if (closeRequested) {
            if (confirmCloseIfEmulatorRunning()) {
                running = false;
                continue;
            }
        }

        if (refreshDevicesRequested_.exchange(false)) refreshDeviceList();
        if (refreshSdkRequested_.exchange(false)) refreshSdkPackageList();

        renderer.syncWindowMetrics();
        const float width = renderer.contentWidth();
        const float height = renderer.contentHeight();
        SkCanvas* canvas = renderer.beginFrame();
        canvas->clear(SkColorSetARGB(255, 15, 17, 22));

        const float workHeight = std::max(1.0f, height - kStatusBarHeight);

        auto selectTab = [&](const std::string& tab) {
            if (selectedTab_ != tab) {
                selectedTab_ = tab;
                settingsUi.setScrollOffset(0.0f);
            }
        };
        auto renderSelectedSettings = [&] {
            if (selectedTab_ == "setup") renderSetup(settingsUi);
            else if (selectedTab_ == "devices") renderDevices(settingsUi);
            else if (selectedTab_ == "sdk") renderSdkManagement(settingsUi);
            else if (selectedTab_ == "apps") renderApps(settingsUi);
            else if (selectedTab_ == "logs") renderLogs(settingsUi);
            else renderLanguage(settingsUi);
        };

        // Launcher mode uses a permanent vertical tab rail. There is no
        // hamburger/collapse state: navigation is always one click away and
        // the selected settings page owns all remaining space.
        navigationUi.begin(canvas, input, 0, 0, kSidebarWidth, workHeight);
        navigationUi.heading(tr("app.title"));
        renderEmulatorControls(navigationUi);
        navigationUi.separator();
        if (navigationUi.compactTextItem("side-devices", tr("nav.devices"), selectedTab_ == "devices")) selectTab("devices");
        if (navigationUi.compactTextItem("side-sdk", tr("nav.sdk"), selectedTab_ == "sdk")) selectTab("sdk");
        if (navigationUi.compactTextItem("side-setup", tr("nav.setup"), selectedTab_ == "setup")) selectTab("setup");
        if (navigationUi.compactTextItem("side-apps", tr("nav.apps"), selectedTab_ == "apps")) selectTab("apps");
        if (navigationUi.compactTextItem("side-logs", tr("nav.logs"), selectedTab_ == "logs")) selectTab("logs");
        if (navigationUi.compactTextItem("side-language", tr("nav.language"), selectedTab_ == "language")) selectTab("language");
        navigationUi.end();

        const float contentX = kSidebarWidth + 1.0f;
        settingsUi.begin(canvas, input, contentX, 0, std::max(1.0f, width - contentX), workHeight);
        renderSelectedSettings();
        settingsUi.end();

        // Fixed bottom status bar: progress/errors stay visible independently of
        // the current settings tab and its scroll position.
        statusUi.begin(canvas, input, 0, workHeight, width, kStatusBarHeight);
        statusUi.label(status(), 12.5f);
        statusUi.end();

        renderer.present();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    SDL_StopTextInput(window_);
#ifdef _WIN32
    if (nativeWindowIcon_) {
        DestroyIcon(static_cast<HICON>(nativeWindowIcon_));
        nativeWindowIcon_ = nullptr;
    }
#endif
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return 0;
}

void Application::enqueue(const std::string& label, std::function<void()> task) {
    if (tasks_.busy()) {
        setStatus(tr("status.busy"));
        return;
    }
    setStatus(trf("status.in_progress", {label}));
    logger_.info(label + " 시작");
    tasks_.post([this, label, task = std::move(task)] {
        try {
            task();
            setStatus(trf("status.done", {label}));
            logger_.info(label + " 완료");
        } catch (const std::exception& error) {
            setStatus(trf("status.failed", {label, error.what()}));
            logger_.error(label + " 실패: " + error.what());
        } catch (...) {
            setStatus(trf("status.failed", {label, tr("status.unknown_error")}));
            logger_.error(label + " 실패: 알 수 없는 오류");
        }
    });
}

void Application::setStatus(const std::string& value) {
    std::scoped_lock lock(stateMutex_);
    status_ = value;
}

std::string Application::status() const {
    std::scoped_lock lock(stateMutex_);
    return status_;
}

std::string Application::tr(const std::string& key) const {
    return language_.text(key);
}

std::string Application::trf(const std::string& key, const std::vector<std::string>& values) const {
    std::string result = tr(key);
    for (std::size_t i = 0; i < values.size(); ++i) {
        result = replaceAll(std::move(result), "{" + std::to_string(i) + "}", values[i]);
    }
    return result;
}

void Application::saveConfig() {
    config_.normalize();
    config_.save(paths_.settingsFile());
}

bool Application::launchReady() const {
    return sdk_.jdkInstalled() && sdk_.commandLineToolsInstalled() &&
           sdk_.emulatorInstalled() && sdk_.platformToolsInstalled() &&
           sdk_.packageInstalled(config_.systemImagePackage()) &&
           avd_.launchable(config_);
}

std::string Application::launchReadinessText() const {
    std::vector<std::string> missing;
    if (!sdk_.jdkInstalled()) missing.emplace_back("JDK");
    if (!sdk_.commandLineToolsInstalled()) missing.emplace_back("Command-line Tools");
    if (!sdk_.emulatorInstalled()) missing.emplace_back("Emulator");
    if (!sdk_.platformToolsInstalled()) missing.emplace_back("ADB");
    if (!sdk_.packageInstalled(config_.systemImagePackage())) missing.emplace_back("System image");
    if (!avd_.launchable(config_)) {
        const auto problem = avd_.launchProblem(config_);
        missing.emplace_back(problem.empty() ? "AVD" : "AVD (" + problem + ")");
    }
    if (missing.empty()) return tr("emu.ready");
    return trf("emu.missing", {join(missing, ", ")});
}

void Application::renderEmulatorControls(ImmediateUi& ui) {
    const bool ready = launchReady();
    if (!selectedAvdName_.empty()) ui.muted(trf("emu.selected", {selectedAvdName_}));
    else ui.muted(tr("emu.no_selection"));
    if (ui.button("start-emulator", tr("emu.start"),
                  ready && !emulator_.running() && !tasks_.busy())) {
        const AppConfig copy = config_;
        enqueue(tr("task.emu_start"), [this, copy] {
            emulator_.start(copy);
        });
    }
    if (ui.button("stop-emulator", tr("emu.stop"),
                  emulator_.running() && !tasks_.busy())) {
        enqueue(tr("task.emu_stop"), [this] { emulator_.stop(); });
    }
    if (!ready && !emulator_.running()) ui.muted(launchReadinessText());
    else if (ready && !emulator_.running()) ui.success(tr("emu.ready"));
}


void Application::refreshHardwareProfiles() {
    hardwareProfilesLoaded_ = true;
    hardwareProfiles_.clear();
    if (!sdk_.commandLineToolsInstalled()) return;
    try {
        hardwareProfiles_ = sdk_.availableHardwareProfiles();
        const auto hasCurrent = std::any_of(hardwareProfiles_.begin(), hardwareProfiles_.end(), [this](const auto& p) {
            return p.id == config_.hardwareProfile;
        });
        if (!hasCurrent && !hardwareProfiles_.empty()) {
            static const char* preferred[] = {"pixel_7", "medium_phone", "pixel_6", "pixel"};
            auto chosen = hardwareProfiles_.begin();
            for (const char* wanted : preferred) {
                const auto it = std::find_if(hardwareProfiles_.begin(), hardwareProfiles_.end(), [wanted](const auto& p) {
                    return p.id == wanted;
                });
                if (it != hardwareProfiles_.end()) { chosen = it; break; }
            }
            config_.hardwareProfile = chosen->id;
            saveConfig();
        }
    } catch (const std::exception& error) {
        logger_.warn(std::string("하드웨어 프로필 조회 실패: ") + error.what());
    }
}

void Application::applyCompatibilityDefaults() {
    config_.apiLevel = 30;
    config_.imageTag = "google_apis_playstore";
    config_.abi = recommendedAbi();
    config_.gpuMode = "auto";
    config_.coldBoot = true;
    config_.saveSnapshot = false;
    if (!hardwareProfilesLoaded_) refreshHardwareProfiles();
    if (!hardwareProfiles_.empty()) {
        static const char* preferred[] = {"pixel_7", "medium_phone", "pixel_6", "pixel"};
        auto chosen = hardwareProfiles_.begin();
        for (const char* wanted : preferred) {
            const auto it = std::find_if(hardwareProfiles_.begin(), hardwareProfiles_.end(), [wanted](const auto& p) {
                return p.id == wanted;
            });
            if (it != hardwareProfiles_.end()) { chosen = it; break; }
        }
        config_.hardwareProfile = chosen->id;
    }
    config_.normalize();
    saveConfig();
    setStatus(tr("setup.compat_defaults_applied"));
}

void Application::renderSetup(ImmediateUi& ui) {
    ui.heading(tr("setup.title"));
    ui.label(std::string("JDK ") + (sdk_.jdkInstalled() ? "OK" : tr("setup.jdk_missing")) + " | Command-line Tools " +
             (sdk_.commandLineToolsInstalled() ? "OK" : tr("setup.jdk_missing")));
    ui.muted(tr("setup.onboarding_note"));

    ui.separator();
    ui.heading(tr("setup.command_line_tools"));
    if (ui.button("open-cli-download", tr("setup.download_cli"))) SDL_OpenURL("https://developer.android.com/studio");
    if (ui.textField("cli-archive", tr("setup.cli_zip"), config_.commandLineToolsArchivePath,
                     tr("setup.cli_placeholder"))) saveConfig();
    if (ui.button("browse-cli", tr("setup.browse_cli"))) {
        const auto selected = selectZipFile(tr("dialog.cli_zip"),
            config_.commandLineToolsArchivePath.empty() ? std::filesystem::path{} : pathFromUtf8(config_.commandLineToolsArchivePath));
        if (selected) { config_.commandLineToolsArchivePath = pathToUtf8(*selected); saveConfig(); }
    }
    if (ui.button("import-cli", tr("setup.import_cli"),
                  !config_.commandLineToolsArchivePath.empty() && !tasks_.busy())) {
        const auto archive = pathFromUtf8(config_.commandLineToolsArchivePath);
        enqueue(tr("task.cli_import"), [this, archive] {
            sdk_.importCommandLineToolsArchive(archive);
            refreshSdkRequested_ = true;
        });
    }

    ui.separator();
    ui.heading(tr("setup.jdk"));
    if (ui.textField("jdk-source", tr("setup.jdk_source"), config_.jdkSourcePath, tr("setup.jdk_placeholder"))) saveConfig();
    if (ui.button("browse-jdk", tr("setup.browse_jdk"))) {
        const auto selected = selectDirectory(tr("dialog.jdk_folder"),
            config_.jdkSourcePath.empty() ? std::filesystem::path{} : pathFromUtf8(config_.jdkSourcePath));
        if (selected) { config_.jdkSourcePath = pathToUtf8(*selected); saveConfig(); }
    }
    if (ui.button("import-jdk", tr("setup.import_jdk"), !config_.jdkSourcePath.empty() && !tasks_.busy())) {
        const auto source = pathFromUtf8(config_.jdkSourcePath);
        enqueue(tr("task.jdk_import"), [this, source] { sdk_.importJdk(source); });
    }

    ui.separator();
    if (ui.button("licenses", tr("setup.licenses"), sdk_.jdkInstalled() && sdk_.commandLineToolsInstalled())) {
        try { if (!sdk_.openLicenseTerminal()) setStatus(tr("setup.terminal_failed")); }
        catch (const std::exception& error) { setStatus(error.what()); }
    }
}

void Application::renderLanguage(ImmediateUi& ui) {
    ui.heading(tr("language.title"));
    ui.label(tr("language.current") + ": " + language_.languageName(config_.language));
    ui.muted(tr("language.description"));
    ui.separator();
    for (const auto& code : language_.availableLanguages()) {
        const bool selected = config_.language == code;
        if (ui.button("language-" + code, language_.languageName(code), !selected)) {
            config_.language = code;
            language_.setLanguage(code);
            config_.language = language_.language();
            saveConfig();
            setStatus(tr("status.ready"));
        }
    }
}

void Application::refreshDeviceList() {
    avdInfos_ = avd_.listAvdInfos();
    deviceListLoaded_ = true;
    if (!selectedAvdName_.empty()) {
        const auto it = std::find_if(avdInfos_.begin(), avdInfos_.end(), [this](const auto& item) { return item.name == selectedAvdName_; });
        if (it == avdInfos_.end()) selectedAvdName_.clear();
    }
    if (selectedAvdName_.empty() && !avdInfos_.empty()) selectDevice(avdInfos_.front().name);
}

void Application::refreshSdkPackageList() {
    sdkPackages_ = sdk_.installedPackages();
    sdkPackageListLoaded_ = true;
    if (!selectedSdkPackage_.empty()) {
        const auto it = std::find_if(sdkPackages_.begin(), sdkPackages_.end(), [this](const auto& item) {
            return item.packagePath == selectedSdkPackage_;
        });
        if (it == sdkPackages_.end()) selectedSdkPackage_.clear();
    }
}

void Application::selectDevice(const std::string& name) {
    if (!avd_.exists(name)) return;
    selectedAvdName_ = name;
    config_ = avd_.loadConfig(name, config_);
    config_.avdName = name;
    saveConfig();
}

void Application::prepareNewDevice() {
    AppConfig fresh = config_;
    int suffix = 1;
    std::string candidate;
    do { candidate = "PortableAVM_Device_" + std::to_string(suffix++); } while (avd_.exists(candidate));
    fresh.avdName = candidate;
    fresh.apiLevel = 30;
    fresh.imageTag = "google_apis_playstore";
    fresh.abi = recommendedAbi();
    fresh.gpuMode = "auto";
    fresh.coldBoot = true;
    fresh.saveSnapshot = false;
    fresh.normalize();
    config_ = fresh;
    selectedAvdName_.clear();
    saveConfig();
}

void Application::renderDevices(ImmediateUi& ui) {
    if (!deviceListLoaded_) refreshDeviceList();
    ui.heading(tr("devices.title"));
    ui.muted(tr("devices.description"));
    if (ui.button("refresh-devices", tr("common.refresh"), !tasks_.busy())) refreshDeviceList();
    if (ui.button("new-device", tr("devices.new"), !emulator_.running() && !tasks_.busy())) prepareNewDevice();

    ui.separator();
    ui.heading(tr("devices.list"));
    if (avdInfos_.empty()) ui.muted(tr("devices.empty"));
    for (const auto& item : avdInfos_) {
        std::string summary = item.name + "   Android " + std::to_string(item.apiLevel) + " / " + item.abi;
        if (!item.launchable) summary += "   [!]";
        if (ui.compactTextItem("device-" + item.name, summary, selectedAvdName_ == item.name)) selectDevice(item.name);
    }

    ui.separator();
    ui.heading(selectedAvdName_.empty() ? tr("devices.create_title") : trf("devices.selected_title", {selectedAvdName_}));
    if (!selectedAvdName_.empty()) {
        const auto it = std::find_if(avdInfos_.begin(), avdInfos_.end(), [this](const auto& item) { return item.name == selectedAvdName_; });
        if (it != avdInfos_.end()) {
            ui.label(trf("devices.summary", {std::to_string(it->apiLevel), it->abi,
                     std::to_string(it->ramMb), std::to_string(it->cpuCores),
                     std::to_string(it->screenWidth), std::to_string(it->screenHeight)}), 12.5f);
            if (!it->launchable) ui.warning(it->problem);
        }
        if (ui.button("run-selected-device", tr("devices.run"), launchReady() && !emulator_.running() && !tasks_.busy())) {
            const AppConfig copy = config_;
            enqueue(tr("task.emu_start"), [this, copy] { emulator_.start(copy); });
        }
        if (ui.button("delete-selected-device", tr("devices.delete"), !emulator_.running() && !tasks_.busy())) {
            const std::string name = selectedAvdName_;
            if (confirmDelete(tr("dialog.delete_device_title"), trf("dialog.delete_device_message", {name}))) {
                enqueue(tr("task.avd_delete"), [this, name] {
                    avd_.deleteAvd(name);
                    refreshDevicesRequested_ = true;
                });
            }
        }
    }

    ui.separator();
    ui.heading(tr("devices.settings"));
    bool changed = false;
    changed |= ui.textField("avd-name", tr("avd.name"), config_.avdName, "PortableAVM_Device_1");
    changed |= ui.sliderInt("api", tr("setup.api"), config_.apiLevel, 21, 40, 1);
    changed |= ui.cycle("tag", tr("setup.image_type"), config_.imageTag, {"google_apis_playstore", "google_apis", "default"});
    changed |= ui.cycle("abi", tr("setup.guest_abi"), config_.abi, {"x86_64", "arm64-v8a", "x86", "armeabi-v7a"});
    if (!hardwareProfilesLoaded_ && sdk_.commandLineToolsInstalled()) refreshHardwareProfiles();
    if (!hardwareProfiles_.empty()) {
        std::vector<std::string> ids;
        for (const auto& profile : hardwareProfiles_) ids.push_back(profile.id);
        if (std::find(ids.begin(), ids.end(), config_.hardwareProfile) == ids.end()) ids.insert(ids.begin(), config_.hardwareProfile);
        changed |= ui.cycle("hardware-profile", tr("avd.hardware_profile"), config_.hardwareProfile, ids);
        if (ui.button("refresh-hardware-profiles", tr("avd.refresh_profiles"), !tasks_.busy())) refreshHardwareProfiles();
        ui.muted(tr("avd.hardware_profile_note"));
    } else {
        ui.muted(tr("avd.hardware_profiles_unavailable"));
        if (sdk_.commandLineToolsInstalled() && ui.button("refresh-hardware-profiles", tr("avd.refresh_profiles"), !tasks_.busy())) refreshHardwareProfiles();
    }
    changed |= ui.sliderInt("ram", tr("avd.ram"), config_.ramMb, 1024, 16384, 512);
    changed |= ui.sliderInt("cores", tr("avd.cores"), config_.cpuCores, 1, 16, 1);
    changed |= ui.sliderInt("data", tr("avd.data"), config_.dataPartitionGb, 8, 256, 8);
    changed |= ui.sliderInt("width", tr("avd.width"), config_.screenWidth, 480, 2160, 20);
    changed |= ui.sliderInt("height", tr("avd.height"), config_.screenHeight, 800, 3840, 20);
    changed |= ui.sliderInt("density", tr("avd.density"), config_.densityDpi, 120, 640, 10);
    if (emulator_.running() && sdk_.platformToolsInstalled()) {
        if (ui.button("apply-live-density", tr("avd.apply_density"), !tasks_.busy())) {
            const int density = config_.densityDpi;
            enqueue(tr("task.dpi_apply"), [this, density] { adb_.shell("wm density " + std::to_string(density)); });
        }
        if (ui.button("reset-live-density", tr("avd.reset_density"), !tasks_.busy())) {
            enqueue(tr("task.dpi_reset"), [this] { adb_.shell("wm density reset"); });
        }
        ui.muted(tr("avd.density_note"));
    }
    changed |= ui.cycle("gpu", tr("avd.gpu"), config_.gpuMode, {"host", "auto", "software", "off"});
    changed |= ui.checkbox("accel", tr("avd.accel"), config_.hardwareAcceleration);
    changed |= ui.checkbox("cold", tr("avd.cold"), config_.coldBoot);
    changed |= ui.checkbox("save-snapshot", tr("avd.snapshot"), config_.saveSnapshot);
    changed |= ui.checkbox("no-audio", tr("avd.audio"), config_.noAudio);
    changed |= ui.checkbox("no-boot", tr("avd.boot_anim"), config_.noBootAnimation);
    if (changed) saveConfig();
    ui.label(trf("setup.package", {config_.systemImagePackage()}), 12.5f);
    if (ui.button("save-device", selectedAvdName_.empty() ? tr("devices.create") : tr("devices.save"),
                  sdk_.packageInstalled(config_.systemImagePackage()) && !emulator_.running() && !tasks_.busy())) {
        const AppConfig copy = config_;
        selectedAvdName_ = copy.avdName;
        enqueue(tr("task.avd_create"), [this, copy] {
            avd_.createOrReplace(copy);
            refreshDevicesRequested_ = true;
        });
    }
}

void Application::renderSdkManagement(ImmediateUi& ui) {
    if (!sdkPackageListLoaded_) refreshSdkPackageList();
    ui.heading(tr("sdk.title"));
    ui.muted(tr("sdk.description"));

    ui.heading(tr("sdk.install"));
    bool changed = false;
    changed |= ui.sliderInt("sdk-api", tr("setup.api"), config_.apiLevel, 21, 40, 1);
    changed |= ui.cycle("sdk-tag", tr("setup.image_type"), config_.imageTag, {"google_apis_playstore", "google_apis", "default"});
    changed |= ui.cycle("sdk-abi", tr("setup.guest_abi"), config_.abi, {"x86_64", "arm64-v8a", "x86", "armeabi-v7a"});
    if (changed) saveConfig();
    if (ui.button("compat-defaults", tr("setup.compat_defaults"), !tasks_.busy())) applyCompatibilityDefaults();
    ui.label(trf("setup.package", {config_.systemImagePackage()}), 12.5f);
    if (ui.button("install-runtime", tr("sdk.install_selected"), sdk_.jdkInstalled() && sdk_.commandLineToolsInstalled() && !tasks_.busy())) {
        const std::string image = config_.systemImagePackage();
        enqueue(tr("task.runtime_install"), [this, image] {
            sdk_.installPackages({"platform-tools", "emulator", image});
            refreshSdkRequested_ = true;
        });
    }

    ui.separator();
    ui.heading(tr("sdk.installed"));
    if (ui.button("refresh-sdk-list", tr("common.refresh"), !tasks_.busy())) refreshSdkPackageList();
    if (sdkPackages_.empty()) ui.muted(tr("sdk.empty"));
    for (const auto& item : sdkPackages_) {
        std::string label = item.displayName;
        if (!item.revision.empty()) label += "  " + item.revision;
        if (ui.compactTextItem("sdk-package-" + item.packagePath, label, selectedSdkPackage_ == item.packagePath))
            selectedSdkPackage_ = item.packagePath;
    }
    const auto selected = std::find_if(sdkPackages_.begin(), sdkPackages_.end(), [this](const auto& item) {
        return item.packagePath == selectedSdkPackage_;
    });
    if (selected != sdkPackages_.end()) {
        ui.label(selected->packagePath, 12.0f);
        bool blocked = emulator_.running();
        if (selected->kind == "system-image" && avd_.systemImageInUse(selected->packagePath)) {
            blocked = true;
            ui.warning(tr("sdk.in_use_by_avd"));
        }
        if (selected->kind == "emulator" && !avdInfos_.empty()) ui.muted(tr("sdk.emulator_delete_note"));
        if (ui.button("delete-sdk-package", tr("sdk.delete_selected"), !blocked && !tasks_.busy())) {
            const std::string package = selected->packagePath;
            if (confirmDelete(tr("dialog.delete_sdk_title"), trf("dialog.delete_sdk_message", {selected->displayName}))) {
                enqueue(tr("task.sdk_delete"), [this, package] {
                    sdk_.uninstallPackage(package);
                    refreshSdkRequested_ = true;
                });
            }
        }
    }
}

void Application::renderApps(ImmediateUi& ui) {
    ui.heading(tr("apps.title"));
    if (ui.textField("apk", tr("apps.path"), config_.apkPath, tr("apps.placeholder"))) saveConfig();
    if (ui.button("browse-apk", tr("apps.browse"))) {
        const auto selected = selectApkFile(config_.apkPath.empty() ? std::filesystem::path{} : pathFromUtf8(config_.apkPath));
        if (selected) {
            config_.apkPath = pathToUtf8(*selected);
            saveConfig();
        }
    }
    if (ui.button("inspect-apk", tr("apps.inspect"), !config_.apkPath.empty())) {
        const ApkInspection inspection = inspectApk(pathFromUtf8(config_.apkPath));
        {
            std::scoped_lock lock(stateMutex_);
            apkInspection_ = inspection;
        }
        setStatus(inspection.summary);
    }
    ApkInspection inspection;
    {
        std::scoped_lock lock(stateMutex_);
        inspection = apkInspection_;
    }
    if (!inspection.summary.empty()) {
        ui.label(inspection.summary);
        if (apkSupportsAbi(inspection, config_.abi)) {
            ui.success(tr("apps.compatible"));
        } else {
            ui.warning(tr("apps.incompatible"));
        }
    }
    if (ui.button("install-apk", tr("apps.install"),
                  emulator_.running() && sdk_.platformToolsInstalled() && !config_.apkPath.empty() && !tasks_.busy())) {
        const auto apk = pathFromUtf8(config_.apkPath);
        enqueue(tr("task.apk_install"), [this, apk] { adb_.installApk(apk); });
    }
    if (ui.button("adb-devices", tr("apps.devices"), sdk_.platformToolsInstalled() && !tasks_.busy())) {
        enqueue(tr("task.adb_devices"), [this] {
            const std::string devices = adb_.devices();
            std::scoped_lock lock(stateMutex_);
            adbDevices_ = devices;
        });
    }
    {
        std::scoped_lock lock(stateMutex_);
        if (!adbDevices_.empty()) ui.label(adbDevices_, 11.5f);
    }

}

void Application::renderLogs(ImmediateUi& ui) {
    ui.heading(tr("logs.title"));
    ui.muted(tr("logs.copy_hint"));
    ui.logView(logger_.snapshot(600), 520.0f);
}

bool Application::confirmDelete(const std::string& title, const std::string& message) {
    const std::string yes = tr("dialog.delete_yes");
    const std::string no = tr("dialog.delete_no");
    SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes.c_str()},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no.c_str()},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING;
    data.window = window_;
    data.title = title.c_str();
    data.message = message.c_str();
    data.numbuttons = 2;
    data.buttons = buttons;
    int buttonId = 0;
    return SDL_ShowMessageBox(&data, &buttonId) && buttonId == 1;
}

bool Application::confirmCloseIfEmulatorRunning() {
    if (!emulator_.running()) return true;

    const std::string title = tr("dialog.emulator_running_title");
    const std::string message = tr("dialog.emulator_running_message");
    // Button labels must outlive SDL_ShowMessageBox; keep translated strings in locals.
    const std::string yes = tr("dialog.stop_emulator_yes");
    const std::string no = tr("dialog.stop_emulator_no");
    SDL_MessageBoxButtonData stableButtons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes.c_str()},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no.c_str()},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING;
    data.window = window_;
    data.title = title.c_str();
    data.message = message.c_str();
    data.numbuttons = 2;
    data.buttons = stableButtons;
    int buttonId = 0;
    if (!SDL_ShowMessageBox(&data, &buttonId)) return false;
    if (buttonId != 1) return false;
    emulator_.stop();
    return true;
}




} // namespace pavm
