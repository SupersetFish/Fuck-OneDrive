#include "onedrive_recovery.h"

#include "common.h"
#include "sha256.h"

#include <Windows.h>
#include <knownfolders.h>
#include <shlobj_core.h>

#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct KnownFolderSpec {
    recovery::FolderKind kind;
    const KNOWNFOLDERID* id;
    const char* displayName;
    const wchar_t* defaultLeaf;
};

constexpr KnownFolderSpec kKnownFolders[] = {
    {recovery::FolderKind::Desktop, &FOLDERID_Desktop, "Desktop", L"Desktop"},
    {recovery::FolderKind::Documents, &FOLDERID_Documents, "Documents", L"Documents"},
    {recovery::FolderKind::Pictures, &FOLDERID_Pictures, "Pictures", L"Pictures"},
    {recovery::FolderKind::Music, &FOLDERID_Music, "Music", L"Music"},
    {recovery::FolderKind::Videos, &FOLDERID_Videos, "Videos", L"Videos"},
};

bool PathsEqual(const fs::path& left, const fs::path& right) {
    return common::PathStartsWith(left, right) && common::PathStartsWith(right, left);
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

std::optional<fs::path> GetExistingEnvironmentPath(const wchar_t* variableName) {
    const DWORD required = GetEnvironmentVariableW(variableName, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring buffer(required, L'\0');
    const DWORD actual = GetEnvironmentVariableW(variableName, buffer.data(), required);
    if (actual == 0) {
        return std::nullopt;
    }

    buffer.resize(actual);
    fs::path pathValue = fs::path(buffer);
    if (fs::exists(pathValue)) {
        return pathValue;
    }
    return std::nullopt;
}

std::vector<fs::path> CollectOneDriveRoots() {
    std::vector<fs::path> roots;

    for (const wchar_t* name : {L"OneDriveConsumer", L"OneDriveCommercial", L"OneDrive"}) {
        if (const std::optional<fs::path> value = GetExistingEnvironmentPath(name)) {
            bool duplicate = false;
            for (const fs::path& existing : roots) {
                if (common::PathStartsWith(*value, existing) && common::PathStartsWith(existing, *value)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                roots.push_back(*value);
            }
        }
    }

    return roots;
}

std::optional<fs::path> FindContainingRoot(const fs::path& path, const std::vector<fs::path>& roots) {
    for (const fs::path& root : roots) {
        if (common::PathStartsWith(path, root)) {
            return root;
        }
    }
    return std::nullopt;
}

DWORD GetAttributes(const fs::path& path) {
    return GetFileAttributesW(path.c_str());
}

bool IsDirectoryReparsePoint(const fs::path& path) {
    const DWORD attributes = GetAttributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool IsOfflineFile(const fs::path& path) {
    const DWORD attributes = GetAttributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_OFFLINE) != 0;
}

bool IsPlaceholderFile(const fs::path& path) {
    const DWORD attributes = GetAttributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_RECALL_ON_OPEN) != 0 || (attributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
}

json FolderToJson(const recovery::FolderBinding& folder) {
    return json{
        {"kind", recovery::DescribeFolderKind(folder.kind)},
        {"displayName", folder.displayName},
        {"currentPath", common::PathToUtf8(folder.currentPath)},
        {"localDefaultPath", common::PathToUtf8(folder.localDefaultPath)},
        {"sourcePath", common::PathToUtf8(folder.sourcePath)},
        {"redirectedToOneDrive", folder.redirectedToOneDrive},
        {"sourceExists", folder.sourceExists},
    };
}

json PlanToJson(const recovery::PlanItem& item) {
    return json{
        {"folderName", item.folderName},
        {"sourcePath", common::PathToUtf8(item.sourcePath)},
        {"targetPath", common::PathToUtf8(item.targetPath)},
        {"relativePath", common::PathToUtf8(item.relativePath)},
        {"bytes", item.bytes},
        {"action", recovery::DescribePlannedAction(item.action)},
        {"targetExists", item.targetExists},
        {"offline", item.offline},
        {"placeholder", item.placeholder},
        {"note", item.note},
    };
}

json ResultToJson(const recovery::ExecutionItemResult& result) {
    return json{
        {"plan", PlanToJson(result.plan)},
        {"disposition", recovery::DescribeDisposition(result.disposition)},
        {"actualTargetPath", common::PathToUtf8(result.actualTargetPath)},
        {"verified", result.verified},
        {"sourceDeleted", result.sourceDeleted},
        {"sourceHash", result.sourceHash},
        {"targetHash", result.targetHash},
        {"message", result.message},
    };
}

json ScanToJson(const recovery::ScanResult& scan) {
    json folders = json::array();
    for (const recovery::FolderBinding& folder : scan.folders) {
        folders.push_back(FolderToJson(folder));
    }

    json items = json::array();
    for (const recovery::PlanItem& item : scan.items) {
        items.push_back(PlanToJson(item));
    }

    return json{
        {"createdAt", common::FormatNowForDisplay()},
        {"userProfilePath", common::PathToUtf8(scan.userProfilePath)},
        {"oneDriveRoot", common::PathToUtf8(scan.oneDriveRoot)},
        {"redirectedFolderCount", scan.redirectedFolderCount},
        {"offlineFileCount", scan.offlineFileCount},
        {"totalSourceBytes", scan.totalSourceBytes},
        {"warnings", scan.warnings},
        {"folders", folders},
        {"items", items},
    };
}

void WriteTextFile(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file.");
    }
    output << content;
}

std::string BuildSummaryLog(const recovery::ScanResult& scan, const recovery::ExecutionSummary& summary, const recovery::ExecutionOptions& options) {
    std::ostringstream builder;
    builder << "Fuck OneDrive\n";
    builder << "Generated at: " << common::FormatNowForDisplay() << "\n";
    builder << "OneDrive root: " << common::PathToUtf8(scan.oneDriveRoot) << "\n";
    builder << "Delete source after verify: " << (options.deleteSourceAfterVerify ? "true" : "false") << "\n";
    builder << "Files scanned: " << scan.items.size() << "\n";
    builder << "Bytes scanned: " << scan.totalSourceBytes << "\n";
    builder << "Copied: " << summary.copiedCount << "\n";
    builder << "Renamed copies: " << summary.renamedCount << "\n";
    builder << "Skipped identical: " << summary.skippedCount << "\n";
    builder << "Failed: " << summary.failedCount << "\n";
    builder << "Deleted source: " << summary.deletedSourceCount << "\n";
    builder << "Cancelled: " << (summary.cancelled ? "true" : "false") << "\n\n";

    for (const recovery::ExecutionItemResult& result : summary.itemResults) {
        builder << recovery::DescribeDisposition(result.disposition) << " | " << common::PathToUtf8(result.plan.sourcePath)
                << " -> " << common::PathToUtf8(result.actualTargetPath.empty() ? result.plan.targetPath : result.actualTargetPath)
                << " | " << result.message << "\n";
    }

    return builder.str();
}

}  // namespace

namespace recovery {

std::string DescribeFolderKind(FolderKind kind) {
    switch (kind) {
        case FolderKind::Desktop:
            return "Desktop";
        case FolderKind::Documents:
            return "Documents";
        case FolderKind::Pictures:
            return "Pictures";
        case FolderKind::Music:
            return "Music";
        case FolderKind::Videos:
            return "Videos";
    }
    return "Unknown";
}

std::string DescribePlannedAction(PlannedAction action) {
    switch (action) {
        case PlannedAction::CopyToLocal:
            return "Copy to local path";
        case PlannedAction::CompareExistingLocal:
            return "Compare existing local file";
    }
    return "Unknown";
}

std::string DescribeDisposition(ExecutionDisposition disposition) {
    switch (disposition) {
        case ExecutionDisposition::Pending:
            return "Pending";
        case ExecutionDisposition::Copied:
            return "Copied";
        case ExecutionDisposition::RenamedCopy:
            return "Renamed copy";
        case ExecutionDisposition::SkippedIdentical:
            return "Skipped identical";
        case ExecutionDisposition::Failed:
            return "Failed";
        case ExecutionDisposition::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

ScanResult BuildScanResult() {
    ScanResult result;
    result.userProfilePath = common::GetUserProfilePath();

    const std::vector<fs::path> oneDriveRoots = CollectOneDriveRoots();
    if (oneDriveRoots.empty()) {
        result.warnings.push_back("No OneDrive root was found in the current environment.");
    }

    std::error_code error;

    for (const KnownFolderSpec& spec : kKnownFolders) {
        FolderBinding binding;
        binding.kind = spec.kind;
        binding.displayName = spec.displayName;
        binding.localDefaultPath = result.userProfilePath / spec.defaultLeaf;

        try {
            binding.currentPath = fs::path(GetKnownFolderPath(*spec.id));
        } catch (const std::exception&) {
            result.warnings.push_back("Failed to query the current path for " + binding.displayName + ".");
            continue;
        }

        std::optional<fs::path> sourceRoot;
        if (const std::optional<fs::path> currentRoot = FindContainingRoot(binding.currentPath, oneDriveRoots)) {
            binding.redirectedToOneDrive = true;
            binding.sourcePath = binding.currentPath;
            sourceRoot = currentRoot;
            ++result.redirectedFolderCount;
        } else {
            for (const fs::path& root : oneDriveRoots) {
                const fs::path candidate = root / spec.defaultLeaf;
                if (fs::exists(candidate, error) && !error) {
                    binding.sourcePath = candidate;
                    sourceRoot = root;
                    break;
                }
                error.clear();
            }
        }

        if (binding.sourcePath.empty()) {
            binding.sourcePath = binding.currentPath;
        }

        binding.sourceExists = fs::exists(binding.sourcePath, error) && !error;
        error.clear();

        if (binding.sourceExists && !binding.redirectedToOneDrive && PathsEqual(binding.sourcePath, binding.localDefaultPath)) {
            binding.sourceExists = false;
        }

        if (result.oneDriveRoot.empty() && sourceRoot.has_value()) {
            result.oneDriveRoot = *sourceRoot;
        }

        result.folders.push_back(binding);

        if (!binding.sourceExists) {
            continue;
        }

        if (PathsEqual(binding.sourcePath, binding.localDefaultPath)) {
            continue;
        }

        fs::recursive_directory_iterator iterator(binding.sourcePath, fs::directory_options::skip_permission_denied, error);
        fs::recursive_directory_iterator end;

        while (!error && iterator != end) {
            const fs::directory_entry entry = *iterator;

            if (entry.is_directory(error)) {
                if (!error && IsDirectoryReparsePoint(entry.path())) {
                    iterator.disable_recursion_pending();
                }
                error.clear();
                iterator.increment(error);
                continue;
            }

            if (error) {
                result.warnings.push_back("Failed to inspect directory entry under " + common::PathToUtf8(binding.sourcePath) + ".");
                error.clear();
                iterator.increment(error);
                continue;
            }

            if (!entry.is_regular_file(error)) {
                error.clear();
                iterator.increment(error);
                continue;
            }

            const fs::path relativePath = entry.path().lexically_relative(binding.sourcePath);
            if (relativePath.empty()) {
                iterator.increment(error);
                continue;
            }

            PlanItem item;
            item.folderName = binding.displayName;
            item.sourcePath = entry.path();
            item.relativePath = relativePath;
            item.targetPath = binding.localDefaultPath / relativePath;
            item.bytes = entry.file_size(error);
            error.clear();
            item.targetExists = fs::exists(item.targetPath, error) && !error;
            error.clear();
            item.action = item.targetExists ? PlannedAction::CompareExistingLocal : PlannedAction::CopyToLocal;
            item.offline = IsOfflineFile(item.sourcePath);
            item.placeholder = IsPlaceholderFile(item.sourcePath);

            if (item.targetExists) {
                item.note = "Local file already exists; execution will compare SHA-256 before deciding.";
            } else {
                item.note = "Safe copy to the local default path.";
            }

            if (item.offline || item.placeholder) {
                ++result.offlineFileCount;
                if (!item.note.empty()) {
                    item.note += " ";
                }
                item.note += "Source may still be cloud-backed and may need hydration.";
            }

            result.totalSourceBytes += item.bytes;
            result.items.push_back(std::move(item));
            iterator.increment(error);
        }

        if (error) {
            result.warnings.push_back("Stopped scanning " + binding.displayName + " because a filesystem error occurred.");
            error.clear();
        }
    }

    if (result.oneDriveRoot.empty() && !oneDriveRoots.empty()) {
        result.oneDriveRoot = oneDriveRoots.front();
    }

    if (result.redirectedFolderCount == 0) {
        result.warnings.push_back("No known folder is currently redirected into OneDrive. The plan only includes matching OneDrive mirror folders that still exist.");
    }

    if (result.offlineFileCount > 0) {
        result.warnings.push_back("Some files look like cloud placeholders or offline files. Those files may download during recovery or fail until they are available locally.");
    }

    return result;
}

bool ExportScanToJson(const ScanResult& scan, const fs::path& outputPath, std::string& errorMessage) {
    try {
        fs::create_directories(outputPath.parent_path());
        const json payload = ScanToJson(scan);
        WriteTextFile(outputPath, payload.dump(2));
        return true;
    } catch (const std::exception& ex) {
        errorMessage = ex.what();
        return false;
    }
}

ExecutionSummary ExecutePlan(
    const ScanResult& scan,
    const ExecutionOptions& options,
    std::atomic_bool& cancelRequested,
    const std::function<void(const ProgressSnapshot&)>& onProgress) {
    ExecutionSummary summary;
    summary.itemResults.reserve(scan.items.size());

    const std::string timestamp = common::FormatNowForFilename();
    const fs::path root = common::GetLocalAppDataPath() / "FuckOneDrive";
    const fs::path manifestDir = root / "manifests";
    const fs::path logDir = root / "logs";

    fs::create_directories(manifestDir);
    fs::create_directories(logDir);

    ProgressSnapshot progress;
    progress.totalItems = scan.items.size();
    progress.totalBytes = scan.totalSourceBytes;
    progress.stage = "Preparing";
    onProgress(progress);

    for (const PlanItem& item : scan.items) {
        if (cancelRequested.load()) {
            summary.cancelled = true;
            ExecutionItemResult cancelled;
            cancelled.plan = item;
            cancelled.disposition = ExecutionDisposition::Cancelled;
            cancelled.actualTargetPath = item.targetPath;
            cancelled.message = "Execution cancelled before this item was processed.";
            summary.itemResults.push_back(std::move(cancelled));
            continue;
        }

        ExecutionItemResult result;
        result.plan = item;
        result.actualTargetPath = item.targetPath;

        try {
            fs::create_directories(item.targetPath.parent_path());

            progress.stage = item.targetExists ? "Comparing" : "Copying";
            progress.currentPath = common::PathToUtf8(item.sourcePath);
            onProgress(progress);

            const std::string sourceHash = ComputeFileSha256(item.sourcePath);
            result.sourceHash = sourceHash;

            if (fs::exists(item.targetPath)) {
                const std::string targetHash = ComputeFileSha256(item.targetPath);
                result.targetHash = targetHash;

                if (sourceHash == targetHash) {
                    result.disposition = ExecutionDisposition::SkippedIdentical;
                    result.verified = true;
                    result.message = "Local file already matches the OneDrive copy.";
                    ++summary.skippedCount;
                } else {
                    const fs::path collisionPath = common::MakeRecoveryCollisionPath(item.targetPath, timestamp);
                    fs::copy_file(item.sourcePath, collisionPath, fs::copy_options::none);

                    result.actualTargetPath = collisionPath;
                    result.targetHash = ComputeFileSha256(collisionPath);
                    result.verified = result.targetHash == sourceHash;

                    if (!result.verified) {
                        result.disposition = ExecutionDisposition::Failed;
                        result.message = "Copied to a renamed path, but hash verification failed.";
                        ++summary.failedCount;
                    } else {
                        result.disposition = ExecutionDisposition::RenamedCopy;
                        result.message = "Copied to a renamed local file because the original local path already had different content.";
                        ++summary.renamedCount;
                        summary.verifiedBytes += item.bytes;
                    }
                }
            } else {
                fs::copy_file(item.sourcePath, item.targetPath, fs::copy_options::none);
                result.targetHash = ComputeFileSha256(item.targetPath);
                result.verified = result.targetHash == sourceHash;

                if (!result.verified) {
                    result.disposition = ExecutionDisposition::Failed;
                    result.message = "Hash verification failed after copying.";
                    ++summary.failedCount;
                } else {
                    result.disposition = ExecutionDisposition::Copied;
                    result.message = "Copied and verified.";
                    ++summary.copiedCount;
                    summary.verifiedBytes += item.bytes;
                }
            }

            if (options.deleteSourceAfterVerify && result.verified &&
                (result.disposition == ExecutionDisposition::Copied || result.disposition == ExecutionDisposition::RenamedCopy ||
                 result.disposition == ExecutionDisposition::SkippedIdentical)) {
                fs::remove(item.sourcePath);
                result.sourceDeleted = true;
                ++summary.deletedSourceCount;
                if (!result.message.empty()) {
                    result.message += " ";
                }
                result.message += "Source file deleted after verification.";
            }
        } catch (const std::exception& ex) {
            result.disposition = ExecutionDisposition::Failed;
            result.message = ex.what();
            ++summary.failedCount;
        }

        summary.itemResults.push_back(std::move(result));
        ++progress.processedItems;
        progress.processedBytes += item.bytes;
        onProgress(progress);
    }

    summary.manifestPath = manifestDir / ("recovery-" + timestamp + ".json");
    summary.logPath = logDir / ("recovery-" + timestamp + ".log");

    json manifest{
        {"createdAt", common::FormatNowForDisplay()},
        {"options", {{"deleteSourceAfterVerify", options.deleteSourceAfterVerify}}},
        {"scan", ScanToJson(scan)},
        {"summary",
         {{"copiedCount", summary.copiedCount},
          {"renamedCount", summary.renamedCount},
          {"skippedCount", summary.skippedCount},
          {"failedCount", summary.failedCount},
          {"deletedSourceCount", summary.deletedSourceCount},
          {"verifiedBytes", summary.verifiedBytes},
          {"cancelled", summary.cancelled}}},
    };

    json results = json::array();
    for (const ExecutionItemResult& result : summary.itemResults) {
        results.push_back(ResultToJson(result));
    }
    manifest["results"] = results;

    WriteTextFile(summary.manifestPath, manifest.dump(2));
    WriteTextFile(summary.logPath, BuildSummaryLog(scan, summary, options));

    progress.stage = summary.cancelled ? "Cancelled" : "Completed";
    progress.currentPath.clear();
    onProgress(progress);

    return summary;
}

}  // namespace recovery
