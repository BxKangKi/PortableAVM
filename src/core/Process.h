#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pavm {

using ProcessEnvironment = std::map<std::string, std::string>;

struct ProcessResult {
    int exitCode = -1;
    bool timedOut = false;
    std::string output;
};

class ChildProcess {
public:
    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint64_t pid() const;
    [[nodiscard]] bool showMainWindow() const;
    void terminate();
    void reset();

private:
    struct Impl;
    explicit ChildProcess(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class ProcessRunner;
};

class ProcessRunner {
public:
    using OutputCallback = std::function<void(const std::string&)>;

    static ProcessResult run(const std::filesystem::path& executable,
                             const std::vector<std::string>& arguments,
                             const ProcessEnvironment& environment,
                             const std::string& standardInput = {},
                             std::chrono::milliseconds timeout = std::chrono::minutes(30),
                             OutputCallback callback = {});

    static ChildProcess spawn(const std::filesystem::path& executable,
                              const std::vector<std::string>& arguments,
                              const ProcessEnvironment& environment,
                              const std::filesystem::path& logFile = {});

    static bool launchInteractiveTerminal(const std::filesystem::path& executable,
                                          const std::vector<std::string>& arguments,
                                          const ProcessEnvironment& environment,
                                          const std::string& title);
};

} // namespace pavm
