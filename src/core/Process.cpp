#include "core/Process.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pavm {

struct ChildProcess::Impl {
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD processId = 0;
#else
    pid_t processId = -1;
#endif
};

ChildProcess::ChildProcess() = default;
ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ChildProcess::~ChildProcess() { reset(); }
ChildProcess::ChildProcess(ChildProcess&& other) noexcept = default;
ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        reset();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool ChildProcess::valid() const {
#ifdef _WIN32
    return impl_ && impl_->process != nullptr;
#else
    return impl_ && impl_->processId > 0;
#endif
}

bool ChildProcess::running() const {
    if (!valid()) {
        return false;
    }
#ifdef _WIN32
    if (impl_->job != nullptr) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (QueryInformationJobObject(impl_->job, JobObjectBasicAccountingInformation,
                                      &accounting, sizeof(accounting), nullptr)) {
            return accounting.ActiveProcesses > 0;
        }
    }
    return WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
#else
    const int result = kill(impl_->processId, 0);
    return result == 0 || errno == EPERM;
#endif
}

std::uint64_t ChildProcess::pid() const {
    if (!valid()) {
        return 0;
    }
#ifdef _WIN32
    return static_cast<std::uint64_t>(impl_->processId);
#else
    return static_cast<std::uint64_t>(impl_->processId);
#endif
}

bool ChildProcess::showMainWindow() const {
    if (!valid() || !running()) return false;
#ifdef _WIN32
    std::vector<DWORD> processIds;
    if (impl_->job != nullptr) {
        DWORD capacity = 32;
        for (int attempt = 0; attempt < 5; ++attempt) {
            const std::size_t bytes = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
                                      sizeof(ULONG_PTR) * static_cast<std::size_t>(capacity - 1);
            std::vector<unsigned char> buffer(bytes);
            auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
            if (QueryInformationJobObject(impl_->job, JobObjectBasicProcessIdList,
                                          list, static_cast<DWORD>(buffer.size()), nullptr)) {
                processIds.reserve(list->NumberOfProcessIdsInList);
                for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
                    processIds.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
                }
                break;
            }
            if (GetLastError() != ERROR_MORE_DATA) break;
            capacity *= 2;
        }
    }
    if (processIds.empty()) processIds.push_back(impl_->processId);

    struct WindowSearch {
        const std::vector<DWORD>* processIds = nullptr;
        HWND window = nullptr;
    } search{&processIds, nullptr};
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (std::find(search->processIds->begin(), search->processIds->end(), pid) == search->processIds->end()) {
            return TRUE;
        }
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        wchar_t className[128]{};
        GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
        // If emulator.exe or one of its children creates a console window, suppress
        // only that console. The Android Emulator Qt/QEMU top-level window is kept.
        if (_wcsicmp(className, L"ConsoleWindowClass") == 0 ||
            _wcsicmp(className, L"PseudoConsoleWindow") == 0) {
            ShowWindowAsync(hwnd, SW_HIDE);
            return TRUE;
        }
        if (GetWindowTextLengthW(hwnd) <= 0) return TRUE;
        search->window = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    if (search.window == nullptr) return false;
    ShowWindowAsync(search.window, IsIconic(search.window) ? SW_RESTORE : SW_SHOW);
    BringWindowToTop(search.window);
    SetForegroundWindow(search.window);
    return true;
#else
    return true;
#endif
}

