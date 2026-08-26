#pragma once

#include <filesystem>
#include <string>

namespace pavm {

std::string sha1File(const std::filesystem::path& path);
std::string sha1Bytes(const void* data, std::size_t size);

} // namespace pavm
