#include "core/RepositoryParser.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace pavm {
namespace {

std::string regexEscape(const std::string& value) {
    static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(value, special, R"(\$&)");
}

std::string elementBlock(const std::string& block, const std::string& tag) {
    const std::string escaped = regexEscape(tag);
    const std::regex expression("<(?:[A-Za-z0-9_-]+:)?" + escaped +
                                "(?:\\s[^>]*)?>[\\s\\S]*?</(?:[A-Za-z0-9_-]+:)?" +
                                escaped + ">", std::regex::icase);
    std::smatch match;
    return std::regex_search(block, match, expression) ? match[0].str() : std::string{};
}

std::string elementText(const std::string& block, const std::string& tag) {
    const std::string full = elementBlock(block, tag);
    if (full.empty()) return {};
    const auto openEnd = full.find('>');
    const auto closeStart = full.rfind('<');
    if (openEnd == std::string::npos || closeStart == std::string::npos || closeStart <= openEnd) return {};
    std::string inner = full.substr(openEnd + 1, closeStart - openEnd - 1);
    inner = std::regex_replace(inner, std::regex(R"(<[^>]+>)"), "");
    return trim(xmlDecode(inner));
}

std::string openingTag(const std::string& block, const std::string& localName) {
    const std::regex expression("<(?:[A-Za-z0-9_-]+:)?" + regexEscape(localName) + R"(\b[^>]*>)",
                                std::regex::icase);
    std::smatch match;
    return std::regex_search(block, match, expression) ? match[0].str() : std::string{};
}

std::string attributeValue(const std::string& tag, const std::string& attribute) {
    const std::regex expression(regexEscape(attribute) + R"(\s*=\s*["']([^"']+)["'])",
                                std::regex::icase);
    std::smatch match;
    return std::regex_search(tag, match, expression) ? xmlDecode(match[1].str()) : std::string{};
}

std::vector<std::string> blocksFor(const std::string& xml, const std::string& name) {
    std::vector<std::string> blocks;
    const std::string escaped = regexEscape(name);
    const std::regex expression("<(?:[A-Za-z0-9_-]+:)?" + escaped +
                                R"(\b[^>]*>[\s\S]*?</(?:[A-Za-z0-9_-]+:)?)" + escaped + ">",
                                std::regex::icase);
    for (std::sregex_iterator it(xml.begin(), xml.end(), expression), end; it != end; ++it) {
        blocks.push_back(it->str());
    }
    return blocks;
}

int revisionPart(const std::string& revisionBlock, const char* name) {
    const auto parsed = parseInt(elementText(revisionBlock, name));
    return parsed.value_or(0);
}

std::string revisionText(const std::string& packageBlock) {
    const std::string revision = elementBlock(packageBlock, "revision");
    if (revision.empty()) return {};
    return std::to_string(revisionPart(revision, "major")) + "." +
           std::to_string(revisionPart(revision, "minor")) + "." +
           std::to_string(revisionPart(revision, "micro"));
}

} // namespace

RepositoryArchive findLatestCommandLineTools(const std::string& xml, const std::string& hostOsValue) {
    const std::string wantedHost = toLowerAscii(hostOsValue);
    for (const std::string& package : blocksFor(xml, "remotePackage")) {
        const std::string path = attributeValue(openingTag(package, "remotePackage"), "path");
        if (path != "cmdline-tools;latest") continue;
        for (const std::string& archive : blocksFor(package, "archive")) {
            const std::string host = toLowerAscii(elementText(archive, "host-os"));
            if (host != wantedHost) continue;
            const std::string complete = elementBlock(archive, "complete");
            const std::string url = elementText(complete, "url");
            const std::string sizeText = elementText(complete, "size");
            const std::string checksum = toLowerAscii(elementText(complete, "checksum"));
            if (url.empty() || sizeText.empty() || checksum.empty()) continue;
            std::uint64_t size = 0;
            try {
                size = std::stoull(sizeText);
            } catch (...) {
                continue;
            }
            RepositoryArchive found;
            found.packagePath = path;
            found.hostOs = host;
            found.relativeUrl = url;
            found.size = size;
            found.checksum = checksum;
            found.checksumType = toLowerAscii(attributeValue(openingTag(complete, "checksum"), "type"));
            if (found.checksumType.empty()) found.checksumType = checksum.size() == 40 ? "sha1" : "unknown";
            found.displayName = elementText(package, "display-name");
            found.revision = revisionText(package);
            return found;
        }
    }
    throw std::runtime_error("Official repository metadata does not contain cmdline-tools;latest for " + wantedHost);
}

std::vector<std::string> findSystemImagePackages(const std::string& output) {
    std::vector<std::string> result;
    const std::regex expression(R"((system-images;android-[0-9A-Za-z_.-]+;[0-9A-Za-z_.-]+;[0-9A-Za-z_.-]+))");
    for (std::sregex_iterator it(output.begin(), output.end(), expression), end; it != end; ++it) {
        const std::string value = (*it)[1].str();
        if (std::find(result.begin(), result.end(), value) == result.end()) result.push_back(value);
    }
    std::sort(result.begin(), result.end());
    return result;
}


std::vector<HardwareProfileInfo> parseHardwareProfiles(const std::string& output) {
    std::vector<HardwareProfileInfo> result;
    std::istringstream input(output);
    HardwareProfileInfo current;
    std::string line;
    auto flush = [&] {
        if (!current.id.empty()) result.push_back(current);
        current = {};
    };
    const std::regex idExpression(R"(^\s*id:\s*[0-9]+\s+or\s+\"([^\"]+)\"\s*$)", std::regex::icase);
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, idExpression)) {
            flush();
            current.id = trim(match[1].str());
            continue;
        }
        const std::string trimmed = trim(line);
        if (trimmed.rfind("Name:", 0) == 0) current.name = trim(trimmed.substr(5));
        else if (trimmed.rfind("OEM :", 0) == 0) current.oem = trim(trimmed.substr(5));
        else if (trimmed.rfind("Tag :", 0) == 0) current.tag = trim(trimmed.substr(5));
        else if (trimmed == "---------") flush();
    }
    flush();
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id == b.id; }), result.end());
    return result;
}

bool isPhoneHardwareProfile(const HardwareProfileInfo& profile) {
    const std::string text = toLowerAscii(profile.id + " " + profile.name + " " + profile.tag);
    static const char* excluded[] = {
        "tablet", "wear", "watch", "tv", "automotive", "auto_", "desktop", "chrome",
        "fold", "freeform", "resizable", "xr", "glass", "car"
    };
    for (const char* word : excluded) {
        if (text.find(word) != std::string::npos) return false;
    }
    return text.find("phone") != std::string::npos || text.find("pixel") != std::string::npos ||
           text.find("nexus") != std::string::npos;
}

std::string xmlDecode(std::string value) {
    value = replaceAll(std::move(value), "&amp;", "&");
    value = replaceAll(std::move(value), "&lt;", "<");
    value = replaceAll(std::move(value), "&gt;", ">");
    value = replaceAll(std::move(value), "&quot;", "\"");
    value = replaceAll(std::move(value), "&apos;", "'");
    return value;
}

} // namespace pavm
