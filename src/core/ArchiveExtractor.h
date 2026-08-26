#pragma once

#include <filesystem>
#include <string>

namespace pavm {

class ArchiveExtractor {
public:
    static void extractZipSafely(const std::filesystem::path& archive,
                                 const std::filesystem::path& destination);
};

} // namespace pavm
