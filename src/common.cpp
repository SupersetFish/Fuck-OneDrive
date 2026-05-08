#include "common.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

std::wstring NormalizePathString(const std::filesystem::path& path) {
    std::wstring value = std::filesystem::absolute(path).lexically_normal().wstring();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    while (value.size() > 3 && !value.empty() && (value.back() == L'\\' || value.back() == L'/')) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring GetEnvironmentVariableString(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return L"";
    }

    std::wstring buffer(required, L'\0');
    const DWORD actual = GetEnvironmentVariableW(name, buffer.data(), required);
    if (actual == 0) {
        return L"";
    }

    buffer.resize(actual);
    return buffer;
}

std::string FormatTime(const std::tm& timeValue, const char* pattern) {
    std::ostringstream builder;
    builder << std::put_time(&timeValue, pattern);
    return builder.str();
}

}  // namespace

namespace common {

std::wstring ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) {
        return L"";
    }

    std::wstring result(required, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), required);
    return result;
}

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return "";
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "";
    }

    std::string result(required, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return ToUtf8(path.wstring());
}

std::wstring ToLowerCopy(const std::wstring& value) {
    std::wstring lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return lowered;
}

bool PathStartsWith(const std::filesystem::path& candidate, const std::filesystem::path& parent) {
    const std::wstring candidateValue = NormalizePathString(candidate);
    const std::wstring parentValue = NormalizePathString(parent);

    if (candidateValue.size() < parentValue.size()) {
        return false;
    }

    if (candidateValue.compare(0, parentValue.size(), parentValue) != 0) {
        return false;
    }

    if (candidateValue.size() == parentValue.size()) {
        return true;
    }

    return candidateValue[parentValue.size()] == L'\\';
}

std::filesystem::path GetUserProfilePath() {
    const std::wstring value = GetEnvironmentVariableString(L"USERPROFILE");
    if (value.empty()) {
        throw std::runtime_error("USERPROFILE is not available.");
    }
    return std::filesystem::path(value);
}

std::filesystem::path GetLocalAppDataPath() {
    const std::wstring value = GetEnvironmentVariableString(L"LOCALAPPDATA");
    if (!value.empty()) {
        return std::filesystem::path(value);
    }
    return GetUserProfilePath() / "AppData" / "Local";
}

std::string FormatBytes(const std::uint64_t bytes) {
    constexpr std::array<const char*, 5> kUnits = {"B", "KB", "MB", "GB", "TB"};

    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (value >= 1024.0 && unitIndex + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unitIndex;
    }

    std::ostringstream builder;
    builder << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 2) << value << ' ' << kUnits[unitIndex];
    return builder.str();
}

std::string FormatNowForFilename() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &raw);
    return FormatTime(localTime, "%Y%m%d-%H%M%S");
}

std::string FormatNowForDisplay() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &raw);
    return FormatTime(localTime, "%Y-%m-%d %H:%M:%S");
}

std::filesystem::path MakeRecoveryCollisionPath(const std::filesystem::path& intendedPath, const std::string& timestamp) {
    const std::wstring suffixBase = L" (Recovered " + ToWide(timestamp) + L")";
    const std::wstring stem = intendedPath.stem().wstring();
    const std::wstring extension = intendedPath.extension().wstring();

    std::filesystem::path candidate = intendedPath.parent_path() / std::filesystem::path(stem + suffixBase + extension);
    if (!std::filesystem::exists(candidate)) {
        return candidate;
    }

    for (int counter = 2; counter < 10'000; ++counter) {
        const std::wstring numbered = suffixBase + L"-" + std::to_wstring(counter);
        candidate = intendedPath.parent_path() / std::filesystem::path(stem + numbered + extension);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("Failed to generate a unique recovery filename.");
}

}  // namespace common
