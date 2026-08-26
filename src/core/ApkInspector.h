#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pavm {

struct ApkInspection {
    bool validZip = false;
    bool hasNativeCode = false;
    std::vector<std::string> abis;
    std::string summary;
};

ApkInspection inspectApk(const std::filesystem::path& apk);
bool apkSupportsAbi(const ApkInspection& inspection, const std::string& abi);

} // namespace pavm
