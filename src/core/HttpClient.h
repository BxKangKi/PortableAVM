#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace pavm {

class HttpClient {
public:
    using ProgressCallback = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;

    static std::string getText(const std::string& url, std::size_t maximumBytes = 32U * 1024U * 1024U);
    static void downloadToFile(const std::string& url,
                               const std::filesystem::path& destination,
                               ProgressCallback progress = {});
};

bool isAllowedGoogleRepositoryUrl(const std::string& url);

} // namespace pavm
