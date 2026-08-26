#include "core/AvdManager.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace pavm {
namespace {

std::map<std::string, std::string> readConfigMap(const std::filesystem::path& path) {
    std::map<std::string, std::string> values;
    if (!std::filesystem::exists(path)) return values;
    std::istringstream input(readTextFile(path));
    std::string line;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = trim(std::string_view(line).substr(0, equal));
        if (!key.empty()) values[key] = trim(std::string_view(line).substr(equal + 1));
    }
    return values;
}

void writeConfigMap(const std::filesystem::path& path, const std::map<std::string, std::string>& values) {
    std::ostringstream out;
    for (const auto& [key, value] : values) {
        if (!isForbiddenIdentityProperty(key)) out << key << '=' << value << '\n';
    }
    writeTextFileAtomic(path, out.str());
}

std::string gpuConfigValue(const std::string& mode) {
    if (mode == "software") return "swiftshader_indirect";
    return mode;
}

int mapInt(const std::map<std::string, std::string>& values, const std::string& key, int fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    return parseInt(it->second).value_or(fallback);
}

int parseSizeGb(const std::string& value, int fallback) {
    std::string digits;
    for (unsigned char c : value) {
        if (std::isdigit(c)) digits.push_back(static_cast<char>(c));
        else break;
    }
    return parseInt(digits).value_or(fallback);
}

} // namespace

AvdManager::AvdManager(const PortablePaths& paths, SdkManager& sdk, Logger& logger)
    : paths_(paths), sdk_(sdk), logger_(logger) {}

