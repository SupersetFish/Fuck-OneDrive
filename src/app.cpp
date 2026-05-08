#include "app.h"

#include "common.h"

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <algorithm>
#include <sstream>

namespace {

ImVec4 HeroColor() {
    return ImVec4(0.09f, 0.26f, 0.42f, 1.0f);
}

ImVec4 AccentColor() {
    return ImVec4(0.78f, 0.31f, 0.15f, 1.0f);
}

ImVec4 GoodColor() {
    return ImVec4(0.15f, 0.53f, 0.28f, 1.0f);
}

ImVec4 WarnColor() {
    return ImVec4(0.72f, 0.28f, 0.14f, 1.0f);
}

ImVec4 SoftTextColor() {
    return ImVec4(0.35f, 0.39f, 0.42f, 1.0f);
}

void BeginCard(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), ImGuiWindowFlags flags = ImGuiWindowFlags_None) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.99f, 0.985f, 0.975f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.84f, 0.81f, 0.76f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::BeginChild(id, size, true, flags);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void SectionHeader(const char* title, const char* subtitle = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, HeroColor());
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (subtitle != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
}

void MetricLine(const char* label, const std::string& value, const ImVec4* valueColor = nullptr) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(170.0f);
    if (valueColor != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, *valueColor);
    }
    ImGui::TextWrapped("%s", value.c_str());
    if (valueColor != nullptr) {
        ImGui::PopStyleColor();
    }
}

std::string YesNo(bool value) {
    return value ? "Yes" : "No";
}

std::string BuildSummaryLine(const recovery::ExecutionSummary& summary) {
    std::ostringstream builder;
    builder << "Copied " << summary.copiedCount << ", renamed " << summary.renamedCount << ", skipped " << summary.skippedCount
            << ", failed " << summary.failedCount;
    if (summary.deletedSourceCount > 0) {
        builder << ", deleted source " << summary.deletedSourceCount;
    }
    if (summary.cancelled) {
        builder << " (cancelled)";
    }
    return builder.str();
}

std::string BuildHealthLabel(const recovery::ScanResult& scan, const recovery::OneDriveEnvironment& environment) {
    if (scan.redirectedFolderCount > 0 && environment.processRunning && environment.autoStartEnabled) {
        return "Captured by OneDrive";
    }
    if (scan.redirectedFolderCount > 0) {
        return "Partially detached";
    }
    if (!environment.processRunning && !environment.autoStartEnabled) {
        return "Local-first";
    }
    return "Mostly local";
}

ImVec4 BuildHealthColor(const recovery::ScanResult& scan, const recovery::OneDriveEnvironment& environment) {
    if (scan.redirectedFolderCount > 0 && environment.processRunning && environment.autoStartEnabled) {
        return WarnColor();
    }
    if (scan.redirectedFolderCount > 0) {
        return AccentColor();
    }
    return GoodColor();
}

std::string BuildRepairSummary(const recovery::SystemRepairSummary& summary) {
    std::ostringstream builder;
    if (summary.attemptedShutdown) {
        builder << (summary.shutdownSucceeded ? "Stopped" : "Did not stop") << " OneDrive. ";
    }
    if (summary.attemptedDisableAutoStart) {
        builder << (summary.autoStartDisabled ? "Auto-start disabled. " : "Auto-start unchanged. ");
    }
    if (!summary.folderResults.empty()) {
        std::size_t verifiedCount = 0;
        for (const recovery::KnownFolderRepairResult& result : summary.folderResults) {
            if (result.verified) {
                ++verifiedCount;
            }
        }
        builder << verifiedCount << " / " << summary.folderResults.size() << " known folders now verify as local.";
    }
    if (summary.explorerRestartRecommended) {
        builder << " Explorer restart or sign-out may still be needed.";
    }
    return builder.str();
}

bool OpenPathInExplorer(const std::filesystem::path& path) {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(result) > 32;
}

}  // namespace

RecoveryApp::RecoveryApp() = default;

