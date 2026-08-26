#include "core/SdkManager.h"
#include "core/ArchiveExtractor.h"
#include "core/RepositoryParser.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pavm {
namespace {

#ifdef _WIN32
constexpr const char* kSdkManagerName = "sdkmanager.bat";
constexpr const char* kAvdManagerName = "avdmanager.bat";
constexpr const char* kEmulatorName = "emulator.exe";
constexpr const char* kAdbName = "adb.exe";
constexpr const char* kJavaName = "java.exe";
#else
constexpr const char* kSdkManagerName = "sdkmanager";
constexpr const char* kAvdManagerName = "avdmanager";
constexpr const char* kEmulatorName = "emulator";
constexpr const char* kAdbName = "adb";
constexpr const char* kJavaName = "java";
#endif


void ensureDirectoryExists(const std::filesystem::path& path, const char* label) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec || !std::filesystem::is_directory(path)) {
        throw std::runtime_error(std::string(label) + " 경로를 생성할 수 없습니다: " +
                                 pathToUtf8(path) + (ec ? " (" + ec.message() + ")" : ""));
    }
}

void copyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!std::filesystem::is_directory(source)) {
        throw std::runtime_error("복사할 원본 폴더를 찾을 수 없습니다: " + pathToUtf8(source));
    }

    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    ec.clear();
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        throw std::runtime_error("대상 폴더를 만들 수 없습니다: " + pathToUtf8(destination) +
                                 " (" + ec.message() + ")");
    }

    for (std::filesystem::recursive_directory_iterator it(source, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            std::filesystem::remove_all(destination);
            throw std::runtime_error("원본 폴더를 읽을 수 없습니다: " + pathToUtf8(source) +
                                     " (" + ec.message() + ")");
        }
        const auto relative = std::filesystem::relative(it->path(), source, ec);
        if (ec) {
            std::filesystem::remove_all(destination);
            throw std::runtime_error("복사 경로를 계산할 수 없습니다: " + pathToUtf8(it->path()) +
                                     " (" + ec.message() + ")");
        }
        const auto target = destination / relative;
        if (it->is_symlink(ec)) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (!ec) std::filesystem::copy_symlink(it->path(), target, ec);
        } else if (it->is_directory(ec)) {
            std::filesystem::create_directories(target, ec);
        } else if (it->is_regular_file(ec)) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (!ec) {
                std::filesystem::copy_file(it->path(), target,
                                           std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        if (ec) {
            const std::string message = ec.message();
            std::filesystem::remove_all(destination);
            throw std::runtime_error("파일 복사 실패: " + pathToUtf8(it->path()) + " -> " +
                                     pathToUtf8(target) + " (" + message + ")");
        }
    }
}

void moveOrCopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination) {
    ensureDirectoryExists(destination.parent_path(), "대상 상위 폴더");
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    ec.clear();
    std::filesystem::rename(source, destination, ec);
    if (!ec) return;

    // Cross-volume moves and a few Windows filesystem configurations cannot rename a
    // directory atomically. Fall back to an entry-by-entry copy with precise errors.
    copyDirectory(source, destination);
    std::filesystem::remove_all(source, ec);
}


std::string sourceProperty(const std::filesystem::path& directory, const std::string& wanted) {
    const auto file = directory / "source.properties";
    if (!std::filesystem::is_regular_file(file)) return {};
    std::istringstream input(readTextFile(file));
    std::string line;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        if (trim(std::string_view(line).substr(0, equal)) == wanted)
            return trim(std::string_view(line).substr(equal + 1));
    }
    return {};
}

void removeEmptyParents(std::filesystem::path path, const std::filesystem::path& stop) {
    std::error_code ec;
    while (!path.empty() && path != stop && path.string().size() >= stop.string().size()) {
        if (!std::filesystem::is_directory(path, ec) || !std::filesystem::is_empty(path, ec)) break;
        std::filesystem::remove(path, ec);
        if (ec) break;
        path = path.parent_path();
    }
}

std::filesystem::path findJavaRoot(const std::filesystem::path& selected) {
    if (std::filesystem::is_regular_file(selected)) {
        const auto parent = selected.parent_path();
        if (toLowerAscii(pathToUtf8(parent.filename())) == "bin") return parent.parent_path();
    }
    if (std::filesystem::is_regular_file(selected / "bin" / kJavaName)) return selected;
    if (std::filesystem::is_directory(selected)) {
        for (const auto& entry : std::filesystem::directory_iterator(selected)) {
            if (entry.is_directory() && std::filesystem::is_regular_file(entry.path() / "bin" / kJavaName)) {
                return entry.path();
            }
        }
    }
    return {};
}

