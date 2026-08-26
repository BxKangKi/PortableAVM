#include "core/HostInfo.h"
#include "core/StringUtil.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pavm {

HostArchitecture hostArchitecture() {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return HostArchitecture::X86_64;
        case PROCESSOR_ARCHITECTURE_ARM64: return HostArchitecture::Arm64;
        case PROCESSOR_ARCHITECTURE_INTEL: return HostArchitecture::X86;
        default: return HostArchitecture::Unknown;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    return HostArchitecture::Arm64;
#elif defined(__x86_64__) || defined(_M_X64)
    return HostArchitecture::X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return HostArchitecture::X86;
#else
    return HostArchitecture::Unknown;
#endif
}

std::string hostArchitectureName() {
    switch (hostArchitecture()) {
        case HostArchitecture::X86_64: return "x86_64";
        case HostArchitecture::Arm64: return "arm64";
        case HostArchitecture::X86: return "x86";
        default: return "unknown";
    }
}

bool abiMatchesHost(const std::string& abiValue) {
    const std::string abi = toLowerAscii(abiValue);
    const auto host = hostArchitecture();
    if (host == HostArchitecture::Arm64) {
        return abi == "arm64-v8a" || abi == "arm64";
    }
    if (host == HostArchitecture::X86_64) {
        return abi == "x86_64" || abi == "x86";
    }
    if (host == HostArchitecture::X86) {
        return abi == "x86";
    }
    return false;
}

std::string recommendedAbi() {
    return hostArchitecture() == HostArchitecture::Arm64 ? "arm64-v8a" : "x86_64";
}

std::string hostOsName() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macosx";
#else
    return "linux";
#endif
}

} // namespace pavm
