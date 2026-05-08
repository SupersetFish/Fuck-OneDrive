#include "app.h"
#include "common.h"

#include <Windows.h>
#include <objbase.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <filesystem>

namespace {

void ApplyStyle() {
    ImGui::StyleColorsLight();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 14.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.ScrollbarRounding = 12.0f;
    style.ChildRounding = 12.0f;
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(11.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.925f, 0.90f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.985f, 0.978f, 0.965f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.29f, 0.39f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.26f, 0.36f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.88f, 0.83f, 0.74f, 0.95f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.91f, 0.86f, 0.77f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.36f, 0.53f, 0.97f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.31f, 0.48f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.11f, 0.25f, 0.40f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.79f, 0.32f, 0.16f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.80f, 0.77f, 0.72f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.96f, 0.95f, 0.93f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.93f, 0.91f, 0.86f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.91f, 0.88f, 0.82f, 1.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.965f, 0.955f, 0.94f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.84f, 0.81f, 0.76f, 1.0f);
}

void LoadBestEffortFont() {
    ImGuiIO& io = ImGui::GetIO();

    const std::filesystem::path fontCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;

    for (const std::filesystem::path& fontPath : fontCandidates) {
        if (!std::filesystem::exists(fontPath)) {
            continue;
        }

        if (io.Fonts->AddFontFromFileTTF(common::PathToUtf8(fontPath).c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull()) !=
            nullptr) {
            return;
        }
    }

    io.Fonts->AddFontDefault();
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(comInit);

    if (!glfwInit()) {
        if (comInitialized) {
            CoUninitialize();
        }
        return 1;
    }

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1680, 980, "Fuck OneDrive", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        if (comInitialized) {
            CoUninitialize();
        }
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LoadBestEffortFont();
    ApplyStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    RecoveryApp app;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.Render();

        ImGui::Render();
        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.94f, 0.925f, 0.90f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (comInitialized) {
        CoUninitialize();
    }

    return 0;
}
