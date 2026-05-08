#pragma once

#include <filesystem>
#include <string>

namespace recovery {

std::string ComputeFileSha256(const std::filesystem::path& path);

}  // namespace recovery