#ifdef _WIN32
bool looksLikeForeignAndroidToolPath(const std::string& raw) {
    std::string value = toLowerAscii(replaceAll(raw, "\\", "/"));
    while (!value.empty() && (value.back() == '/' || value.back() == ' ')) value.pop_back();
    return value.find("/android/sdk/") != std::string::npos ||
           value.ends_with("/android/sdk") ||
           value.find("/cmdline-tools/") != std::string::npos ||
           value.ends_with("/platform-tools") ||
           value.ends_with("/emulator");
}

std::string sanitizedInheritedPath(const std::string& inherited) {
    std::vector<std::string> kept;
    std::set<std::string> seen;
    for (const auto& raw : split(inherited, ';', false)) {
        const std::string item = trim(raw);
        if (item.empty() || looksLikeForeignAndroidToolPath(item)) continue;
        const std::string key = toLowerAscii(replaceAll(item, "\\", "/"));
        if (seen.insert(key).second) kept.push_back(item);
    }
    return join(kept, ";");
}
#else
bool looksLikeForeignAndroidToolPath(const std::string& raw) {
    const std::string value = toLowerAscii(raw);
    return value.find("/android/sdk/") != std::string::npos ||
           value.ends_with("/android/sdk") ||
           value.find("/cmdline-tools/") != std::string::npos ||
           value.ends_with("/platform-tools") ||
           value.ends_with("/emulator");
}

std::string sanitizedInheritedPath(const std::string& inherited) {
    std::vector<std::string> kept;
    std::set<std::string> seen;
    for (const auto& raw : split(inherited, ':', false)) {
        const std::string item = trim(raw);
        if (item.empty() || looksLikeForeignAndroidToolPath(item)) continue;
        if (seen.insert(item).second) kept.push_back(item);
    }
    return join(kept, ":");
}
#endif

} // namespace

SdkManager::SdkManager(const PortablePaths& paths, Logger& logger, int adbPort)
    : paths_(paths), logger_(logger), adbPort_(adbPort) {}

bool SdkManager::commandLineToolsInstalled() const {
    return std::filesystem::is_regular_file(sdkManagerExecutable());
}

bool SdkManager::emulatorInstalled() const {
    return std::filesystem::is_regular_file(emulatorExecutable());
}

bool SdkManager::platformToolsInstalled() const {
    return std::filesystem::is_regular_file(adbExecutable());
}

bool SdkManager::jdkInstalled() const {
    return std::filesystem::is_regular_file(paths_.jdk / "bin" / kJavaName);
}

bool SdkManager::packageInstalled(const std::string& package) const {
    const auto parts = split(package, ';', true);
    if (parts.empty()) return false;
    if (parts[0] == "emulator") return emulatorInstalled();
    if (parts[0] == "platform-tools") return platformToolsInstalled();
    if (parts[0] == "system-images" && parts.size() == 4) {
        return std::filesystem::is_directory(paths_.sdk / "system-images" / parts[1] / parts[2] / parts[3]);
    }
    return false;
}

