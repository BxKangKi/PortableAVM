#include "core/ArchiveExtractor.h"
#include "core/Process.h"
#include "core/StringUtil.h"

#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pavm {
namespace {

std::filesystem::path tarExecutable() {
#ifdef _WIN32
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"tar.exe";
    return std::filesystem::path(systemDirectory) / L"tar.exe";
#else
    if (std::filesystem::exists("/usr/bin/tar")) return "/usr/bin/tar";
    if (std::filesystem::exists("/bin/tar")) return "/bin/tar";
    return "tar";
#endif
}

bool unsafeEntry(const std::string& raw) {
    std::string value = replaceAll(trim(raw), "\\", "/");
    if (value.empty()) return false;
    if (value.front() == '/' || value.front() == '~') return true;
    if (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 && value[1] == ':') return true;
    for (const std::string& part : split(value, '/', true)) {
        if (part == "..") return true;
    }
    return false;
}

} // namespace

void ArchiveExtractor::extractZipSafely(const std::filesystem::path& archive,
                                        const std::filesystem::path& destination) {
    if (!std::filesystem::is_regular_file(archive)) {
        throw std::runtime_error("Archive does not exist: " + pathToUtf8(archive));
    }
    const auto tar = tarExecutable();
    const auto listing = ProcessRunner::run(tar, {"-tf", pathToUtf8(archive)}, {}, {}, std::chrono::minutes(2));
    if (listing.exitCode != 0) {
        throw std::runtime_error("Cannot list ZIP archive: " + listing.output);
    }
    std::istringstream entries(listing.output);
    std::string entry;
    std::size_t count = 0;
    while (std::getline(entries, entry)) {
        if (++count > 200000) throw std::runtime_error("Archive contains too many entries");
        if (unsafeEntry(entry)) throw std::runtime_error("Unsafe path in archive: " + entry);
    }
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    ec.clear();
    std::filesystem::create_directories(destination, ec);
    if (ec || !std::filesystem::is_directory(destination)) {
        throw std::runtime_error("Cannot create extraction directory: " + pathToUtf8(destination) +
                                 (ec ? " (" + ec.message() + ")" : ""));
    }
    const auto extraction = ProcessRunner::run(tar,
        {"-xf", pathToUtf8(archive), "-C", pathToUtf8(destination)}, {}, {}, std::chrono::minutes(10));
    if (extraction.exitCode != 0) {
        std::filesystem::remove_all(destination, ec);
        throw std::runtime_error("ZIP extraction failed: " + extraction.output);
    }
}

} // namespace pavm
