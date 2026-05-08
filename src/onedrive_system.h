#pragma once

#include "onedrive_recovery.h"

#include <filesystem>
#include <string>
#include <vector>

namespace recovery {

struct OneDriveEnvironment {
    std::filesystem::path rootPath;
    std::filesystem::path executablePath;
    std::string startupCommand;
    bool installed = false;
    bool processRunning = false;
    bool autoStartEnabled = false;
};

struct KnownFolderRepairResult {
    FolderKind kind = FolderKind::Desktop;
    std::string displayName;
    std::filesystem::path previousPath;
    std::filesystem::path targetPath;
    bool shellApiUpdated = false;
    bool userShellUpdated = false;
    bool shellFoldersUpdated = false;
    bool verified = false;
    std::string message;
};

struct SystemRepairOptions {
    bool stopOneDriveProcess = true;
    bool disableAutoStart = true;
    bool restoreKnownFolders = true;
};

struct SystemRepairSummary {
    OneDriveEnvironment environmentBefore;
    OneDriveEnvironment environmentAfter;
    std::vector<KnownFolderRepairResult> folderResults;
    std::vector<std::string> notes;
    std::filesystem::path auditPath;
    bool attemptedShutdown = false;
    bool shutdownSucceeded = false;
    bool attemptedDisableAutoStart = false;
    bool autoStartDisabled = false;
    bool explorerRestartRecommended = false;
};

OneDriveEnvironment DetectOneDriveEnvironment();
SystemRepairSummary RepairSystemState(const ScanResult& scan, const SystemRepairOptions& options);

}  // namespace recovery
