#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pavm {

struct HardwareProfileInfo {
    std::string id;
    std::string name;
    std::string oem;
    std::string tag;
};

struct RepositoryArchive {
    std::string packagePath;
    std::string hostOs;
    std::string relativeUrl;
    std::uint64_t size = 0;
    std::string checksum;
    std::string checksumType;
    std::string displayName;
    std::string revision;
};

RepositoryArchive findLatestCommandLineTools(const std::string& xml, const std::string& hostOs);
std::vector<std::string> findSystemImagePackages(const std::string& sdkManagerOutput);
std::vector<HardwareProfileInfo> parseHardwareProfiles(const std::string& avdManagerOutput);
bool isPhoneHardwareProfile(const HardwareProfileInfo& profile);
std::string xmlDecode(std::string value);

} // namespace pavm
