#pragma once

#include "onedrive_recovery.h"
#include "onedrive_system.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RecoveryApp {
  public:
    RecoveryApp();
    ~RecoveryApp();

    void Render();

  private:
    void OpenAuditFolder() const;
    void FinalizeWorkerIfNeeded();
    void RenderHero();
    void RenderLeftRail();
    void RenderRightRail();
    void RenderStatusCard();
    void RenderActionCard();
    void RenderRepairCard();
    void RenderFoldersCard();
    void RenderPlanCard();
    void RenderLogCard();
    void RunScan();
    void RunSystemRepair();
    void ExportCurrentPlan();
    void StartRecovery();
    void AppendLog(const std::string& line);

    recovery::OneDriveEnvironment environment_;
    recovery::ScanResult scan_;
    recovery::SystemRepairSummary lastRepairSummary_;
    bool hasEnvironment_ = false;
    bool hasScan_ = false;
    bool hasRepairSummary_ = false;
    bool stopOneDriveProcess_ = true;
    bool disableAutoStart_ = true;
    bool restoreKnownFolders_ = true;
    bool deleteSourceAfterVerify_ = false;

    std::vector<std::string> logLines_;
    std::string statusLine_ = "Click Scan to build a recovery plan.";
    std::filesystem::path lastPlanExportPath_;
    std::filesystem::path lastSystemAuditPath_;
    std::filesystem::path lastManifestPath_;
    std::filesystem::path lastLogPath_;

    std::thread worker_;
    std::atomic_bool workerRunning_ = false;
    std::atomic_bool cancelRequested_ = false;
    recovery::ProgressSnapshot progress_;
    recovery::ExecutionSummary lastSummary_;

    std::mutex stateMutex_;
};