void ChildProcess::terminate() {
    if (!valid()) {
        return;
    }
#ifdef _WIN32
    if (running()) {
        if (impl_->job != nullptr) {
            TerminateJobObject(impl_->job, 1);
        } else {
            TerminateProcess(impl_->process, 1);
        }
        WaitForSingleObject(impl_->process, 5000);
    }
#else
    if (running()) {
        kill(-impl_->processId, SIGTERM);
        for (int i = 0; i < 30 && running(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (running()) {
            kill(-impl_->processId, SIGKILL);
        }
        int status = 0;
        waitpid(impl_->processId, &status, WNOHANG);
    }
#endif
}

void ChildProcess::reset() {
    if (!impl_) {
        return;
    }
#ifdef _WIN32
    if (impl_->process != nullptr) {
        CloseHandle(impl_->process);
    }
    if (impl_->job != nullptr) {
        CloseHandle(impl_->job);
    }
#else
    if (impl_->processId > 0) {
        int status = 0;
        waitpid(impl_->processId, &status, WNOHANG);
    }
#endif
    impl_.reset();
}

#ifdef _WIN32
namespace {

struct CaseInsensitiveWideLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(c);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring commandLineFor(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            bool& useCmd) {
    const std::wstring extension = executable.extension().wstring();
    useCmd = _wcsicmp(extension.c_str(), L".bat") == 0 || _wcsicmp(extension.c_str(), L".cmd") == 0;
    std::wstring inner = quoteWindowsArgument(executable.wstring());
    for (const std::string& argument : arguments) {
        inner.push_back(L' ');
        inner += quoteWindowsArgument(utf8ToWide(argument));
    }
    if (!useCmd) {
        return inner;
    }
    // /S /C requires a second quote around a command that starts with a quoted path.
    return L"cmd.exe /D /S /C \"" + inner + L"\"";
}

std::vector<wchar_t> environmentBlock(const ProcessEnvironment& overrides) {
    std::map<std::wstring, std::wstring, CaseInsensitiveWideLess> values;
    LPWCH block = GetEnvironmentStringsW();
    if (block != nullptr) {
        for (const wchar_t* current = block; *current != L'\0'; current += wcslen(current) + 1) {
            const std::wstring entry(current);
            const std::size_t equal = entry.find(L'=', 1);
            if (equal != std::wstring::npos) {
                values[entry.substr(0, equal)] = entry.substr(equal + 1);
            }
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [key, value] : overrides) {
        const std::wstring wideKey = utf8ToWide(key);
        if (value.empty()) {
            values.erase(wideKey);
        } else {
            values[wideKey] = utf8ToWide(value);
        }
    }
    std::vector<wchar_t> result;
    for (const auto& [key, value] : values) {
        const std::wstring entry = key + L"=" + value;
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

std::filesystem::path cmdPath() {
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"cmd.exe";
    }
    return std::filesystem::path(systemDirectory) / L"cmd.exe";
}

} // namespace

ProcessResult ProcessRunner::run(const std::filesystem::path& executable,
                                 const std::vector<std::string>& arguments,
                                 const ProcessEnvironment& environment,
                                 const std::string& standardInput,
                                 std::chrono::milliseconds timeout,
                                 OutputCallback callback) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stdinRead, &stdinWrite, &security, 0) ||
        !SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }

    bool useCmd = false;
    std::wstring commandLine = commandLineFor(executable, arguments, useCmd);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    auto envBlock = environmentBlock(environment);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stdoutWrite;
    PROCESS_INFORMATION process{};
    const std::filesystem::path application = useCmd ? cmdPath() : executable;
    const BOOL created = CreateProcessW(application.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        envBlock.data(), nullptr, &startup, &process);
    CloseHandle(stdoutWrite);
    CloseHandle(stdinRead);
    if (!created) {
        CloseHandle(stdoutRead);
        CloseHandle(stdinWrite);
        throw std::runtime_error("CreateProcessW failed with code " + std::to_string(GetLastError()));
    }
    CloseHandle(process.hThread);

    if (!standardInput.empty()) {
        DWORD written = 0;
        WriteFile(stdinWrite, standardInput.data(), static_cast<DWORD>(standardInput.size()), &written, nullptr);
    }
    CloseHandle(stdinWrite);

    ProcessResult result;
    std::mutex outputMutex;
    std::thread reader([&] {
        std::array<char, 8192> buffer{};
        DWORD count = 0;
        while (ReadFile(stdoutRead, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count > 0) {
            std::string chunk(buffer.data(), buffer.data() + count);
            {
                std::scoped_lock lock(outputMutex);
                result.output += chunk;
            }
            if (callback) {
                callback(chunk);
            }
        }
    });

    const DWORD waitMs = timeout.count() > static_cast<long long>(INFINITE - 1)
                             ? INFINITE - 1
                             : static_cast<DWORD>(timeout.count());
    const DWORD wait = WaitForSingleObject(process.hProcess, waitMs);
    if (wait == WAIT_TIMEOUT) {
        result.timedOut = true;
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);
    CloseHandle(process.hProcess);
    if (reader.joinable()) {
        reader.join();
    }
    CloseHandle(stdoutRead);
    return result;
}

ChildProcess ProcessRunner::spawn(const std::filesystem::path& executable,
                                  const std::vector<std::string>& arguments,
                                  const ProcessEnvironment& environment,
                                  const std::filesystem::path& logFile) {
    bool useCmd = false;
    std::wstring commandLine = commandLineFor(executable, arguments, useCmd);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    auto envBlock = environmentBlock(environment);

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE log = INVALID_HANDLE_VALUE;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    if (!logFile.empty()) {
        std::filesystem::create_directories(logFile.parent_path());
        log = CreateFileW(logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &inheritable, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE || nullInput == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
            if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
            throw std::runtime_error("Failed to prepare emulator log handles with code " + std::to_string(error));
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    if (log != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = nullInput;
        startup.hStdOutput = log;
        startup.hStdError = log;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
        throw std::runtime_error("CreateJobObjectW failed with code " + std::to_string(GetLastError()));
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
        throw std::runtime_error("SetInformationJobObject failed with code " + std::to_string(error));
    }

    PROCESS_INFORMATION process{};
    const std::filesystem::path application = useCmd ? cmdPath() : executable;
    // emulator.exe is a console-subsystem launcher, but PortableAVM is a GUI launcher.
    // CREATE_NO_WINDOW suppresses only the console allocation. The Android Emulator
    // Qt/QEMU GUI is a normal top-level window and is discovered/restored separately.
    const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW;
    const std::filesystem::path workingDirectory = executable.parent_path();
    const BOOL created = CreateProcessW(application.c_str(), mutableCommand.data(), nullptr, nullptr,
                                        log != INVALID_HANDLE_VALUE, creationFlags,
                                        envBlock.data(), workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                        &startup, &process);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        throw std::runtime_error("CreateProcessW failed with code " + std::to_string(error));
    }

    if (!AssignProcessToJobObject(job, process.hProcess)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        throw std::runtime_error("AssignProcessToJobObject failed with code " + std::to_string(error));
    }

    auto impl = std::make_unique<ChildProcess::Impl>();
    impl->process = process.hProcess;
    impl->processId = process.dwProcessId;
    impl->job = job;

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateJobObject(impl->job, 1);
        CloseHandle(process.hThread);
        CloseHandle(impl->job);
        CloseHandle(impl->process);
        impl->job = nullptr;
        impl->process = nullptr;
        throw std::runtime_error("ResumeThread failed with code " + std::to_string(error));
    }
    CloseHandle(process.hThread);
    return ChildProcess(std::move(impl));
}

bool ProcessRunner::launchInteractiveTerminal(const std::filesystem::path& executable,
                                              const std::vector<std::string>& arguments,
                                              const ProcessEnvironment& environment,
                                              const std::string& title) {
    bool ignored = false;
    std::wstring command = commandLineFor(executable, arguments, ignored);
    std::wstring line = L"cmd.exe /D /C \"title " + utf8ToWide(title) + L" & " + command + L"\"";
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    auto envBlock = environmentBlock(environment);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(cmdPath().c_str(), mutableLine.data(), nullptr, nullptr, FALSE,
                                        CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                                        envBlock.data(), nullptr, &startup, &process);
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

#else
namespace {

void applyEnvironment(const ProcessEnvironment& environment) {
    for (const auto& [key, value] : environment) {
        if (value.empty()) unsetenv(key.c_str());
        else setenv(key.c_str(), value.c_str(), 1);
    }
}

std::vector<char*> argvFor(const std::filesystem::path& executable,
                           const std::vector<std::string>& arguments,
                           std::vector<std::string>& storage) {
    storage.clear();
    storage.push_back(executable.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (std::string& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);
    return argv;
}

} // namespace

ProcessResult ProcessRunner::run(const std::filesystem::path& executable,
                                 const std::vector<std::string>& arguments,
                                 const ProcessEnvironment& environment,
                                 const std::string& standardInput,
                                 std::chrono::milliseconds timeout,
                                 OutputCallback callback) {
    int stdoutPipe[2]{};
    int stdinPipe[2]{};
    if (pipe(stdoutPipe) != 0 || pipe(stdinPipe) != 0) {
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
    }
    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);
        close(stdinPipe[0]); close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        applyEnvironment(environment);
        std::vector<std::string> storage;
        auto argv = argvFor(executable, arguments, storage);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    if (!standardInput.empty()) {
        const char* data = standardInput.data();
        std::size_t remaining = standardInput.size();
        while (remaining > 0) {
            const ssize_t written = write(stdinPipe[1], data, remaining);
            if (written <= 0) break;
            data += written;
            remaining -= static_cast<std::size_t>(written);
        }
    }
    close(stdinPipe[1]);

    ProcessResult result;
    std::mutex outputMutex;
    std::thread reader([&] {
        std::array<char, 8192> buffer{};
        for (;;) {
            const ssize_t count = read(stdoutPipe[0], buffer.data(), buffer.size());
            if (count <= 0) break;
            std::string chunk(buffer.data(), static_cast<std::size_t>(count));
            {
                std::scoped_lock lock(outputMutex);
                result.output += chunk;
            }
            if (callback) callback(chunk);
        }
    });

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    close(stdoutPipe[0]);
    if (reader.joinable()) reader.join();
    if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
    return result;
}

ChildProcess ProcessRunner::spawn(const std::filesystem::path& executable,
                                  const std::vector<std::string>& arguments,
                                  const ProcessEnvironment& environment,
                                  const std::filesystem::path& logFile) {
    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid == 0) {
        setpgid(0, 0);
        int output = open("/dev/null", O_WRONLY);
        if (!logFile.empty()) {
            std::filesystem::create_directories(logFile.parent_path());
            output = open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        }
        const int input = open("/dev/null", O_RDONLY);
        if (input >= 0) dup2(input, STDIN_FILENO);
        if (output >= 0) {
            dup2(output, STDOUT_FILENO);
            dup2(output, STDERR_FILENO);
        }
        applyEnvironment(environment);
        std::vector<std::string> storage;
        auto argv = argvFor(executable, arguments, storage);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    auto impl = std::make_unique<ChildProcess::Impl>();
    impl->processId = pid;
    return ChildProcess(std::move(impl));
}

bool ProcessRunner::launchInteractiveTerminal(const std::filesystem::path& executable,
                                              const std::vector<std::string>& arguments,
                                              const ProcessEnvironment& environment,
                                              const std::string& title) {
    const char* terminals[] = {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"};
    for (const char* terminal : terminals) {
        const std::string shell = "exec \"$0\" \"$@\"; printf '\\nPress Enter to close...'; read _";
        std::vector<std::string> terminalArgs;
        if (std::string(terminal) == "gnome-terminal") {
            terminalArgs = {"--title", title, "--", "sh", "-c", shell,
                            executable.string()};
        } else if (std::string(terminal) == "konsole") {
            terminalArgs = {"--new-tab", "-p", "tabtitle=" + title, "-e", "sh", "-c", shell,
                            executable.string()};
        } else {
            terminalArgs = {"-T", title, "-e", "sh", "-c", shell, executable.string()};
        }
        terminalArgs.insert(terminalArgs.end(), arguments.begin(), arguments.end());
        const pid_t pid = fork();
        if (pid == 0) {
            applyEnvironment(environment);
            std::vector<std::string> storage;
            auto argv = argvFor(terminal, terminalArgs, storage);
            execvp(terminal, argv.data());
            _exit(127);
        }
        if (pid > 0) {
            return true;
        }
    }
    return false;
}
#endif

} // namespace pavm