void AvdManager::createOrReplace(const AppConfig& source) {
    AppConfig config = source;
    config.normalize();
    if (!isSafeAvdName(config.avdName)) throw std::runtime_error("안전하지 않은 AVD 이름입니다.");
    if (!sdk_.packageInstalled(config.systemImagePackage())) {
        throw std::runtime_error("선택한 시스템 이미지가 설치되지 않았습니다: " + config.systemImagePackage());
    }

    std::error_code ec;
    std::filesystem::create_directories(paths_.avd, ec);
    if (ec || !std::filesystem::is_directory(paths_.avd)) {
        throw std::runtime_error("AVD 저장 폴더를 만들 수 없습니다: " + pathToUtf8(paths_.avd) +
                                 (ec ? " (" + ec.message() + ")" : ""));
    }
    if (exists(config.avdName)) deleteAvd(config.avdName);

    const auto targetDirectory = avdDirectory(config.avdName);
    const auto descriptorFile = paths_.avd / (config.avdName + ".ini");
    const std::filesystem::path imageDirectory = systemImageDirectory(config);
    if (!std::filesystem::is_directory(imageDirectory)) {
        throw std::runtime_error("시스템 이미지 디렉터리를 찾을 수 없습니다: " + pathToUtf8(imageDirectory));
    }

    // Prefer the official avdmanager profile path. This lets current Android
    // command-line tools expand the selected built-in Phone hardware profile
    // into config.ini. Some older command-line-tools releases fail in portable
    // SDK layouts while reading system-image devices.xml; in that case we keep
    // the portable fallback below instead of making AVD creation unusable.
    if (!config.hardwareProfile.empty() && std::filesystem::is_regular_file(sdk_.avdManagerExecutable())) {
        const auto official = ProcessRunner::run(
            sdk_.avdManagerExecutable(),
            {"create", "avd", "--force", "--name", config.avdName, "--package",
             config.systemImagePackage(), "--device", config.hardwareProfile},
            sdk_.environment(), "no\n", std::chrono::minutes(3));
        if (official.exitCode == 0 && std::filesystem::is_regular_file(targetDirectory / "config.ini") &&
            std::filesystem::is_regular_file(descriptorFile)) {
            auto values = readConfigMap(targetDirectory / "config.ini");
            std::string imagePath = replaceAll(pathToUtf8(std::filesystem::absolute(imageDirectory)), "\\", "/");
            if (!imagePath.ends_with('/')) imagePath.push_back('/');
            values["image.sysdir.1"] = imagePath;
            values["hw.device.name"] = config.hardwareProfile;
            values["PlayStore.enabled"] = config.isPlayStoreImage() ? "true" : "false";
            values["abi.type"] = config.abi;
            values["tag.id"] = config.imageTag;
            values["target"] = "android-" + std::to_string(config.apiLevel);
            writeConfigMap(targetDirectory / "config.ini", values);
            applyHardwareConfig(config);
            logger_.info("공식 Phone 하드웨어 프로필로 AVD 생성 완료: " + config.hardwareProfile);
            logger_.info("AVD 저장 위치: " + pathToUtf8(targetDirectory));
            return;
        }
        logger_.warn("공식 하드웨어 프로필 AVD 생성이 실패해 포터블 fallback을 사용합니다: " +
                     trim(official.output));
        std::filesystem::remove_all(targetDirectory, ec);
        ec.clear();
        std::filesystem::remove(descriptorFile, ec);
        ec.clear();
    }

    std::filesystem::create_directories(targetDirectory, ec);
    if (ec || !std::filesystem::is_directory(targetDirectory)) {
        throw std::runtime_error("AVD 디렉터리를 만들 수 없습니다: " + pathToUtf8(targetDirectory) +
                                 (ec ? " (" + ec.message() + ")" : ""));
    }

    // Portable fallback for command-line-tools releases that cannot create an
    // AVD in a non-default SDK location. Keep it phone-oriented but generic; no
    // certified identity, build fingerprint or attestation data is written.
    std::string imagePath = replaceAll(pathToUtf8(std::filesystem::absolute(imageDirectory)), "\\", "/");
    if (!imagePath.ends_with('/')) imagePath.push_back('/');
    std::map<std::string, std::string> values;
    values["AvdId"] = config.avdName;
    values["PlayStore.enabled"] = config.isPlayStoreImage() ? "true" : "false";
    values["abi.type"] = config.abi;
    values["avd.ini.displayname"] = config.avdName;
    values["avd.ini.encoding"] = "UTF-8";
    values["hw.cpu.arch"] = config.abi == "arm64-v8a" ? "arm64" : config.abi;
    if (!config.hardwareProfile.empty()) values["hw.device.name"] = config.hardwareProfile;
    values["hw.keyboard"] = "yes";
    values["hw.mainKeys"] = "no";
    values["hw.trackBall"] = "no";
    values["hw.dPad"] = "no";
    values["hw.sensors.orientation"] = "yes";
    values["hw.sensors.proximity"] = "yes";
    values["hw.gps"] = "yes";
    values["hw.accelerometer"] = "yes";
    values["hw.gyroscope"] = "yes";
    values["hw.sensors.light"] = "yes";
    values["hw.sensors.magnetic_field"] = "yes";
    values["hw.audioInput"] = "yes";
    values["hw.battery"] = "yes";
    values["hw.gsmModem"] = "yes";
    values["hw.touchScreen"] = "yes";
    values["hw.camera.back"] = "virtualscene";
    values["hw.camera.front"] = "emulated";
    values["image.sysdir.1"] = imagePath;
    values["runtime.network.latency"] = "none";
    values["runtime.network.speed"] = "full";
    values["showDeviceFrame"] = "no";
    values["skin.dynamic"] = "yes";
    values["tag.id"] = config.imageTag;
    values["target"] = "android-" + std::to_string(config.apiLevel);
    writeConfigMap(targetDirectory / "config.ini", values);

    std::ostringstream descriptor;
    descriptor << "avd.ini.encoding=UTF-8\n";
    descriptor << "path=" << pathToUtf8(targetDirectory) << '\n';
    descriptor << "path.rel=" << config.avdName << ".avd\n";
    descriptor << "target=android-" << config.apiLevel << '\n';
    writeTextFileAtomic(descriptorFile, descriptor.str());

    if (!std::filesystem::is_regular_file(targetDirectory / "config.ini") ||
        !std::filesystem::is_regular_file(descriptorFile)) {
        std::filesystem::remove_all(targetDirectory, ec);
        std::filesystem::remove(descriptorFile, ec);
        throw std::runtime_error("포터블 AVD 설정 파일을 생성하지 못했습니다.");
    }

    applyHardwareConfig(config);
    logger_.info("포터블 AVD 생성 완료: " + config.avdName);
    logger_.info("AVD 저장 위치: " + pathToUtf8(targetDirectory));
}
void AvdManager::deleteAvd(const std::string& name) {
    if (!isSafeAvdName(name)) throw std::runtime_error("안전하지 않은 AVD 이름입니다.");
    if (std::filesystem::is_regular_file(sdk_.avdManagerExecutable())) {
        const auto result = ProcessRunner::run(sdk_.avdManagerExecutable(),
            {"delete", "avd", "--name", name}, sdk_.environment(), {}, std::chrono::minutes(2));
        if (result.exitCode != 0 && exists(name)) {
            logger_.warn("avdmanager 삭제가 실패해 포터블 AVD 파일을 직접 정리합니다.");
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(avdDirectory(name), ec);
    std::filesystem::remove(paths_.avd / (name + ".ini"), ec);
    logger_.info("AVD 삭제: " + name);
}

void AvdManager::applyHardwareConfig(const AppConfig& source) {
    AppConfig config = source;
    config.normalize();
    const auto directory = avdDirectory(config.avdName);
    const auto file = directory / "config.ini";
    if (!std::filesystem::is_directory(directory)) throw std::runtime_error("AVD 디렉터리를 찾지 못했습니다.");
    auto values = readConfigMap(file);
    values["hw.ramSize"] = std::to_string(config.ramMb);
    values["hw.cpu.ncore"] = std::to_string(config.cpuCores);
    values["disk.dataPartition.size"] = std::to_string(config.dataPartitionGb) + "G";
    values["hw.lcd.width"] = std::to_string(config.screenWidth);
    values["hw.lcd.height"] = std::to_string(config.screenHeight);
    values["hw.lcd.density"] = std::to_string(config.densityDpi);
    values["hw.gpu.enabled"] = config.gpuMode == "off" ? "no" : "yes";
    values["hw.gpu.mode"] = gpuConfigValue(config.gpuMode);
    values["hw.keyboard"] = "yes";
    values["fastboot.forceColdBoot"] = config.coldBoot ? "yes" : "no";
    values["fastboot.forceFastBoot"] = config.coldBoot ? "no" : "yes";
    // Hardware/layout values only. Certified identity, build fingerprint and attestation fields are never written.
    for (auto it = values.begin(); it != values.end();) {
        if (isForbiddenIdentityProperty(it->first)) it = values.erase(it);
        else ++it;
    }
    writeConfigMap(file, values);
}

std::vector<std::string> AvdManager::listAvds() const {
    std::vector<std::string> result;
    if (!std::filesystem::is_directory(paths_.avd)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(paths_.avd)) {
        if (!entry.is_directory() || entry.path().extension() != ".avd") continue;
        std::string name = pathToUtf8(entry.path().stem());
        if (isSafeAvdName(name)) result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

AppConfig AvdManager::loadConfig(const std::string& name, const AppConfig& defaults) const {
    AppConfig config = defaults;
    config.avdName = name;
    const auto values = readConfigMap(avdDirectory(name) / "config.ini");
    auto getValue = [&](const std::string& key) -> std::string {
        const auto it = values.find(key);
        return it == values.end() ? std::string{} : it->second;
    };
    const std::string target = getValue("target");
    if (target.rfind("android-", 0) == 0) config.apiLevel = parseInt(target.substr(8)).value_or(config.apiLevel);
    if (!getValue("tag.id").empty()) config.imageTag = getValue("tag.id");
    if (!getValue("abi.type").empty()) config.abi = getValue("abi.type");
    if (!getValue("hw.device.name").empty()) config.hardwareProfile = getValue("hw.device.name");
    config.ramMb = mapInt(values, "hw.ramSize", config.ramMb);
    config.cpuCores = mapInt(values, "hw.cpu.ncore", config.cpuCores);
    config.screenWidth = mapInt(values, "hw.lcd.width", config.screenWidth);
    config.screenHeight = mapInt(values, "hw.lcd.height", config.screenHeight);
    config.densityDpi = mapInt(values, "hw.lcd.density", config.densityDpi);
    const auto partition = getValue("disk.dataPartition.size");
    if (!partition.empty()) config.dataPartitionGb = parseSizeGb(partition, config.dataPartitionGb);
    const auto gpu = getValue("hw.gpu.mode");
    if (!gpu.empty()) config.gpuMode = gpu == "swiftshader_indirect" ? "software" : gpu;
    const auto gpuEnabled = getValue("hw.gpu.enabled");
    if (gpuEnabled == "no") config.gpuMode = "off";
    config.normalize();
    return config;
}

std::vector<AvdInfo> AvdManager::listAvdInfos() const {
    std::vector<AvdInfo> result;
    AppConfig defaults;
    defaults.normalize();
    for (const auto& name : listAvds()) {
        const auto config = loadConfig(name, defaults);
        AvdInfo info;
        info.name = name;
        info.systemImagePackage = config.systemImagePackage();
        info.hardwareProfile = config.hardwareProfile;
        info.abi = config.abi;
        info.apiLevel = config.apiLevel;
        info.ramMb = config.ramMb;
        info.cpuCores = config.cpuCores;
        info.screenWidth = config.screenWidth;
        info.screenHeight = config.screenHeight;
        info.densityDpi = config.densityDpi;
        info.problem = launchProblem(config);
        info.launchable = info.problem.empty();
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}

bool AvdManager::systemImageInUse(const std::string& package) const {
    for (const auto& info : listAvdInfos()) {
        if (info.systemImagePackage == package) return true;
    }
    return false;
}

bool AvdManager::exists(const std::string& name) const {
    return isSafeAvdName(name) && std::filesystem::is_directory(avdDirectory(name));
}

bool AvdManager::launchable(const AppConfig& source) const {
    return launchProblem(source).empty();
}

std::string AvdManager::launchProblem(const AppConfig& source) const {
    AppConfig config = source;
    config.normalize();
    if (!isSafeAvdName(config.avdName)) return "AVD 이름이 올바르지 않습니다.";
    const auto directory = avdDirectory(config.avdName);
    if (!std::filesystem::is_regular_file(directory / "config.ini")) {
        return "AVD config.ini가 없습니다.";
    }
    if (!std::filesystem::is_regular_file(paths_.avd / (config.avdName + ".ini"))) {
        return "AVD descriptor .ini가 없습니다.";
    }
    const auto image = systemImageDirectory(config);
    if (!std::filesystem::is_directory(image)) return "시스템 이미지 디렉터리가 없습니다.";
    if (!std::filesystem::is_regular_file(image / "system.img")) {
        return "시스템 이미지의 system.img가 없습니다.";
    }
    return {};
}

std::filesystem::path AvdManager::systemImageDirectory(const AppConfig& source) const {
    AppConfig config = source;
    config.normalize();
    return paths_.sdk / "system-images" / ("android-" + std::to_string(config.apiLevel)) /
           config.imageTag / config.abi;
}

std::filesystem::path AvdManager::avdDirectory(const std::string& name) const {
    if (!isSafeAvdName(name)) return paths_.avd / "invalid.avd";
    return paths_.avd / (name + ".avd");
}

} // namespace pavm
