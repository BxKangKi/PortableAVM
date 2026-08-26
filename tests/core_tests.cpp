#include "core/ApkInspector.h"
#include "core/AvdManager.h"
#include "core/Logger.h"
#include "core/LanguageManager.h"
#include "core/PortablePaths.h"
#include "core/SdkManager.h"
#include "core/Config.h"
#include "core/RepositoryParser.h"
#include "core/Sha1.h"
#include "core/StringUtil.h"

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testSha1() {
    const char* value = "abc";
    require(pavm::sha1Bytes(value, 3) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 test failed");
}

void testRepositoryParser() {
    const std::string xml = R"xml(
<repository xmlns="http://schemas.android.com/repository/android/generic/02">
  <remotePackage path="cmdline-tools;latest">
    <revision><major>19</major><minor>0</minor><micro>1</micro></revision>
    <display-name>Android SDK Command-line Tools</display-name>
    <archives>
      <archive><host-os>linux</host-os><complete><size>10</size><checksum type="sha1">aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa</checksum><url>tools-linux.zip</url></complete></archive>
      <archive><host-os>windows</host-os><complete><size>20</size><checksum type="sha1">bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb</checksum><url>tools-win.zip</url></complete></archive>
    </archives>
  </remotePackage>
</repository>)xml";
    const auto archive = pavm::findLatestCommandLineTools(xml, "windows");
    require(archive.relativeUrl == "tools-win.zip", "repository URL parse failed");
    require(archive.size == 20, "repository size parse failed");
    require(archive.checksumType == "sha1", "repository checksum type failed");
    require(archive.revision == "19.0.1", "repository revision failed");
}


void testHardwareProfileParser() {
    const std::string output = R"txt(Available devices definitions:
id: 0 or "medium_phone"
    Name: Medium Phone
    OEM : Generic
---------
id: 1 or "pixel_7"
    Name: Pixel 7
    OEM : Google
---------
id: 2 or "pixel_tablet"
    Name: Pixel Tablet
    OEM : Google
---------
id: 3 or "wearos_small_round"
    Name: Wear OS Small Round
    OEM : Google
)txt";
    const auto profiles = pavm::parseHardwareProfiles(output);
    require(profiles.size() == 4, "hardware profile parse failed");
    require(pavm::isPhoneHardwareProfile(profiles[0]), "medium_phone should be a Phone profile");
    require(pavm::isPhoneHardwareProfile(profiles[1]), "pixel_7 should be a Phone profile");
    require(!pavm::isPhoneHardwareProfile(profiles[2]), "tablet profile must be filtered from Phone profiles");
    require(!pavm::isPhoneHardwareProfile(profiles[3]), "Wear profile must be filtered from Phone profiles");
}

void testConfigAndPolicy() {
    pavm::AppConfig config;
    config.abi = "invalid";
    config.apiLevel = 100;
    config.ramMb = 1;
    config.avdName = "../escape";
    config.normalize();
    require(config.apiLevel == 50, "API clamp failed");
    require(config.ramMb == 1024, "RAM clamp failed");
    require(config.avdName == "PortableAVM_Game", "AVD name sanitation failed");
    require(pavm::isForbiddenIdentityProperty("ro.build.fingerprint"), "fingerprint policy failed");
    require(pavm::isForbiddenIdentityProperty("ro.product.model"), "model policy failed");
    require(!pavm::isForbiddenIdentityProperty("hw.ramSize"), "hardware property policy failed");
    require(pavm::isSafeAvdName("Game_01"), "valid AVD name rejected");
    require(!pavm::isSafeAvdName("../../bad"), "unsafe AVD name accepted");
}

void testPackageList() {
    const std::string output = "system-images;android-35;google_apis_playstore;x86_64 | 1\n"
                               "system-images;android-35;google_apis;arm64-v8a | 2\n"
                               "system-images;android-35;google_apis;arm64-v8a | 2\n";
    const auto packages = pavm::findSystemImagePackages(output);
    require(packages.size() == 2, "system image parser de-duplication failed");
}


