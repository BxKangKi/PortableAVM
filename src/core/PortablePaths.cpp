#include "core/PortablePaths.h"
#include "core/StringUtil.h"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace pavm {
namespace {

std::filesystem::path executablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    std::array<char, PATH_MAX + 1> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), PATH_MAX);
    if (length > 0) {
        buffer[static_cast<std::size_t>(length)] = '\0';
        return std::filesystem::path(buffer.data());
    }
    return std::filesystem::current_path() / "PortableAVM";
#endif
}

void make(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Cannot create directory " + pathToUtf8(path) + ": " + ec.message());
    }
}

} // namespace

PortablePaths PortablePaths::discover() {
    PortablePaths paths;
    paths.root = std::filesystem::weakly_canonical(executablePath().parent_path());
    paths.data = paths.root / "Data";
    paths.bin = paths.data / "bin";
    paths.configs = paths.data / "Configs";
    paths.lang = paths.data / "lang";
    paths.sdk = paths.data / "Runtime" / "Android" / "sdk";
    paths.jdk = paths.data / "Runtime" / "jdk";
    paths.avd = paths.data / "AVD";
    paths.homeAndroid = paths.data / "home" / "android";
    paths.homeEmulator = paths.data / "home" / "emulator";
    paths.homeAdb = paths.data / "home" / "adb";
    paths.homeUser = paths.data / "home" / "user";
    paths.gradleHome = paths.data / "home" / "gradle";
    paths.appDataRoaming = paths.data / "windows" / "roaming";
    paths.appDataLocal = paths.data / "windows" / "local";
    paths.downloads = paths.data / "downloads";
    paths.cache = paths.data / "cache";
    paths.temp = paths.data / "temp";
    paths.logs = paths.data / "Logs";
    paths.locks = paths.data / "Locks";
    return paths;
}

void PortablePaths::ensureLayout() const {
    // Keep startup minimal. Only the Data root is guaranteed at application start.
    // Every subsystem creates the directories it actually uses immediately before use.
    make(data);
}

void PortablePaths::ensureChildEnvironmentLayout() const {
    // Android command-line tools and the Emulator expect these environment-backed
    // directories to exist. Create them just before starting an Android child process.
    for (const auto& path : {data, sdk, avd, homeAndroid, homeEmulator, homeAdb, homeUser,
                             gradleHome, appDataRoaming, appDataLocal, temp, logs}) {
        make(path);
    }
}

std::map<std::string, std::string> PortablePaths::childEnvironment(int adbPort) const {
    ensureChildEnvironmentLayout();
    std::map<std::string, std::string> env;
    env["ANDROID_HOME"] = pathToUtf8(sdk);
    env["ANDROID_SDK_ROOT"] = pathToUtf8(sdk);
    env["ANDROID_USER_HOME"] = pathToUtf8(homeAndroid);
    env["ANDROID_EMULATOR_HOME"] = pathToUtf8(homeEmulator);
    env["ANDROID_AVD_HOME"] = pathToUtf8(avd);
    env["ANDROID_PREFS_ROOT"] = pathToUtf8(homeAndroid);
    env["ANDROID_ADB_SERVER_PORT"] = std::to_string(adbPort);
    env["ADB_VENDOR_KEYS"] = pathToUtf8(homeAdb);
    env["GRADLE_USER_HOME"] = pathToUtf8(gradleHome);
    env["HOME"] = pathToUtf8(homeUser);
    env["USERPROFILE"] = pathToUtf8(homeUser);
    env["APPDATA"] = pathToUtf8(appDataRoaming);
    env["LOCALAPPDATA"] = pathToUtf8(appDataLocal);
    env["TEMP"] = pathToUtf8(temp);
    env["TMP"] = pathToUtf8(temp);
    env["JAVA_HOME"] = pathToUtf8(jdk);
    return env;
}

std::filesystem::path PortablePaths::settingsFile() const {
    return configs / "settings.ini";
}

std::filesystem::path PortablePaths::logFile() const {
    return logs / "PortableAVM.log";
}

} // namespace pavm
