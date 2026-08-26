#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pavm {

std::optional<std::filesystem::path> selectApkFile(const std::filesystem::path& initial = {});
std::optional<std::filesystem::path> selectZipFile(const std::string& title,
                                                   const std::filesystem::path& initial = {});
std::optional<std::filesystem::path> selectDirectory(const std::string& title,
                                                     const std::filesystem::path& initial = {});

} // namespace pavm