RecoveryApp::~RecoveryApp() {
    cancelRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void RecoveryApp::Render() {
    FinalizeWorkerIfNeeded();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
    ImGui::Begin("LiberationRoot", nullptr, rootFlags);

    RenderHero();
    ImGui::Spacing();

    const float logHeight = 240.0f;
    const float topAreaHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y - logHeight - 12.0f);

    if (ImGui::BeginTable("dashboard-layout", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, topAreaHeight))) {
        ImGui::TableSetupColumn("LeftRail", ImGuiTableColumnFlags_WidthFixed, 430.0f);
        ImGui::TableSetupColumn("RightRail", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        RenderLeftRail();
        ImGui::TableNextColumn();
        RenderRightRail();
        ImGui::EndTable();
    }

    ImGui::Spacing();
    RenderLogCard();

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void RecoveryApp::OpenAuditFolder() const {
    const std::filesystem::path auditRoot = common::GetLocalAppDataPath() / "FuckOneDrive";
    OpenPathInExplorer(auditRoot);
}

void RecoveryApp::FinalizeWorkerIfNeeded() {
    if (worker_.joinable() && !workerRunning_.load()) {
        worker_.join();
    }
}

void RecoveryApp::RenderHero() {
    BeginCard("hero-card", ImVec2(0.0f, 140.0f));

    std::string statusCopy;
    {
        std::lock_guard lock(stateMutex_);
        statusCopy = statusLine_;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, HeroColor());
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted("Fuck OneDrive");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 320.0f);
    if (hasScan_ && hasEnvironment_) {
        const ImVec4 healthColor = BuildHealthColor(scan_, environment_);
        ImGui::PushStyleColor(ImGuiCol_Text, healthColor);
        ImGui::TextUnformatted(BuildHealthLabel(scan_, environment_).c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, AccentColor());
        ImGui::TextUnformatted("Awaiting inspection");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
    ImGui::TextWrapped(
        "One window, one job: stop OneDrive from owning the user profile, restore known folders back to %%USERPROFILE%%, and copy files back with SHA-256 verification.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (hasScan_ && hasEnvironment_) {
        const ImVec4 runningColor = environment_.processRunning ? WarnColor() : GoodColor();
        const ImVec4 startupColor = environment_.autoStartEnabled ? WarnColor() : GoodColor();
        if (ImGui::BeginTable("hero-metrics", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            MetricLine("Redirected folders", std::to_string(scan_.redirectedFolderCount));
            ImGui::TableNextColumn();
            MetricLine("Files to inspect", std::to_string(scan_.items.size()));
            ImGui::TableNextColumn();
            MetricLine("OneDrive running", YesNo(environment_.processRunning), &runningColor);
            ImGui::TableNextColumn();
            MetricLine("Auto-start enabled", YesNo(environment_.autoStartEnabled), &startupColor);
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", statusCopy.c_str());

    EndCard();
}

void RecoveryApp::RenderLeftRail() {
    RenderStatusCard();
    ImGui::Spacing();
    RenderActionCard();
    ImGui::Spacing();
    RenderRepairCard();
}

void RecoveryApp::RenderRightRail() {
    RenderFoldersCard();
    ImGui::Spacing();
    RenderPlanCard();
}

void RecoveryApp::RenderStatusCard() {
    BeginCard("status-card", ImVec2(0.0f, 238.0f));
    SectionHeader("Machine State", "This is the current takeover surface for the signed-in Windows user.");

    if (!hasScan_ || !hasEnvironment_) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
        ImGui::TextWrapped("Run a scan first. The tool will inspect OneDrive state, known folder routes, and the recovery candidate set.");
        ImGui::PopStyleColor();
        EndCard();
        return;
    }

    const ImVec4 runningColor = environment_.processRunning ? WarnColor() : GoodColor();
    const ImVec4 startColor = environment_.autoStartEnabled ? WarnColor() : GoodColor();

    MetricLine("OneDrive root", common::PathToUtf8(environment_.rootPath));
    MetricLine("Executable", common::PathToUtf8(environment_.executablePath));
    MetricLine("Running now", YesNo(environment_.processRunning), &runningColor);
    MetricLine("Auto-start", YesNo(environment_.autoStartEnabled), &startColor);
    MetricLine("Redirected folders", std::to_string(scan_.redirectedFolderCount));
    MetricLine("Pending bytes", common::FormatBytes(scan_.totalSourceBytes));

    if (!scan_.warnings.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, WarnColor());
        ImGui::TextUnformatted("Warnings");
        ImGui::PopStyleColor();
        for (const std::string& warning : scan_.warnings) {
            ImGui::BulletText("%s", warning.c_str());
        }
    }

    EndCard();
}

void RecoveryApp::RenderActionCard() {
    const bool running = workerRunning_.load();

    BeginCard("action-card", ImVec2(0.0f, 320.0f));
    SectionHeader("Liberation Steps", "Use the sequence below: inspect, detach, verify the plan, then copy data back.");

    if (ImGui::Button("1. Scan Machine", ImVec2(-1.0f, 32.0f))) {
        RunScan();
    }

    ImGui::Spacing();
    ImGui::Checkbox("Ask OneDrive to shut down if it is running", &stopOneDriveProcess_);
    ImGui::Checkbox("Disable the current user's OneDrive auto-start entry", &disableAutoStart_);
    ImGui::Checkbox("Restore known folders to %USERPROFILE%", &restoreKnownFolders_);

    ImGui::BeginDisabled(running);
    if (ImGui::Button("2. Detach OneDrive Takeover", ImVec2(-1.0f, 36.0f))) {
        RunSystemRepair();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Checkbox("Delete source files only after SHA-256 verification", &deleteSourceAfterVerify_);

    ImGui::BeginDisabled(!hasScan_);
    if (ImGui::Button("3. Export Plan JSON", ImVec2(-1.0f, 32.0f))) {
        ExportCurrentPlan();
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!hasScan_ || scan_.items.empty() || running);
    if (ImGui::Button("4. Recover Files To Local Folders", ImVec2(-1.0f, 38.0f))) {
        StartRecovery();
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Cancel Running Copy Job", ImVec2(-1.0f, 30.0f))) {
        cancelRequested_.store(true);
        AppendLog("Cancellation requested.");
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Open Audit Folder", ImVec2(-1.0f, 30.0f))) {
        OpenAuditFolder();
    }

    EndCard();
}

void RecoveryApp::RenderRepairCard() {
    BeginCard("repair-card", ImVec2(0.0f, 0.0f));
    SectionHeader("System Repair Audit", "This records the non-copy actions: process shutdown, startup removal, and known folder path reset.");

    if (!hasRepairSummary_) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
        ImGui::TextWrapped("No system repair has been run in this session.");
        ImGui::PopStyleColor();
        EndCard();
        return;
    }

    ImGui::TextWrapped("%s", BuildRepairSummary(lastRepairSummary_).c_str());
    if (!lastSystemAuditPath_.empty()) {
        ImGui::Spacing();
        MetricLine("Audit file", common::PathToUtf8(lastSystemAuditPath_));
    }

    if (!lastRepairSummary_.notes.empty()) {
        ImGui::Spacing();
        for (const std::string& note : lastRepairSummary_.notes) {
            ImGui::BulletText("%s", note.c_str());
        }
    }

    if (!lastRepairSummary_.folderResults.empty()) {
        ImGui::Spacing();
        if (ImGui::BeginTable("repair-results", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Folder");
            ImGui::TableSetupColumn("Verified local");
            ImGui::TableSetupColumn("Result");
            ImGui::TableHeadersRow();

            for (const recovery::KnownFolderRepairResult& result : lastRepairSummary_.folderResults) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(result.displayName.c_str());
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, result.verified ? GoodColor() : AccentColor());
                ImGui::TextUnformatted(result.verified ? "Yes" : "Pending restart");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", result.message.c_str());
            }

            ImGui::EndTable();
        }
    }

    EndCard();
}

void RecoveryApp::RenderFoldersCard() {
    BeginCard("folders-card", ImVec2(0.0f, 260.0f));
    SectionHeader("Known Folder Routes", "These are the shell folders that OneDrive typically hijacks first.");

    if (!hasScan_) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
        ImGui::TextWrapped("No folder mapping is available yet.");
        ImGui::PopStyleColor();
        EndCard();
        return;
    }

    if (ImGui::BeginTable("folders", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Folder");
        ImGui::TableSetupColumn("Current route");
        ImGui::TableSetupColumn("Local default");
        ImGui::TableSetupColumn("Recovery source");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (const recovery::FolderBinding& folder : scan_.folders) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(folder.displayName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", common::PathToUtf8(folder.currentPath).c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", common::PathToUtf8(folder.localDefaultPath).c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", common::PathToUtf8(folder.sourcePath).c_str());
            ImGui::TableNextColumn();
            if (folder.redirectedToOneDrive) {
                ImGui::PushStyleColor(ImGuiCol_Text, WarnColor());
                ImGui::TextUnformatted("Redirected");
                ImGui::PopStyleColor();
            } else if (folder.sourceExists) {
                ImGui::PushStyleColor(ImGuiCol_Text, AccentColor());
                ImGui::TextUnformatted("Mirror remains");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, GoodColor());
                ImGui::TextUnformatted("Local");
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }

    EndCard();
}

void RecoveryApp::RenderPlanCard() {
    BeginCard("plan-card", ImVec2(0.0f, 0.0f));
    SectionHeader("Recovery Plan", "Existing local files are never overwritten. Conflicts get compared by SHA-256 and copied to a renamed local path if needed.");

    if (!hasScan_) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftTextColor());
        ImGui::TextWrapped("No recovery plan is available yet.");
        ImGui::PopStyleColor();
        EndCard();
        return;
    }

    ImGui::Text("Plan items: %llu", static_cast<unsigned long long>(scan_.items.size()));
    ImGui::SameLine();
    ImGui::Text("Total bytes: %s", common::FormatBytes(scan_.totalSourceBytes).c_str());

    if (scan_.items.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, GoodColor());
        ImGui::TextWrapped("No candidate files were found in OneDrive mirror folders.");
        ImGui::PopStyleColor();
        EndCard();
        return;
    }

    ImGui::Spacing();
    ImGui::BeginChild("plan-table", ImVec2(0.0f, 330.0f), true);
    if (ImGui::BeginTable("plan", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Folder");
        ImGui::TableSetupColumn("Relative path");
        ImGui::TableSetupColumn("Planned action");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Source state");
        ImGui::TableSetupColumn("Note");
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(scan_.items.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const recovery::PlanItem& item = scan_.items[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(item.folderName.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", common::PathToUtf8(item.relativePath).c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", recovery::DescribePlannedAction(item.action).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(common::FormatBytes(item.bytes).c_str());
                ImGui::TableNextColumn();
                if (item.offline || item.placeholder) {
                    ImGui::PushStyleColor(ImGuiCol_Text, WarnColor());
                    ImGui::TextUnformatted("Needs hydration");
                    ImGui::PopStyleColor();
                } else if (item.targetExists) {
                    ImGui::PushStyleColor(ImGuiCol_Text, AccentColor());
                    ImGui::TextUnformatted("Local file exists");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, GoodColor());
                    ImGui::TextUnformatted("Ready");
                    ImGui::PopStyleColor();
                }
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", item.note.c_str());
            }
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    EndCard();
}

void RecoveryApp::RenderLogCard() {
    const bool running = workerRunning_.load();

    recovery::ProgressSnapshot progressCopy;
    recovery::ExecutionSummary summaryCopy;
    std::filesystem::path manifestCopy;
    std::filesystem::path logCopy;
    std::vector<std::string> logCopyLines;

    {
        std::lock_guard lock(stateMutex_);
        progressCopy = progress_;
        summaryCopy = lastSummary_;
        manifestCopy = lastManifestPath_;
        logCopy = lastLogPath_;
        logCopyLines = logLines_;
    }

    BeginCard("log-card", ImVec2(0.0f, 0.0f));
    SectionHeader("Run Log", "Every action taken by the app ends up here, along with the current copy progress and output artifacts.");

    if (running || progressCopy.totalItems > 0) {
        const float fraction = progressCopy.totalItems == 0 ? 0.0f
                                                            : static_cast<float>(progressCopy.processedItems) / static_cast<float>(progressCopy.totalItems);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
        ImGui::Text("Stage: %s", progressCopy.stage.c_str());
        ImGui::Text("Processed: %llu / %llu", static_cast<unsigned long long>(progressCopy.processedItems),
                    static_cast<unsigned long long>(progressCopy.totalItems));
        ImGui::TextWrapped("Current path: %s", progressCopy.currentPath.c_str());
        ImGui::Text("Processed bytes: %s / %s", common::FormatBytes(progressCopy.processedBytes).c_str(),
                    common::FormatBytes(progressCopy.totalBytes).c_str());
        ImGui::Spacing();
    }

    if (!summaryCopy.itemResults.empty() && !running) {
        ImGui::TextWrapped("%s", BuildSummaryLine(summaryCopy).c_str());
    }
    if (!lastSystemAuditPath_.empty()) {
        MetricLine("Last system audit", common::PathToUtf8(lastSystemAuditPath_));
    }
    if (!manifestCopy.empty()) {
        MetricLine("Last copy manifest", common::PathToUtf8(manifestCopy));
    }
    if (!logCopy.empty()) {
        MetricLine("Last text log", common::PathToUtf8(logCopy));
    }

    ImGui::Spacing();
    ImGui::BeginChild("log-scroll", ImVec2(0.0f, 120.0f), true);
    for (const std::string& line : logCopyLines) {
        ImGui::TextWrapped("%s", line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    EndCard();
}

void RecoveryApp::RunScan() {
    if (workerRunning_.load()) {
        return;
    }

    try {
        environment_ = recovery::DetectOneDriveEnvironment();
        scan_ = recovery::BuildScanResult();
        hasEnvironment_ = true;
        hasScan_ = true;

        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = "Inspection complete. Review system state, detach OneDrive, then run the verified copy job.";
            lastPlanExportPath_.clear();
            progress_ = {};
        }

        std::ostringstream logLine;
        logLine << "Scan completed. Redirected folders: " << scan_.redirectedFolderCount << ", plan items: " << scan_.items.size()
                << ", OneDrive running: " << YesNo(environment_.processRunning) << ", auto-start: " << YesNo(environment_.autoStartEnabled)
                << '.';
        AppendLog(logLine.str());
    } catch (const std::exception& ex) {
        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = "Inspection failed.";
        }
        AppendLog(std::string("Scan failed: ") + ex.what());
    }
}

void RecoveryApp::RunSystemRepair() {
    if (workerRunning_.load()) {
        return;
    }

    if (!hasScan_ || !hasEnvironment_) {
        RunScan();
        if (!hasScan_ || !hasEnvironment_) {
            return;
        }
    }

    try {
        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = "Applying system repair actions...";
        }
        AppendLog("System repair started.");

        const recovery::SystemRepairOptions options{
            stopOneDriveProcess_,
            disableAutoStart_,
            restoreKnownFolders_,
        };

        lastRepairSummary_ = recovery::RepairSystemState(scan_, options);
        hasRepairSummary_ = true;
        lastSystemAuditPath_ = lastRepairSummary_.auditPath;

        AppendLog("System repair finished. " + BuildRepairSummary(lastRepairSummary_));
        for (const std::string& note : lastRepairSummary_.notes) {
            AppendLog(note);
        }

        RunScan();
        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = lastRepairSummary_.explorerRestartRecommended
                              ? "System repair applied. Explorer restart or sign-out may still be required for every app to see the new paths."
                              : "System repair finished.";
        }
    } catch (const std::exception& ex) {
        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = "System repair failed.";
        }
        AppendLog(std::string("System repair failed: ") + ex.what());
    }
}

