#include "platform/NativeDialogs.h"
#include "core/Process.h"
#include "core/StringUtil.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>

namespace pavm {
namespace {

std::optional<std::filesystem::path> dialog(bool folder, const std::string& title,
                                            const std::filesystem::path& initial, bool zipOnly = false) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                IID_IFileOpenDialog, reinterpret_cast<void**>(&picker)))) {
        if (SUCCEEDED(init)) CoUninitialize();
        return std::nullopt;
    }
    DWORD options = 0;
    picker->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) options |= FOS_PICKFOLDERS;
    else options |= FOS_FILEMUSTEXIST;
    picker->SetOptions(options);
    picker->SetTitle(utf8ToWide(title).c_str());
    COMDLG_FILTERSPEC apkFilter[] = {{L"Android package (*.apk)", L"*.apk"}, {L"All files", L"*.*"}};
    COMDLG_FILTERSPEC zipFilter[] = {{L"ZIP archive (*.zip)", L"*.zip"}, {L"All files", L"*.*"}};
    if (!folder) picker->SetFileTypes(2, zipOnly ? zipFilter : apkFilter);
    if (!initial.empty() && std::filesystem::exists(initial)) {
        IShellItem* item = nullptr;
        const auto initialFolder = std::filesystem::is_directory(initial) ? initial : initial.parent_path();
        if (SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), nullptr, IID_IShellItem,
                                                   reinterpret_cast<void**>(&item)))) {
            picker->SetFolder(item);
            item->Release();
        }
    }
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(picker->Show(nullptr))) {
        IShellItem* selected = nullptr;
        if (SUCCEEDED(picker->GetResult(&selected))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            selected->Release();
        }
    }
    picker->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
}

} // namespace

std::optional<std::filesystem::path> selectApkFile(const std::filesystem::path& initial) {
    return dialog(false, "APK 선택", initial);
}
std::optional<std::filesystem::path> selectZipFile(const std::string& title,
                                                   const std::filesystem::path& initial) {
    return dialog(false, title, initial, true);
}
std::optional<std::filesystem::path> selectDirectory(const std::string& title,
                                                     const std::filesystem::path& initial) {
    return dialog(true, title, initial);
}

} // namespace pavm

#else
namespace pavm {
namespace {

std::optional<std::filesystem::path> zenity(const std::vector<std::string>& args) {
    const std::filesystem::path executable = "/usr/bin/zenity";
    if (!std::filesystem::is_regular_file(executable)) return std::nullopt;
    const auto result = ProcessRunner::run(executable, args, {}, {}, std::chrono::minutes(5));
    if (result.exitCode != 0) return std::nullopt;
    const std::string path = trim(result.output);
    return path.empty() ? std::nullopt : std::optional<std::filesystem::path>(pathFromUtf8(path));
}

} // namespace

std::optional<std::filesystem::path> selectApkFile(const std::filesystem::path& initial) {
    std::vector<std::string> args{"--file-selection", "--title=APK 선택", "--file-filter=Android package | *.apk"};
    if (!initial.empty()) args.push_back("--filename=" + pathToUtf8(initial));
    return zenity(args);
}
std::optional<std::filesystem::path> selectZipFile(const std::string& title,
                                                   const std::filesystem::path& initial) {
    std::vector<std::string> args{"--file-selection", "--title=" + title, "--file-filter=ZIP archive | *.zip"};
    if (!initial.empty()) args.push_back("--filename=" + pathToUtf8(initial));
    return zenity(args);
}
std::optional<std::filesystem::path> selectDirectory(const std::string& title,
                                                     const std::filesystem::path& initial) {
    std::vector<std::string> args{"--file-selection", "--directory", "--title=" + title};
    if (!initial.empty()) args.push_back("--filename=" + pathToUtf8(initial) + "/");
    return zenity(args);
}

} // namespace pavm
#endif