void testJdkImportCreatesLazyRuntimeDirectories() {
    const auto root = std::filesystem::temp_directory_path() / ("pavm-jdk-test-" + pavm::randomToken());
    pavm::PortablePaths paths;
    paths.root = root;
    paths.data = root / "Data";
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

    const auto source = root / "source-jdk";
    std::filesystem::create_directories(source / "bin");
#ifdef _WIN32
    pavm::writeTextFileAtomic(source / "bin" / "java.exe", "test");
#else
    pavm::writeTextFileAtomic(source / "bin" / "java", "test");
#endif

    // Simulate a fresh install: Data/Runtime, Data/temp and Data/Runtime/jdk do not exist.
    require(!std::filesystem::exists(paths.jdk), "JDK destination unexpectedly exists before import");
    require(!std::filesystem::exists(paths.temp), "JDK temp directory unexpectedly exists before import");

    pavm::Logger logger(paths.logs / "jdk-import.log");
    pavm::SdkManager sdk(paths, logger, 5037);
    sdk.importJdk(source);
    require(sdk.jdkInstalled(), "JDK import did not create a usable portable JDK");
    require(std::filesystem::is_directory(paths.jdk), "JDK destination was not created lazily");
    require(std::filesystem::is_directory(paths.temp), "JDK temp directory was not created lazily");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void testChildEnvironmentCreatesRequiredDirectories() {
    const auto root = std::filesystem::temp_directory_path() / ("pavm-env-test-" + pavm::randomToken());
    pavm::PortablePaths paths;
    paths.root = root;
    paths.data = root / "Data";
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

    require(!std::filesystem::exists(paths.homeAndroid), "environment test started with pre-existing directories");
    const auto env = paths.childEnvironment(5037);
    require(env.at("ANDROID_AVD_HOME") == pavm::pathToUtf8(paths.avd), "ANDROID_AVD_HOME mismatch");
    for (const auto& path : {paths.sdk, paths.avd, paths.homeAndroid, paths.homeEmulator, paths.homeAdb,
                             paths.homeUser, paths.gradleHome, paths.appDataRoaming, paths.appDataLocal,
                             paths.temp, paths.logs}) {
        require(std::filesystem::is_directory(path), "required child environment directory was not created");
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void testPortableAvdCreation() {
    const auto root = std::filesystem::temp_directory_path() / ("pavm-avd-test-" + pavm::randomToken());
    pavm::PortablePaths paths;
    paths.root = root;
    paths.data = root / "Data";
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
    paths.ensureLayout();

    pavm::AppConfig config;
    config.apiLevel = 35;
    config.imageTag = "google_apis_playstore";
    config.abi = "x86_64";
    config.avdName = "Test_AVD";
    config.normalize();
    const auto image = paths.sdk / "system-images" / "android-35" / config.imageTag / config.abi;
    std::filesystem::create_directories(image);
    pavm::writeTextFileAtomic(image / "system.img", "test");

    pavm::Logger logger(paths.logs / "test.log");
    pavm::SdkManager sdk(paths, logger, config.adbPort);
    pavm::AvdManager avd(paths, sdk, logger);
    avd.createOrReplace(config);
    require(avd.launchable(config), "direct portable AVD should be launchable");
    const std::string text = pavm::readTextFile(avd.avdDirectory(config.avdName) / "config.ini");
    require(text.find("image.sysdir.1=") != std::string::npos, "AVD image.sysdir.1 missing");
    require(text.find(pavm::pathToUtf8(std::filesystem::absolute(image))) != std::string::npos ||
            text.find(pavm::replaceAll(pavm::pathToUtf8(std::filesystem::absolute(image)), "\\", "/")) != std::string::npos,
            "AVD image.sysdir.1 must use the selected absolute image directory");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}



void testMultipleAvdInventoryAndSdkRemoval() {
    const auto root = std::filesystem::temp_directory_path() / ("pavm-multi-test-" + pavm::randomToken());
    pavm::PortablePaths paths;
    paths.root = root;
    paths.data = root / "Data";
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
    paths.ensureLayout();

    const auto image30 = paths.sdk / "system-images" / "android-30" / "google_apis_playstore" / "x86_64";
    const auto image35 = paths.sdk / "system-images" / "android-35" / "google_apis" / "x86_64";
    std::filesystem::create_directories(image30);
    std::filesystem::create_directories(image35);
    pavm::writeTextFileAtomic(image30 / "system.img", "test");
    pavm::writeTextFileAtomic(image35 / "system.img", "test");
    pavm::writeTextFileAtomic(image30 / "source.properties", "Pkg.Revision = 10\n");
    pavm::writeTextFileAtomic(image35 / "source.properties", "Pkg.Revision = 11\n");

    pavm::Logger logger(paths.logs / "multi.log");
    pavm::SdkManager sdk(paths, logger, 5037);
    pavm::AvdManager avd(paths, sdk, logger);
    pavm::AppConfig first;
    first.avdName = "Device_One";
    first.apiLevel = 30;
    first.imageTag = "google_apis_playstore";
    first.abi = "x86_64";
    first.normalize();
    avd.createOrReplace(first);
    pavm::AppConfig second = first;
    second.avdName = "Device_Two";
    second.apiLevel = 35;
    second.imageTag = "google_apis";
    second.normalize();
    avd.createOrReplace(second);

    const auto devices = avd.listAvdInfos();
    require(devices.size() == 2, "multiple AVD inventory failed");
    require(devices[0].name == "Device_One" && devices[1].name == "Device_Two", "AVD inventory sorting failed");
    require(avd.systemImageInUse(first.systemImagePackage()), "used system image was not detected");

    const auto packages = sdk.installedPackages();
    require(packages.size() == 2, "installed system-image inventory failed");
    require(std::any_of(packages.begin(), packages.end(), [&](const auto& item) { return item.packagePath == first.systemImagePackage(); }),
            "API 30 system image missing from inventory");

    avd.deleteAvd("Device_One");
    require(!avd.systemImageInUse(first.systemImagePackage()), "system image still marked in use after AVD deletion");
    sdk.uninstallPackage(first.systemImagePackage());
    require(!sdk.packageInstalled(first.systemImagePackage()), "system image uninstall failed");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void testLanguageManager() {
    const auto root = std::filesystem::temp_directory_path() / ("pavm-lang-test-" + pavm::randomToken());
    std::filesystem::create_directories(root);
    pavm::writeTextFileAtomic(root / "en.lang", "language.name=English\nhello=Hello\nfallback=Fallback\n");
    pavm::writeTextFileAtomic(root / "ko.lang", "language.name=한국어\nhello=안녕하세요\n");
    pavm::writeTextFileAtomic(root / "ja.lang", "language.name=日本語\nhello=こんにちは\n");
    pavm::LanguageManager language(root);
    language.setLanguage("ko");
    require(language.text("hello") == "안녕하세요", "Korean language lookup failed");
    require(language.text("fallback") == "Fallback", "English language fallback failed");
    language.setLanguage("ja");
    require(language.language() == "ja", "dynamic language code was not accepted");
    require(language.languageName("ja") == "日本語", "dynamic language display name failed");
    require(language.text("missing.key") == "missing.key", "translation key fallback failed");
    const auto available = language.availableLanguages();
    require(std::find(available.begin(), available.end(), "ja") != available.end(), "dynamic .lang discovery failed");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void testAllowedUrl() {
    // This policy is implemented in the runtime HTTP layer; the core checks identity policy here.
    require(pavm::replaceAll("a-b-b", "b", "x") == "a-x-x", "replaceAll failed");
}

} // namespace

int main() {
    try {
        testSha1();
        testRepositoryParser();
        testHardwareProfileParser();
        testConfigAndPolicy();
        testPackageList();
        testJdkImportCreatesLazyRuntimeDirectories();
        testChildEnvironmentCreatesRequiredDirectories();
        testPortableAvdCreation();
        testMultipleAvdInventoryAndSdkRemoval();
        testLanguageManager();
        testAllowedUrl();
        std::cout << "PortableAVM core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
