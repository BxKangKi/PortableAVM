#include "core/AdbManager.h"
#include "core/StringUtil.h"

#include <chrono>
#include <stdexcept>

namespace pavm {

AdbManager::AdbManager(SdkManager& sdk, Logger& logger) : sdk_(sdk), logger_(logger) {}

void AdbManager::installApk(const std::filesystem::path& apk) {
    if (!std::filesystem::is_regular_file(apk)) throw std::runtime_error("APK 파일을 찾지 못했습니다.");
    if (!sdk_.platformToolsInstalled()) throw std::runtime_error("platform-tools가 설치되지 않았습니다.");
    logger_.info("사용자가 선택한 APK 설치: " + pathToUtf8(apk.filename()));
    const auto result = ProcessRunner::run(sdk_.adbExecutable(),
        {"wait-for-device", "install", "-r", "-d", pathToUtf8(apk)},
        sdk_.environment(), {}, std::chrono::minutes(10),
        [this](const std::string& chunk) { logger_.appendRaw(trim(chunk)); });
    if (result.exitCode != 0) throw std::runtime_error("adb install 실패: " + result.output);
}

std::string AdbManager::devices() {
    if (!sdk_.platformToolsInstalled()) return "platform-tools 미설치";
    const auto result = ProcessRunner::run(sdk_.adbExecutable(), {"devices", "-l"},
                                            sdk_.environment(), {}, std::chrono::seconds(30));
    return result.output;
}

std::string AdbManager::shell(const std::string& command) {
    if (!sdk_.platformToolsInstalled()) throw std::runtime_error("platform-tools가 설치되지 않았습니다.");
    const auto result = ProcessRunner::run(sdk_.adbExecutable(), {"shell", command},
                                            sdk_.environment(), {}, std::chrono::minutes(1));
    if (result.exitCode != 0) throw std::runtime_error("adb shell 실패: " + result.output);
    return result.output;
}

void AdbManager::stopServer() {
    if (!sdk_.platformToolsInstalled()) return;
    const auto result = ProcessRunner::run(sdk_.adbExecutable(), {"kill-server"},
                                            sdk_.environment(), {}, std::chrono::seconds(15));
    if (result.exitCode == 0) logger_.info("포터블 ADB 서버를 종료했습니다.");
}

} // namespace pavm
