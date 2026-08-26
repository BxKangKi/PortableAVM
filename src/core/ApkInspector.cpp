#include "core/ApkInspector.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

namespace pavm {
namespace {

std::uint16_t read16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8U);
}

std::uint32_t read32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::string abiFromEntry(const std::string& name) {
    if (!startsWith(name, "lib/")) {
        return {};
    }
    const std::size_t slash = name.find('/', 4);
    if (slash == std::string::npos || slash == 4 || !endsWith(toLowerAscii(name), ".so")) {
        return {};
    }
    return name.substr(4, slash - 4);
}

} // namespace

ApkInspection inspectApk(const std::filesystem::path& apk) {
    ApkInspection result;
    std::ifstream input(apk, std::ios::binary);
    if (!input) {
        result.summary = "APK 파일을 열 수 없습니다.";
        return result;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 22) {
        result.summary = "ZIP/APK 파일이 너무 작습니다.";
        return result;
    }

    const std::streamoff tailSize = std::min<std::streamoff>(fileSize, 65557);
    input.seekg(fileSize - tailSize);
    std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
    input.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()));

    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail.size() - 22; ; --i) {
        if (read32(tail.data() + i) == 0x06054b50U) {
            eocd = i;
            break;
        }
        if (i == 0) {
            break;
        }
    }
    if (eocd == std::string::npos) {
        result.summary = "ZIP 중앙 디렉터리를 찾지 못했습니다.";
        return result;
    }

    const std::uint16_t entries = read16(tail.data() + eocd + 10);
    const std::uint32_t centralSize = read32(tail.data() + eocd + 12);
    const std::uint32_t centralOffset = read32(tail.data() + eocd + 16);
    if (static_cast<std::uint64_t>(centralOffset) + centralSize > static_cast<std::uint64_t>(fileSize)) {
        result.summary = "손상된 ZIP 중앙 디렉터리입니다.";
        return result;
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(centralOffset));
    std::set<std::string> abis;
    bool manifest = false;
    for (std::uint32_t index = 0; index < entries; ++index) {
        std::array<unsigned char, 46> header{};
        input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (input.gcount() != static_cast<std::streamsize>(header.size()) || read32(header.data()) != 0x02014b50U) {
            result.summary = "손상된 ZIP 항목입니다.";
            return result;
        }
        const std::uint16_t nameLength = read16(header.data() + 28);
        const std::uint16_t extraLength = read16(header.data() + 30);
        const std::uint16_t commentLength = read16(header.data() + 32);
        if (nameLength == 0 || nameLength > 32768) {
            result.summary = "비정상 ZIP 항목 이름입니다.";
            return result;
        }
        std::string name(nameLength, '\0');
        input.read(name.data(), static_cast<std::streamsize>(nameLength));
        if (!input) {
            result.summary = "ZIP 항목 이름을 읽지 못했습니다.";
            return result;
        }
        input.seekg(static_cast<std::streamoff>(extraLength) + commentLength, std::ios::cur);
        if (name == "AndroidManifest.xml") {
            manifest = true;
        }
        const std::string abi = abiFromEntry(name);
        if (!abi.empty()) {
            abis.insert(abi);
        }
    }

    result.validZip = manifest;
    result.hasNativeCode = !abis.empty();
    result.abis.assign(abis.begin(), abis.end());
    if (!manifest) {
        result.summary = "AndroidManifest.xml이 없어 APK로 확인되지 않습니다.";
    } else if (abis.empty()) {
        result.summary = "네이티브 라이브러리가 없는 범용 APK입니다.";
    } else {
        result.summary = "APK 네이티브 ABI: " + join(result.abis, ", ");
    }
    return result;
}

bool apkSupportsAbi(const ApkInspection& inspection, const std::string& abi) {
    if (!inspection.validZip) {
        return false;
    }
    if (!inspection.hasNativeCode) {
        return true;
    }
    if (std::find(inspection.abis.begin(), inspection.abis.end(), abi) != inspection.abis.end()) {
        return true;
    }
    if (abi == "arm64-v8a" && std::find(inspection.abis.begin(), inspection.abis.end(), "armeabi-v7a") != inspection.abis.end()) {
        return true;
    }
    if (abi == "x86_64" && std::find(inspection.abis.begin(), inspection.abis.end(), "x86") != inspection.abis.end()) {
        return true;
    }
    return false;
}

} // namespace pavm