std::vector<InstalledSdkPackage> SdkManager::installedPackages() const {
    std::vector<InstalledSdkPackage> result;
    auto add = [&](std::string path, std::string name, std::string kind, const std::filesystem::path& directory) {
        if (!std::filesystem::is_directory(directory)) return;
        InstalledSdkPackage item;
        item.packagePath = std::move(path);
        item.displayName = std::move(name);
        item.kind = std::move(kind);
        item.directory = directory;
        item.revision = sourceProperty(directory, "Pkg.Revision");
        result.push_back(std::move(item));
    };

    if (emulatorInstalled()) add("emulator", "Android Emulator", "emulator", paths_.sdk / "emulator");
    if (platformToolsInstalled()) add("platform-tools", "Android Platform Tools", "platform-tools", paths_.sdk / "platform-tools");

    const auto imagesRoot = paths_.sdk / "system-images";
    std::error_code ec;
    if (std::filesystem::is_directory(imagesRoot, ec)) {
        for (const auto& api : std::filesystem::directory_iterator(imagesRoot, ec)) {
            if (ec) break;
            if (!api.is_directory()) continue;
            for (const auto& tag : std::filesystem::directory_iterator(api.path(), ec)) {
                if (ec) break;
                if (!tag.is_directory()) continue;
                for (const auto& abi : std::filesystem::directory_iterator(tag.path(), ec)) {
                    if (ec) break;
                    if (!abi.is_directory()) continue;
                    if (!std::filesystem::exists(abi.path() / "system.img") &&
                        !std::filesystem::exists(abi.path() / "package.xml")) continue;
                    const std::string package = "system-images;" + pathToUtf8(api.path().filename()) + ";" +
                                                pathToUtf8(tag.path().filename()) + ";" + pathToUtf8(abi.path().filename());
                    add(package, pathToUtf8(api.path().filename()) + " / " + pathToUtf8(tag.path().filename()) +
                                 " / " + pathToUtf8(abi.path().filename()), "system-image", abi.path());
                }
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.packagePath < b.packagePath;
    });
    return result;
}

void SdkManager::uninstallPackage(const std::string& package) {
    const auto parts = split(package, ';', true);
    std::filesystem::path target;
    if (package == "emulator") target = paths_.sdk / "emulator";
    else if (package == "platform-tools") target = paths_.sdk / "platform-tools";
    else if (parts.size() == 4 && parts[0] == "system-images")
        target = paths_.sdk / "system-images" / parts[1] / parts[2] / parts[3];
    else throw std::runtime_error("관리할 수 없는 SDK 패키지입니다: " + package);

    if (!std::filesystem::exists(target)) return;
    bool removedByManager = false;
    if (jdkInstalled() && commandLineToolsInstalled()) {
        try {
            const auto result = ProcessRunner::run(sdkManagerExecutable(), {"--uninstall", package}, environment(), {}, std::chrono::minutes(5));
            removedByManager = result.exitCode == 0 && !std::filesystem::exists(target);
            if (!removedByManager && result.exitCode != 0) logger_.warn("sdkmanager 제거 실패, 직접 정리를 시도합니다: " + trim(result.output));
        } catch (const std::exception& error) {
            logger_.warn(std::string("sdkmanager 제거 예외, 직접 정리를 시도합니다: ") + error.what());
        }
    }
    if (!removedByManager) {
        std::error_code ec;
        std::filesystem::remove_all(target, ec);
        if (ec || std::filesystem::exists(target)) {
            throw std::runtime_error("SDK 패키지 폴더를 삭제할 수 없습니다: " + pathToUtf8(target) +
                                     (ec ? " (" + ec.message() + ")" : ""));
        }
    }
    if (parts.size() == 4 && parts[0] == "system-images") {
        removeEmptyParents(target.parent_path(), paths_.sdk / "system-images");
    }
    logger_.info("SDK 패키지 삭제: " + package);
}

void SdkManager::importCommandLineToolsArchive(const std::filesystem::path& archive) {
    // Import must also work on a completely fresh portable data directory.
    // Create every parent used by staging and final placement before touching the ZIP.
    ensureDirectoryExists(paths_.data, "data");
    ensureDirectoryExists(paths_.temp, "임시 폴더");
    ensureDirectoryExists(paths_.sdk, "Android SDK 폴더");
    ensureDirectoryExists(paths_.sdk / "cmdline-tools", "Command-line Tools 폴더");

    if (!std::filesystem::is_regular_file(archive)) {
        throw std::runtime_error("Command-line Tools ZIP 파일을 찾을 수 없습니다: " + pathToUtf8(archive));
    }
    if (toLowerAscii(pathToUtf8(archive.extension())) != ".zip") {
        throw std::runtime_error("Command-line Tools는 ZIP 압축 파일만 가져올 수 있습니다.");
    }

    const std::filesystem::path staging = paths_.temp / ("cmdline-tools-import-" + randomToken());
    ensureDirectoryExists(staging.parent_path(), "Command-line Tools 임시 폴더");
    ArchiveExtractor::extractZipSafely(archive, staging);

    std::filesystem::path source = staging / "cmdline-tools";
    if (!std::filesystem::is_regular_file(source / "bin" / kSdkManagerName)) {
        source.clear();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(staging)) {
            if (!entry.is_regular_file()) continue;
            if (toLowerAscii(pathToUtf8(entry.path().filename())) == toLowerAscii(kSdkManagerName) &&
                toLowerAscii(pathToUtf8(entry.path().parent_path().filename())) == "bin") {
                const auto candidate = entry.path().parent_path().parent_path();
                if (std::filesystem::is_regular_file(candidate / "bin" / kAvdManagerName)) {
                    source = candidate;
                    break;
                }
            }
        }
    }
    if (source.empty() || !std::filesystem::is_regular_file(source / "bin" / kSdkManagerName) ||
        !std::filesystem::is_regular_file(source / "bin" / kAvdManagerName)) {
        std::filesystem::remove_all(staging);
        throw std::runtime_error("선택한 ZIP에서 Android Command-line Tools 구조를 찾지 못했습니다.");
    }

    const std::filesystem::path toolsRoot = paths_.sdk / "cmdline-tools";
    const std::filesystem::path target = toolsRoot / "latest";
    const std::filesystem::path prepared = toolsRoot / (".latest-new-" + randomToken());
    ensureDirectoryExists(toolsRoot, "Command-line Tools 설치 폴더");
    ensureDirectoryExists(prepared.parent_path(), "Command-line Tools 준비 폴더");
    moveOrCopyDirectory(source, prepared);
    if (!std::filesystem::is_regular_file(prepared / "bin" / kSdkManagerName) ||
        !std::filesystem::is_regular_file(prepared / "bin" / kAvdManagerName)) {
        std::filesystem::remove_all(prepared);
        std::filesystem::remove_all(staging);
        throw std::runtime_error("가져온 Command-line Tools 설치 검증에 실패했습니다.");
    }

    std::error_code ec;
    const auto backup = toolsRoot / (".latest-old-" + randomToken());
    if (std::filesystem::exists(target)) {
        std::filesystem::rename(target, backup, ec);
        if (ec) {
            std::filesystem::remove_all(prepared);
            std::filesystem::remove_all(staging);
            throw std::runtime_error("기존 Command-line Tools가 사용 중이라 교체할 수 없습니다: " + ec.message());
        }
    }

    ec.clear();
    std::filesystem::rename(prepared, target, ec);
    if (ec) {
        std::error_code restoreEc;
        if (std::filesystem::exists(backup)) std::filesystem::rename(backup, target, restoreEc);
        std::filesystem::remove_all(prepared);
        std::filesystem::remove_all(staging);
        throw std::runtime_error("Command-line Tools 최종 배치 실패: " + ec.message());
    }
    std::filesystem::remove_all(backup, ec);
    std::filesystem::remove_all(staging, ec);
    logger_.info("Command-line Tools ZIP을 가져왔습니다: " + pathToUtf8(archive));
    logger_.info("설치 경로: " + pathToUtf8(target));
}