void RecoveryApp::ExportCurrentPlan() {
    if (!hasScan_) {
        return;
    }

    const std::filesystem::path outputPath =
        common::GetLocalAppDataPath() / "FuckOneDrive" / "plans" / ("plan-" + common::FormatNowForFilename() + ".json");

    std::string errorMessage;
    if (recovery::ExportScanToJson(scan_, outputPath, errorMessage)) {
        {
            std::lock_guard lock(stateMutex_);
            lastPlanExportPath_ = outputPath;
            statusLine_ = "Plan exported.";
        }
        AppendLog("Plan exported to " + common::PathToUtf8(outputPath));
    } else {
        {
            std::lock_guard lock(stateMutex_);
            statusLine_ = "Plan export failed.";
        }
        AppendLog("Plan export failed: " + errorMessage);
    }
}

void RecoveryApp::StartRecovery() {
    if (!hasScan_ || scan_.items.empty() || workerRunning_.load()) {
        return;
    }

    FinalizeWorkerIfNeeded();

    const recovery::ScanResult scanCopy = scan_;
    const recovery::ExecutionOptions options{deleteSourceAfterVerify_};

    cancelRequested_.store(false);
    workerRunning_.store(true);
    {
        std::lock_guard lock(stateMutex_);
        statusLine_ = "Recovery running...";
        progress_ = {};
    }
    AppendLog("Recovery started.");

    worker_ = std::thread([this, scanCopy, options]() {
        recovery::ExecutionSummary summary = recovery::ExecutePlan(
            scanCopy,
            options,
            cancelRequested_,
            [this](const recovery::ProgressSnapshot& snapshot) {
                std::lock_guard lock(stateMutex_);
                progress_ = snapshot;
            });

        {
            std::lock_guard lock(stateMutex_);
            lastSummary_ = summary;
            lastManifestPath_ = summary.manifestPath;
            lastLogPath_ = summary.logPath;
            statusLine_ = summary.cancelled ? "Recovery cancelled." : "Recovery finished.";
        }

        AppendLog("Recovery finished. " + BuildSummaryLine(summary));
        workerRunning_.store(false);
    });
}

void RecoveryApp::AppendLog(const std::string& line) {
    std::lock_guard lock(stateMutex_);
    logLines_.push_back(common::FormatNowForDisplay() + " | " + line);
    if (logLines_.size() > 250) {
        logLines_.erase(logLines_.begin(), logLines_.begin() + static_cast<long long>(logLines_.size() - 250));
    }
}
