#include "core/Config.h"
#include "core/HostInfo.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace pavm {
namespace {

std::map<std::string, std::string> parseIni(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    std::string section;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = toLowerAscii(trim(std::string_view(line).substr(1, line.size() - 2)));
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos) {
            continue;
        }
        const std::string key = toLowerAscii(trim(std::string_view(line).substr(0, equal)));
        const std::string value = trim(std::string_view(line).substr(equal + 1));
        values[(section.empty() ? "" : section + ".") + key] = value;
    }
    return values;
}

std::string get(const std::map<std::string, std::string>& values, const std::string& key,
                const std::string& fallback) {
    const auto it = values.find("portableavm." + toLowerAscii(key));
    return it == values.end() ? fallback : it->second;
}

int getInt(const std::map<std::string, std::string>& values, const std::string& key, int fallback) {
    const auto parsed = parseInt(get(values, key, ""));
    return parsed.value_or(fallback);
}

bool getBool(const std::map<std::string, std::string>& values, const std::string& key, bool fallback) {
    return parseBool(get(values, key, boolText(fallback)), fallback);
}

std::string cleanValue(std::string value) {
    value = replaceAll(std::move(value), "\r", "");
    value = replaceAll(std::move(value), "\n", " ");
    return value;
}

} // namespace

void AppConfig::normalize() {
    if (abi.empty()) {
        abi = recommendedAbi();
    }
    apiLevel = std::clamp(apiLevel, 21, 50);
    ramMb = std::clamp(ramMb, 1024, 32768);
    cpuCores = std::clamp(cpuCores, 1, 32);
    dataPartitionGb = std::clamp(dataPartitionGb, 8, 512);
    screenWidth = std::clamp(screenWidth, 480, 4320);
    screenHeight = std::clamp(screenHeight, 800, 7680);
    densityDpi = std::clamp(densityDpi, 120, 960);
    adbPort = std::clamp(adbPort, 1024, 65535);

    if (imageTag != "default" && imageTag != "google_apis" && imageTag != "google_apis_playstore") {
        imageTag = "google_apis";
    }
    if (abi != "x86_64" && abi != "x86" && abi != "arm64-v8a" && abi != "armeabi-v7a") {
        abi = recommendedAbi();
    }
    if (gpuMode != "auto" && gpuMode != "host" && gpuMode != "swiftshader_indirect" &&
        gpuMode != "software" && gpuMode != "off") {
        gpuMode = "auto";
    }
    if (!isSafeAvdName(avdName)) {
        avdName = "PortableAVM_Game";
    }
    language = cleanValue(language);
    if (language.empty() || language.size() > 32 ||
        !std::all_of(language.begin(), language.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '_' || c == '-';
        })) {
        language = "ko";
    }
    hardwareProfile = cleanValue(hardwareProfile);
    if (hardwareProfile.empty()) {
        hardwareProfile = "medium_phone";
    }
}

std::string AppConfig::systemImagePackage() const {
    return "system-images;android-" + std::to_string(apiLevel) + ";" + imageTag + ";" + abi;
}

bool AppConfig::isArmImage() const {
    return abi == "arm64-v8a" || abi == "armeabi-v7a";
}

bool AppConfig::isPlayStoreImage() const {
    return imageTag == "google_apis_playstore";
}

AppConfig AppConfig::load(const std::filesystem::path& path) {
    AppConfig config;
    config.abi = recommendedAbi();
    if (!std::filesystem::exists(path)) {
        config.normalize();
        return config;
    }
    const auto values = parseIni(readTextFile(path));
    config.sdkDownloadConsent = getBool(values, "sdk_download_consent", false);
    config.consentDocument = get(values, "consent_document", config.consentDocument);
    config.consentTimestamp = get(values, "consent_timestamp", "");
    config.apiLevel = getInt(values, "api_level", config.apiLevel);
    config.imageTag = get(values, "image_tag", config.imageTag);
    config.abi = get(values, "abi", config.abi);
    config.avdName = get(values, "avd_name", config.avdName);
    config.hardwareProfile = get(values, "hardware_profile", config.hardwareProfile);
    config.ramMb = getInt(values, "ram_mb", config.ramMb);
    config.cpuCores = getInt(values, "cpu_cores", config.cpuCores);
    config.dataPartitionGb = getInt(values, "data_partition_gb", config.dataPartitionGb);
    config.screenWidth = getInt(values, "screen_width", config.screenWidth);
    config.screenHeight = getInt(values, "screen_height", config.screenHeight);
    config.densityDpi = getInt(values, "density_dpi", config.densityDpi);
    config.gpuMode = get(values, "gpu_mode", config.gpuMode);
    config.hardwareAcceleration = getBool(values, "hardware_acceleration", config.hardwareAcceleration);
    config.coldBoot = getBool(values, "cold_boot", config.coldBoot);
    config.wipeDataNextBoot = getBool(values, "wipe_data_next_boot", config.wipeDataNextBoot);
    config.saveSnapshot = getBool(values, "save_snapshot", config.saveSnapshot);
    config.noAudio = getBool(values, "no_audio", config.noAudio);
    config.noBootAnimation = getBool(values, "no_boot_animation", config.noBootAnimation);
    config.adbPort = getInt(values, "adb_port", config.adbPort);
    config.apkPath = get(values, "apk_path", "");
    config.jdkSourcePath = get(values, "jdk_source_path", "");
    config.commandLineToolsArchivePath = get(values, "command_line_tools_archive_path", "");
    config.language = get(values, "language", config.language);
    config.normalize();
    return config;
}

