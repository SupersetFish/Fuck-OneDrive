#include "onedrive_system.h"

#include "common.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <knownfolders.h>
#include <shlobj_core.h>

#include <array>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kUserShellFoldersPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders";
constexpr wchar_t kShellFoldersPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders";

struct FolderSystemSpec {
    recovery::FolderKind kind;
    const KNOWNFOLDERID* id;
    const char* displayName;
    const wchar_t* localLeaf;
    const wchar_t* registryValueName;
};

constexpr FolderSystemSpec kFolderSpecs[] = {
    {recovery::FolderKind::Desktop, &FOLDERID_Desktop, "Desktop", L"Desktop", L"Desktop"},
    {recovery::FolderKind::Documents, &FOLDERID_Documents, "Documents", L"Documents", L"Personal"},
    {recovery::FolderKind::Pictures, &FOLDERID_Pictures, "Pictures", L"Pictures", L"My Pictures"},
    {recovery::FolderKind::Music, &FOLDERID_Music, "Music", L"Music", L"My Music"},
    {recovery::FolderKind::Videos, &FOLDERID_Videos, "Videos", L"Videos", L"My Video"},
};

std::wstring NormalizePathString(const fs::path& path) {
    std::wstring value = fs::absolute(path).lexically_normal().wstring();
    for (wchar_t& ch : value) {
        if (ch == L'/') {
            ch = L'\\';
        } else {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
    }
    while (value.size() > 3 && !value.empty() && value.back() == L'\\') {
        value.pop_back();
    }
    return value;
}

bool PathsEqual(const fs::path& left, const fs::path& right) {
    return NormalizePathString(left) == NormalizePathString(right);
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

std::optional<fs::path> GetExistingEnvironmentPath(const wchar_t* variableName) {
    const std::wstring value = GetEnvironmentVariableString(variableName);
    if (value.empty()) {
        return std::nullopt;
    }

    fs::path pathValue(value);
    std::error_code error;
    if (fs::exists(pathValue, error) && !error) {
        return pathValue;
    }
    return std::nullopt;
}

std::vector<fs::path> CollectOneDriveRoots() {
    std::vector<fs::path> roots;

    for (const wchar_t* variableName : {L"OneDriveConsumer", L"OneDriveCommercial", L"OneDrive"}) {
        if (const std::optional<fs::path> candidate = GetExistingEnvironmentPath(variableName)) {
            bool duplicate = false;
            for (const fs::path& existing : roots) {
                if (PathsEqual(existing, *candidate)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                roots.push_back(*candidate);
            }
        }
    }

    return roots;
}

std::optional<std::wstring> QueryRegistryString(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD valueType = 0;
    DWORD bytes = 0;
    const LSTATUS sizeStatus = RegQueryValueExW(key, valueName, nullptr, &valueType, nullptr, &bytes);
    if (sizeStatus != ERROR_SUCCESS || (valueType != REG_SZ && valueType != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    const LSTATUS readStatus = RegQueryValueExW(key, valueName, nullptr, &valueType, reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (readStatus != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return buffer;
}

bool SetRegistryString(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName, DWORD type, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(rootKey, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, valueName, 0, type, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::wstring GetKnownFolderPath(const KNOWNFOLDERID& id) {
    PWSTR rawPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, 0, nullptr, &rawPath);
    if (FAILED(hr)) {
        throw std::runtime_error("SHGetKnownFolderPath failed.");
    }

    std::wstring value(rawPath);
    CoTaskMemFree(rawPath);
    return value;
}

std::wstring ExpandEnvironmentStringValue(const std::wstring& value) {
    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }

    std::wstring expanded(required, L'\0');
    ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required);
    while (!expanded.empty() && expanded.back() == L'\0') {
        expanded.pop_back();
    }
    return expanded;
}

std::wstring ExtractExecutableFromCommand(const std::wstring& command) {
    if (command.empty()) {
        return L"";
    }

    if (command.front() == L'"') {
        const std::size_t endQuote = command.find(L'"', 1);
        if (endQuote != std::wstring::npos) {
            return command.substr(1, endQuote - 1);
        }
    }

    const std::size_t separator = command.find(L' ');
    return separator == std::wstring::npos ? command : command.substr(0, separator);
}

bool IsProcessRunning(const wchar_t* executableName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, executableName) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

std::string HexFromHresult(const HRESULT hr) {
    std::ostringstream builder;
    builder << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return builder.str();
}

json EnvironmentToJson(const recovery::OneDriveEnvironment& environment) {
    return json{
        {"rootPath", common::PathToUtf8(environment.rootPath)},
        {"executablePath", common::PathToUtf8(environment.executablePath)},
        {"startupCommand", environment.startupCommand},
        {"installed", environment.installed},
        {"processRunning", environment.processRunning},
        {"autoStartEnabled", environment.autoStartEnabled},
    };
}

json FolderRepairToJson(const recovery::KnownFolderRepairResult& result) {
    return json{
        {"kind", recovery::DescribeFolderKind(result.kind)},
        {"displayName", result.displayName},
        {"previousPath", common::PathToUtf8(result.previousPath)},
        {"targetPath", common::PathToUtf8(result.targetPath)},
        {"shellApiUpdated", result.shellApiUpdated},
        {"userShellUpdated", result.userShellUpdated},
        {"shellFoldersUpdated", result.shellFoldersUpdated},
        {"verified", result.verified},
        {"message", result.message},
    };
}

fs::path DetermineOneDriveExecutable() {
    const std::wstring startupCommand = QueryRegistryString(HKEY_CURRENT_USER, kRunKeyPath, L"OneDrive").value_or(L"");
    const fs::path fromStartup = ExpandEnvironmentStringValue(ExtractExecutableFromCommand(startupCommand));
    if (!fromStartup.empty()) {
        std::error_code error;
        if (fs::exists(fromStartup, error) && !error) {
            return fromStartup;
        }
    }

    const std::vector<fs::path> candidates = {
        fs::path(GetEnvironmentVariableString(L"LOCALAPPDATA")) / "Microsoft" / "OneDrive" / "OneDrive.exe",
        fs::path(GetEnvironmentVariableString(L"ProgramFiles")) / "Microsoft OneDrive" / "OneDrive.exe",
        fs::path(GetEnvironmentVariableString(L"ProgramFiles(x86)")) / "Microsoft OneDrive" / "OneDrive.exe",
    };

    for (const fs::path& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        std::error_code error;
        if (fs::exists(candidate, error) && !error) {
            return candidate;
        }
    }

    return {};
}

bool LaunchOneDriveShutdown(const fs::path& executablePath) {
    if (executablePath.empty()) {
        return false;
    }

    std::wstring commandLine = L"\"" + executablePath.wstring() + L"\" /shutdown";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        executablePath.parent_path().c_str(),
        &startupInfo,
        &processInfo);

    if (!created) {
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, 5'000);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (!IsProcessRunning(L"OneDrive.exe")) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return !IsProcessRunning(L"OneDrive.exe");
}

std::optional<recovery::FolderBinding> FindBinding(const recovery::ScanResult& scan, recovery::FolderKind kind) {
    for (const recovery::FolderBinding& folder : scan.folders) {
        if (folder.kind == kind) {
            return folder;
        }
    }
    return std::nullopt;
}

void WriteTextFile(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file.");
    }
    output << content;
}

}  // namespace

namespace recovery {

OneDriveEnvironment DetectOneDriveEnvironment() {
    OneDriveEnvironment environment;

    const std::vector<fs::path> roots = CollectOneDriveRoots();
    if (!roots.empty()) {
        environment.rootPath = roots.front();
    }

    if (const std::optional<std::wstring> startupCommand = QueryRegistryString(HKEY_CURRENT_USER, kRunKeyPath, L"OneDrive")) {
        environment.startupCommand = common::ToUtf8(*startupCommand);
        environment.autoStartEnabled = true;
    }

    environment.executablePath = DetermineOneDriveExecutable();
    environment.installed = !environment.executablePath.empty();
    environment.processRunning = IsProcessRunning(L"OneDrive.exe");

    return environment;
}

SystemRepairSummary RepairSystemState(const ScanResult& scan, const SystemRepairOptions& options) {
    SystemRepairSummary summary;
    summary.environmentBefore = DetectOneDriveEnvironment();

    if (options.stopOneDriveProcess) {
        summary.attemptedShutdown = true;
        if (!summary.environmentBefore.processRunning) {
            summary.shutdownSucceeded = true;
            summary.notes.push_back("OneDrive was not running.");
        } else if (LaunchOneDriveShutdown(summary.environmentBefore.executablePath)) {
            summary.shutdownSucceeded = true;
            summary.notes.push_back("Requested OneDrive to shut down.");
        } else {
            summary.notes.push_back("Failed to stop the running OneDrive process automatically.");
        }
    }

    if (options.disableAutoStart) {
        summary.attemptedDisableAutoStart = true;
        if (!summary.environmentBefore.autoStartEnabled) {
            summary.autoStartDisabled = true;
            summary.notes.push_back("OneDrive auto-start was already disabled for the current user.");
        } else {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
                const LSTATUS status = RegDeleteValueW(key, L"OneDrive");
                RegCloseKey(key);
                summary.autoStartDisabled = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
                summary.notes.push_back(summary.autoStartDisabled ? "Removed the current user's OneDrive auto-start entry."
                                                                  : "Failed to remove the current user's OneDrive auto-start entry.");
            } else {
                summary.notes.push_back("Failed to open the Run registry key for auto-start changes.");
            }
        }
    }

    if (options.restoreKnownFolders) {
        const fs::path userProfile = common::GetUserProfilePath();

        for (const FolderSystemSpec& spec : kFolderSpecs) {
            KnownFolderRepairResult folderResult;
            folderResult.kind = spec.kind;
            folderResult.displayName = spec.displayName;

            const std::optional<FolderBinding> binding = FindBinding(scan, spec.kind);
            folderResult.previousPath = binding.has_value() ? binding->currentPath : fs::path(GetKnownFolderPath(*spec.id));
            folderResult.targetPath = userProfile / spec.localLeaf;

            std::error_code directoryError;
            fs::create_directories(folderResult.targetPath, directoryError);

            const HRESULT hr = SHSetKnownFolderPath(*spec.id, 0, nullptr, folderResult.targetPath.c_str());
            folderResult.shellApiUpdated = SUCCEEDED(hr);
            if (!folderResult.shellApiUpdated) {
                folderResult.message = "SHSetKnownFolderPath failed with " + HexFromHresult(hr) + ".";
            }

            const std::wstring expandedValue = L"%USERPROFILE%\\" + std::wstring(spec.localLeaf);
            folderResult.userShellUpdated =
                SetRegistryString(HKEY_CURRENT_USER, kUserShellFoldersPath, spec.registryValueName, REG_EXPAND_SZ, expandedValue);
            folderResult.shellFoldersUpdated = SetRegistryString(
                HKEY_CURRENT_USER,
                kShellFoldersPath,
                spec.registryValueName,
                REG_SZ,
                folderResult.targetPath.wstring());

            try {
                const fs::path currentAfter = fs::path(GetKnownFolderPath(*spec.id));
                folderResult.verified = PathsEqual(currentAfter, folderResult.targetPath);
            } catch (const std::exception&) {
                folderResult.verified = false;
            }

            if (folderResult.verified) {
                folderResult.message = "Folder location now points to the local default path.";
            } else if (folderResult.message.empty()) {
                folderResult.message = "Registry fallback was written, but Windows may still need Explorer restart or sign-out.";
            } else {
                folderResult.message += " Registry fallback was still written.";
            }

            if (folderResult.shellApiUpdated || folderResult.userShellUpdated || folderResult.shellFoldersUpdated ||
                !PathsEqual(folderResult.previousPath, folderResult.targetPath)) {
                summary.explorerRestartRecommended = true;
            }

            summary.folderResults.push_back(std::move(folderResult));
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    summary.environmentAfter = DetectOneDriveEnvironment();

    const std::string timestamp = common::FormatNowForFilename();
    const fs::path outputDir = common::GetLocalAppDataPath() / "FuckOneDrive" / "system-actions";
    fs::create_directories(outputDir);
    summary.auditPath = outputDir / ("system-repair-" + timestamp + ".json");

    json folderResults = json::array();
    for (const KnownFolderRepairResult& folderResult : summary.folderResults) {
        folderResults.push_back(FolderRepairToJson(folderResult));
    }

    const json payload{
        {"createdAt", common::FormatNowForDisplay()},
        {"environmentBefore", EnvironmentToJson(summary.environmentBefore)},
        {"environmentAfter", EnvironmentToJson(summary.environmentAfter)},
        {"notes", summary.notes},
        {"attemptedShutdown", summary.attemptedShutdown},
        {"shutdownSucceeded", summary.shutdownSucceeded},
        {"attemptedDisableAutoStart", summary.attemptedDisableAutoStart},
        {"autoStartDisabled", summary.autoStartDisabled},
        {"explorerRestartRecommended", summary.explorerRestartRecommended},
        {"folderResults", folderResults},
    };

    WriteTextFile(summary.auditPath, payload.dump(2));
    return summary;
}

}  // namespace recovery
