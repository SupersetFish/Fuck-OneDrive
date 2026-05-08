#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace recovery {

enum class FolderKind {
    Desktop,
    Documents,
    Pictures,
    Music,
    Videos
};

enum class PlannedAction {
    CopyToLocal,
    CompareExistingLocal
};

enum class ExecutionDisposition {
    Pending,
    Copied,
    RenamedCopy,
    SkippedIdentical,
    Failed,
    Cancelled
};

struct FolderBinding {
    FolderKind kind = FolderKind::Desktop;
    std::string displayName;
    std::filesystem::path currentPath;
    std::filesystem::path localDefaultPath;
    std::filesystem::path sourcePath;
    bool redirectedToOneDrive = false;
    bool sourceExists = false;
};

struct PlanItem {
    std::string folderName;
    std::filesystem::path sourcePath;
    std::filesystem::path targetPath;
    std::filesystem::path relativePath;
    std::uint64_t bytes = 0;
    PlannedAction action = PlannedAction::CopyToLocal;
    bool targetExists = false;
    bool offline = false;
    bool placeholder = false;
    std::string note;
};

struct ScanResult {
    std::filesystem::path userProfilePath;
    std::filesystem::path oneDriveRoot;
    std::vector<FolderBinding> folders;
    std::vector<PlanItem> items;
    std::vector<std::string> warnings;
    std::uint64_t totalSourceBytes = 0;
    std::size_t redirectedFolderCount = 0;
    std::size_t offlineFileCount = 0;
};

struct ExecutionOptions {
    bool deleteSourceAfterVerify = false;
};

struct ProgressSnapshot {
    std::size_t processedItems = 0;
    std::size_t totalItems = 0;
    std::uint64_t processedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::string stage;
    std::string currentPath;
};

struct ExecutionItemResult {
    PlanItem plan;
    ExecutionDisposition disposition = ExecutionDisposition::Pending;
    std::filesystem::path actualTargetPath;
    bool verified = false;
    bool sourceDeleted = false;
    std::string sourceHash;
    std::string targetHash;
    std::string message;
};

struct ExecutionSummary {
    std::filesystem::path manifestPath;
    std::filesystem::path logPath;
    std::vector<ExecutionItemResult> itemResults;
    std::size_t copiedCount = 0;
    std::size_t renamedCount = 0;
    std::size_t skippedCount = 0;
    std::size_t failedCount = 0;
    std::size_t deletedSourceCount = 0;
    std::uint64_t verifiedBytes = 0;
    bool cancelled = false;
};

ScanResult BuildScanResult();
bool ExportScanToJson(const ScanResult& scan, const std::filesystem::path& outputPath, std::string& errorMessage);
ExecutionSummary ExecutePlan(
    const ScanResult& scan,
    const ExecutionOptions& options,
    std::atomic_bool& cancelRequested,
    const std::function<void(const ProgressSnapshot&)>& onProgress);

std::string DescribeFolderKind(FolderKind kind);
std::string DescribePlannedAction(PlannedAction action);
std::string DescribeDisposition(ExecutionDisposition disposition);

}  // namespace recovery
