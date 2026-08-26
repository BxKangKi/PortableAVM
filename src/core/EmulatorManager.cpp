#include "core/EmulatorManager.h"
#include "core/HostInfo.h"
#include "core/StringUtil.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace pavm {

namespace {
std::string tailFile(const std::filesystem::path& path, std::size_t maximumBytes = 12000) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) return {};
    const auto size = static_cast<std::size_t>(end);
    const auto start = size > maximumBytes ? static_cast<std::streamoff>(size - maximumBytes) : 0;
    input.seekg(start, std::ios::beg);
    std::ostringstream out;
    out << input.rdbuf();
    return trim(out.str());
}
}

EmulatorManager::EmulatorManager(const PortablePaths& paths, SdkManager& sdk, Logger& logger)
    : paths_(paths), sdk_(sdk), logger_(logger) {}
EmulatorManager::~EmulatorManager() { stop(); }

std::uint64_t EmulatorManager::start(const AppConfig& source) {
    AppConfig config = source;
    config.normalize();
    std::scoped_lock lock(mutex_);
    if (process_.running()) throw std::runtime_error("에뮬레이터가 이미 실행 중입니다.");
    if (!sdk_.emulatorInstalled()) throw std::runtime_error("Android Emulator 패키지가 설치되지 않았습니다.");

    const auto imageDirectory = paths_.sdk / "system-images" /
        ("android-" + std::to_string(config.apiLevel)) / config.imageTag / config.abi;
    const auto avdDirectory = paths_.avd / (config.avdName + ".avd");
    if (!std::filesystem::is_regular_file(avdDirectory / "config.ini")) {
        throw std::runtime_error("AVD config.ini를 찾을 수 없습니다. AVD를 다시 생성하세요.");
    }
    if (!std::filesystem::is_regular_file(imageDirectory / "system.img")) {
        throw std::runtime_error("시스템 이미지가 불완전합니다: " + pathToUtf8(imageDirectory));
    }

    std::vector<std::string> args = {
        "-avd", config.avdName,
        "-sysdir", pathToUtf8(std::filesystem::absolute(imageDirectory)),
        "-datadir", pathToUtf8(std::filesystem::absolute(avdDirectory)),
        "-memory", std::to_string(config.ramMb),
        "-cores", std::to_string(config.cpuCores),
        "-gpu", config.gpuMode == "software" ? "swiftshader_indirect" : config.gpuMode,
        "-no-metrics",
        "-verbose"
    };
    const bool matchingArchitecture = abiMatchesHost(config.abi);
    if (!config.hardwareAcceleration || !matchingArchitecture) {
        args.insert(args.end(), {"-accel", "off"});
    } else {
        args.insert(args.end(), {"-accel", "auto"});
    }
    if (config.coldBoot) args.push_back("-no-snapshot-load");
    if (!config.saveSnapshot) args.push_back("-no-snapshot-save");
    if (config.wipeDataNextBoot) args.push_back("-wipe-data");
    if (config.noAudio) args.push_back("-no-audio");
    if (config.noBootAnimation) args.push_back("-no-boot-anim");

    logger_.info("에뮬레이터 시작: " + config.avdName);
    logger_.info("시스템 이미지: " + pathToUtf8(imageDirectory));
    logger_.info(accelerationDescription(config));
    const auto environment = sdk_.environment();
    const auto logPath = paths_.logs / "emulator-process.log";
    std::error_code ec;
    std::filesystem::remove(logPath, ec);
#ifdef _WIN32
    windowShown_ = false;
#endif
    process_ = ProcessRunner::spawn(sdk_.emulatorExecutable(), args, environment, logPath);
    if (!process_.valid()) throw std::runtime_error("에뮬레이터 프로세스를 시작하지 못했습니다.");

    // A process can be created successfully and still die immediately because of
    // an invalid AVD, missing hypervisor support, or a renderer problem. Do not
    // report a false successful launch in that case.
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    if (!process_.running()) {
        const std::string tail = tailFile(logPath);
        process_.reset();
        if (!tail.empty()) {
            logger_.error("에뮬레이터 초기화 실패:\n" + tail);
            throw std::runtime_error("에뮬레이터가 시작 직후 종료되었습니다. 로그: " + tail);
        }
        throw std::runtime_error("에뮬레이터가 시작 직후 종료되었습니다. " + pathToUtf8(logPath) + "를 확인하세요.");
    }

#ifdef _WIN32
    // The Android Emulator may create its Qt/QEMU top-level window after the
    // launcher process has already handed work to another process. Follow the
    // entire Job Object and explicitly reveal/restore the first top-level
    // window we find. This keeps launcher mode independent from console-window
    // behavior and fixes hidden emulator windows.
    bool windowShown = false;
    for (int i = 0; i < 40 && process_.running(); ++i) {
        if (process_.showMainWindow()) {
            windowShown = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!windowShown && process_.running()) {
        logger_.warn("에뮬레이터 프로세스는 실행 중이지만 top-level 창을 아직 찾지 못했습니다. 백그라운드에서 계속 탐색합니다.");
    }
    windowShown_ = windowShown;
#endif
    return process_.pid();
}

void EmulatorManager::stop() {
    std::scoped_lock lock(mutex_);
    if (!process_.valid()) return;
    if (process_.running()) {
        logger_.info("에뮬레이터 프로세스를 종료합니다.");
        process_.terminate();
    }
    process_.reset();
#ifdef _WIN32
    windowShown_ = false;
#endif
}

bool EmulatorManager::running() const {
    std::scoped_lock lock(mutex_);
    const bool alive = process_.running();
#ifdef _WIN32
    if (alive && !windowShown_) {
        windowShown_ = process_.showMainWindow();
    }
#endif
    return alive;
}

std::uint64_t EmulatorManager::pid() const {
    std::scoped_lock lock(mutex_);
    return process_.pid();
}

std::string EmulatorManager::accelerationDescription(const AppConfig& config) const {
    if (!abiMatchesHost(config.abi)) {
        return "호스트 " + hostArchitectureName() + "와 게스트 ABI " + config.abi +
               "가 달라 공식 에뮬레이터의 소프트웨어 CPU 에뮬레이션을 시도합니다. 지원 여부와 성능은 시스템 이미지에 따라 달라집니다.";
    }
    if (!config.hardwareAcceleration) return "하드웨어 가속을 사용하지 않습니다.";
    return "호스트와 게스트 ABI가 일치하여 공식 하이퍼바이저 가속을 요청합니다.";
}

} // namespace pavm