void AppConfig::save(const std::filesystem::path& path) const {
    AppConfig normalized = *this;
    normalized.normalize();
    std::ostringstream out;
    out << "; PortableAVM configuration. All paths are kept under data.\n";
    out << "[portableavm]\n";
    out << "sdk_download_consent=" << boolText(normalized.sdkDownloadConsent) << '\n';
    out << "consent_document=" << cleanValue(normalized.consentDocument) << '\n';
    out << "consent_timestamp=" << cleanValue(normalized.consentTimestamp) << '\n';
    out << "api_level=" << normalized.apiLevel << '\n';
    out << "image_tag=" << cleanValue(normalized.imageTag) << '\n';
    out << "abi=" << cleanValue(normalized.abi) << '\n';
    out << "avd_name=" << cleanValue(normalized.avdName) << '\n';
    out << "hardware_profile=" << cleanValue(normalized.hardwareProfile) << '\n';
    out << "ram_mb=" << normalized.ramMb << '\n';
    out << "cpu_cores=" << normalized.cpuCores << '\n';
    out << "data_partition_gb=" << normalized.dataPartitionGb << '\n';
    out << "screen_width=" << normalized.screenWidth << '\n';
    out << "screen_height=" << normalized.screenHeight << '\n';
    out << "density_dpi=" << normalized.densityDpi << '\n';
    out << "gpu_mode=" << cleanValue(normalized.gpuMode) << '\n';
    out << "hardware_acceleration=" << boolText(normalized.hardwareAcceleration) << '\n';
    out << "cold_boot=" << boolText(normalized.coldBoot) << '\n';
    out << "wipe_data_next_boot=" << boolText(normalized.wipeDataNextBoot) << '\n';
    out << "save_snapshot=" << boolText(normalized.saveSnapshot) << '\n';
    out << "no_audio=" << boolText(normalized.noAudio) << '\n';
    out << "no_boot_animation=" << boolText(normalized.noBootAnimation) << '\n';
    out << "adb_port=" << normalized.adbPort << '\n';
    out << "apk_path=" << cleanValue(normalized.apkPath) << '\n';
    out << "jdk_source_path=" << cleanValue(normalized.jdkSourcePath) << '\n';
    out << "command_line_tools_archive_path=" << cleanValue(normalized.commandLineToolsArchivePath) << '\n';
    out << "language=" << cleanValue(normalized.language) << '\n';
    writeTextFileAtomic(path, out.str());
}

bool isSafeAvdName(const std::string& value) {
    if (value.empty() || value.size() > 64 || value == "." || value == "..") {
        return false;
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    const std::string upper = toLowerAscii(value);
    static const char* reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                     "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                     "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* name : reserved) {
        if (upper == name) {
            return false;
        }
    }
    return value.find("..") == std::string::npos;
}

bool isForbiddenIdentityProperty(const std::string& keyValue) {
    const std::string key = toLowerAscii(trim(keyValue));
    return startsWith(key, "ro.build.fingerprint") || startsWith(key, "ro.product.model") ||
           startsWith(key, "ro.product.brand") || startsWith(key, "ro.product.manufacturer") ||
           startsWith(key, "ro.boot.vbmeta") || startsWith(key, "ro.boot.verifiedbootstate") ||
           startsWith(key, "ro.build.tags") || startsWith(key, "ro.build.type") ||
           startsWith(key, "ro.serialno") || startsWith(key, "persist.sys.pi") ||
           key.find("play_integrity") != std::string::npos || key.find("attestation") != std::string::npos;
}

} // namespace pavm
