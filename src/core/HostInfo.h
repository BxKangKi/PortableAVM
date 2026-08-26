#pragma once

#include <string>

namespace pavm {

enum class HostArchitecture {
    X86_64,
    Arm64,
    X86,
    Unknown
};

HostArchitecture hostArchitecture();
std::string hostArchitectureName();
bool abiMatchesHost(const std::string& abi);
std::string recommendedAbi();
std::string hostOsName();

} // namespace pavm
