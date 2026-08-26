#include "core/HttpClient.h"
#include "core/StringUtil.h"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace pavm {

bool isAllowedGoogleRepositoryUrl(const std::string& value) {
    const std::string url = toLowerAscii(trim(value));
    constexpr std::string_view prefix = "https://dl.google.com/android/repository/";
    return startsWith(url, prefix) && url.find('@') == std::string::npos &&
           url.find("\\") == std::string::npos && url.find("..") == std::string::npos;
}

#ifdef _WIN32
namespace {

struct InternetHandle {
    HINTERNET value = nullptr;
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

UrlParts crackUrl(const std::string& value) {
    const std::wstring url = utf8ToWide(value);
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        throw std::runtime_error("Invalid URL");
    }
    UrlParts result;
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    result.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        result.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    result.port = parts.nPort;
    result.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!result.secure) {
        throw std::runtime_error("Only HTTPS downloads are permitted");
    }
    return result;
}

std::uint64_t contentLength(HINTERNET request) {
    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                             buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    try {
        return std::stoull(buffer);
    } catch (...) {
        return 0;
    }
}

template <typename Sink>
void request(const std::string& url, Sink sink, HttpClient::ProgressCallback progress) {
    const UrlParts parts = crackUrl(url);
    InternetHandle session(WinHttpOpen(L"PortableAVM/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) throw std::runtime_error("WinHttpOpen failed");
    WinHttpSetTimeouts(session.value, 15000, 15000, 30000, 30000);
    InternetHandle connection(WinHttpConnect(session.value, parts.host.c_str(), parts.port, 0));
    if (!connection.value) throw std::runtime_error("WinHttpConnect failed");
    InternetHandle req(WinHttpOpenRequest(connection.value, L"GET", parts.path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
    if (!req.value) throw std::runtime_error("WinHttpOpenRequest failed");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(req.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    if (!WinHttpSendRequest(req.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req.value, nullptr)) {
        throw std::runtime_error("HTTPS request failed with code " + std::to_string(GetLastError()));
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        throw std::runtime_error("HTTP status " + std::to_string(status));
    }
    const std::uint64_t total = contentLength(req.value);
    std::uint64_t downloaded = 0;
    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(req.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count)) {
            throw std::runtime_error("WinHttpReadData failed");
        }
        if (count == 0) break;
        sink(buffer.data(), static_cast<std::size_t>(count));
        downloaded += count;
        if (progress) progress(downloaded, total);
    }
}

} // namespace

std::string HttpClient::getText(const std::string& url, std::size_t maximumBytes) {
    std::string result;
    request(url, [&](const char* data, std::size_t size) {
        if (result.size() + size > maximumBytes) {
            throw std::runtime_error("HTTP response exceeds configured limit");
        }
        result.append(data, size);
    }, {});
    return result;
}

void HttpClient::downloadToFile(const std::string& url,
                                const std::filesystem::path& destination,
                                ProgressCallback progress) {
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.string() + ".part-" + randomToken();
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create download file");
    try {
        request(url, [&](const char* data, std::size_t size) {
            output.write(data, static_cast<std::streamsize>(size));
            if (!output) throw std::runtime_error("Download write failed");
        }, std::move(progress));
        output.close();
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(temporary, destination, ec);
        if (ec) throw std::runtime_error("Cannot finalize download: " + ec.message());
    } catch (...) {
        output.close();
        std::filesystem::remove(temporary);
        throw;
    }
}

#else
namespace {

struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }
    }
    ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& curlGlobal() {
    static CurlGlobal global;
    return global;
}

struct StringSink {
    std::string* value;
    std::size_t maximum;
};

size_t writeString(char* ptr, size_t size, size_t count, void* userdata) {
    const std::size_t bytes = size * count;
    auto* sink = static_cast<StringSink*>(userdata);
    if (sink->value->size() + bytes > sink->maximum) return 0;
    sink->value->append(ptr, bytes);
    return bytes;
}

size_t writeFile(char* ptr, size_t size, size_t count, void* userdata) {
    const std::size_t bytes = size * count;
    auto* output = static_cast<std::ofstream*>(userdata);
    output->write(ptr, static_cast<std::streamsize>(bytes));
    return *output ? bytes : 0;
}

struct ProgressData {
    HttpClient::ProgressCallback callback;
};

int progressCallback(void* client, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* data = static_cast<ProgressData*>(client);
    if (data->callback) {
        data->callback(static_cast<std::uint64_t>(now), static_cast<std::uint64_t>(total));
    }
    return 0;
}

void configure(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PortableAVM/0.3");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
}

} // namespace

std::string HttpClient::getText(const std::string& url, std::size_t maximumBytes) {
    curlGlobal();
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    std::string result;
    StringSink sink{&result, maximumBytes};
    configure(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) throw std::runtime_error(std::string("HTTPS request failed: ") + curl_easy_strerror(code));
    return result;
}

void HttpClient::downloadToFile(const std::string& url,
                                const std::filesystem::path& destination,
                                ProgressCallback progress) {
    curlGlobal();
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.string() + ".part-" + randomToken();
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create download file");
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    ProgressData progressData{std::move(progress)};
    configure(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressData);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    output.close();
    if (code != CURLE_OK) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(code));
    }
    std::error_code ec;
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot finalize download: " + ec.message());
    }
}
#endif

} // namespace pavm