void SdkManager::installPackages(const std::vector<std::string>& packages) {
    requireJdk();
    requireCommandLineTools();
    if (packages.empty()) return;
    std::vector<std::string> args{"--sdk_root=" + pathToUtf8(paths_.sdk)};
    args.insert(args.end(), packages.begin(), packages.end());
    logger_.info("sdkmanager 패키지 설치: " + join(packages, ", "));
    const auto result = ProcessRunner::run(sdkManagerExecutable(), args, environment(), {},
                                           std::chrono::hours(2),
                                           [this](const std::string& chunk) { logProcessChunk(chunk); });
    if (result.timedOut) throw std::runtime_error("sdkmanager 실행 시간 초과");
    if (result.exitCode != 0) throw std::runtime_error("sdkmanager 실패, 종료 코드 " + std::to_string(result.exitCode));
    logger_.info("SDK 패키지 설치 완료");
}

std::vector<std::string> SdkManager::availableSystemImages() {
    requireJdk();
    requireCommandLineTools();
    const auto result = ProcessRunner::run(sdkManagerExecutable(),
        {"--sdk_root=" + pathToUtf8(paths_.sdk), "--list"}, environment(), {}, std::chrono::minutes(10));
    if (result.exitCode != 0) throw std::runtime_error("sdkmanager --list 실패");
    return findSystemImagePackages(result.output);
}


std::vector<HardwareProfileInfo> SdkManager::availableHardwareProfiles() {
    requireCommandLineTools();
    const auto result = ProcessRunner::run(avdManagerExecutable(), {"list", "device"}, environment(), {},
                                           std::chrono::minutes(2));
    if (result.exitCode != 0) {
        throw std::runtime_error("avdmanager device profile list failed (exit " +
                                 std::to_string(result.exitCode) + "): " + trim(result.output));
    }
    auto profiles = parseHardwareProfiles(result.output);
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(), [](const HardwareProfileInfo& profile) {
        return !isPhoneHardwareProfile(profile);
    }), profiles.end());
    return profiles;
}

