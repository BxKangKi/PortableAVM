#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace pavm {

struct PortablePaths {
    std::filesystem::path root;
    std::filesystem::path data;
    std::filesystem::path bin;
    std::filesystem::path configs;
    std::filesystem::path lang;
    std::filesystem::path sdk;
    std::filesystem::path jdk;
    std::filesystem::path avd;
    std::filesystem::path homeAndroid;
    std::filesystem::path homeEmulator;
    std::filesystem::path homeAdb;
    std::filesystem::path homeUser;
    std::filesystem::path gradleHome;
    std::filesystem::path appDataRoaming;
    std::filesystem::path appDataLocal;
    std::filesystem::path downloads;
    std::filesystem::path cache;
    std::filesystem::path temp;
    std::filesystem::path logs;
    std::filesystem::path locks;

    static PortablePaths discover();
    void ensureLayout() const;
    void ensureChildEnvironmentLayout() const;
    [[nodiscard]] std::map<std::string, std::string> childEnvironment(int adbPort) const;
    [[nodiscard]] std::filesystem::path settingsFile() const;
    [[nodiscard]] std::filesystem::path logFile() const;
};

} // namespace pavm
