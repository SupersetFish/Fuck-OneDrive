#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace common {

std::wstring ToWide(std::string_view utf8);
std::string ToUtf8(const std::wstring& wide);
std::string PathToUtf8(const std::filesystem::path& path);
std::wstring ToLowerCopy(const std::wstring& value);
bool PathStartsWith(const std::filesystem::path& candidate, const std::filesystem::path& parent);
std::filesystem::path GetUserProfilePath();
std::filesystem::path GetLocalAppDataPath();
std::string FormatBytes(std::uint64_t bytes);
std::string FormatNowForFilename();
std::string FormatNowForDisplay();
std::filesystem::path MakeRecoveryCollisionPath(const std::filesystem::path& intendedPath, const std::string& timestamp);

}  // namespace common