bool SdkManager::openLicenseTerminal() {
    requireJdk();
    requireCommandLineTools();
    logger_.info("사용자가 직접 검토하고 응답할 수 있도록 SDK 라이선스 터미널을 엽니다.");
    return ProcessRunner::launchInteractiveTerminal(sdkManagerExecutable(),
        {"--sdk_root=" + pathToUtf8(paths_.sdk), "--licenses"}, environment(), "PortableAVM SDK Licenses");
}

void SdkManager::importJdk(const std::filesystem::path& sourceValue) {
    const auto source = findJavaRoot(sourceValue);
    if (source.empty()) {
        throw std::runtime_error("선택한 폴더에서 bin/" + std::string(kJavaName) + "를 찾지 못했습니다.");
    }

    // Data/Runtime/jdk is intentionally created lazily. std::filesystem::equivalent()
    // throws on Windows when the destination does not exist yet, so compare only
    // when both sides exist and use the non-throwing overload.
    std::error_code ec;
    if (std::filesystem::exists(paths_.jdk, ec) && !ec) {
        ec.clear();
        if (std::filesystem::equivalent(source, paths_.jdk, ec) && !ec) return;
    }

    ensureDirectoryExists(paths_.temp, "JDK 임시 폴더");
    ensureDirectoryExists(paths_.jdk.parent_path(), "JDK 런타임 폴더");

    const auto staging = paths_.temp / ("jdk-" + randomToken());
    logger_.info("JDK를 포터블 런타임으로 복사합니다.");
    try {
        copyDirectory(source, staging);
        if (!std::filesystem::is_regular_file(staging / "bin" / kJavaName)) {
            throw std::runtime_error("복사된 JDK 검증 실패");
        }
        moveOrCopyDirectory(staging, paths_.jdk);
    } catch (...) {
        std::error_code cleanupEc;
        std::filesystem::remove_all(staging, cleanupEc);
        throw;
    }
    logger_.info("포터블 JDK 가져오기 완료");
}

std::filesystem::path SdkManager::sdkManagerExecutable() const {
    return paths_.sdk / "cmdline-tools" / "latest" / "bin" / kSdkManagerName;
}
std::filesystem::path SdkManager::avdManagerExecutable() const {
    return paths_.sdk / "cmdline-tools" / "latest" / "bin" / kAvdManagerName;
}
std::filesystem::path SdkManager::emulatorExecutable() const {
    return paths_.sdk / "emulator" / kEmulatorName;
}
std::filesystem::path SdkManager::adbExecutable() const {
    return paths_.sdk / "platform-tools" / kAdbName;
}

ProcessEnvironment SdkManager::environment() const {
    auto env = paths_.childEnvironment(adbPort_);
    std::string path = pathToUtf8(paths_.sdk / "platform-tools") + ";" +
                       pathToUtf8(paths_.sdk / "emulator") + ";" +
                       pathToUtf8(paths_.jdk / "bin");
#ifdef _WIN32
    char* inherited = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&inherited, &size, "PATH") == 0 && inherited != nullptr) {
        const std::string sanitized = sanitizedInheritedPath(inherited);
        if (!sanitized.empty()) path += ";" + sanitized;
        std::free(inherited);
    }
#else
    if (const char* inherited = std::getenv("PATH")) {
        const std::string sanitized = sanitizedInheritedPath(inherited);
        if (!sanitized.empty()) path += ";" + sanitized;
    }
    path = replaceAll(path, ";", ":");
#endif
    env["PATH"] = path;
    // ANDROID_SDK_HOME is a legacy user-home variable, not the SDK install root.
    // Remove any inherited value; mixing it with ANDROID_HOME/ANDROID_SDK_ROOT can
    // make recent avdmanager releases fail while writing the AVD .ini file.
    env["ANDROID_SDK_HOME"] = "";
    env["ANDROID_NDK_HOME"] = "";
    env["ANDROID_NDK_ROOT"] = "";
    return env;
}

void SdkManager::requireJdk() const {
    if (!jdkInstalled()) throw std::runtime_error("먼저 지원되는 JDK를 Data/Runtime/jdk로 가져오세요.");
}

void SdkManager::requireCommandLineTools() const {
    if (!commandLineToolsInstalled()) throw std::runtime_error("먼저 Android Command-line Tools를 설치하세요.");
}

void SdkManager::logProcessChunk(const std::string& chunk) {
    for (const std::string& line : split(replaceAll(chunk, "\r", ""), '\n', false)) {
        if (!trim(line).empty()) logger_.appendRaw(trim(line));
    }
}

} // namespace pavm
