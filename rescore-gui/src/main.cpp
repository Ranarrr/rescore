// SPDX-License-Identifier: MIT
//
// main.cpp - Rescore GUI. A small cross-platform window (Windows / macOS / Linux)
// that converts a legacy Finale .mus file to MusicXML. Drag a file onto the
// window or click to choose one; the MusicXML is written next to the input. This
// is only a thin shell around rescore::convert_mus_to_musicxml - no musical logic
// lives here.

#include <rescore/convert.hpp>
#include <rescore/result.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <nfd.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct AppState {
    std::string status;
    std::string era; // detected Finale build + format generation of the loaded file
    bool ok = false;
};
AppState g_app;

std::string with_extension(const std::string& path, const std::string& ext) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + ext;
    }
    return path.substr(0, dot) + ext;
}

std::string basename_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void convert_file(const std::string& mus_path) {
    std::ifstream in(mus_path, std::ios::binary);
    if (!in) {
        g_app.ok = false;
        g_app.status = "Could not open that file.";
        g_app.era.clear();
        return;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char c : raw) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }

    // Identify the file's Finale build / format generation for display, even if
    // the conversion below fails.
    g_app.era = rescore::describe_era(bytes);

    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(bytes, diags);
    if (!res) {
        g_app.ok = false;
        g_app.status = "Could not convert " + basename_of(mus_path) + ": " + res.message();
        return;
    }
    const std::string out_path = with_extension(mus_path, ".musicxml");
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        g_app.ok = false;
        g_app.status = "Converted, but could not save the output file.";
        return;
    }
    out << res.value();
    g_app.ok = true;
    g_app.status = "Saved " + basename_of(out_path);
}

void choose_file() {
    nfdchar_t* path = nullptr;
    const nfdfilteritem_t filter[1] = {{"Finale .mus file", "mus"}};
    if (NFD_OpenDialog(&path, filter, 1, nullptr) == NFD_OKAY && path != nullptr) {
        convert_file(path);
        NFD_FreePath(path);
    }
}

void drop_callback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (count > 0 && paths != nullptr && paths[0] != nullptr) {
        convert_file(paths[0]);
    }
}

// Horizontally centre the next single line of text within the available width.
// `scale` magnifies the font for this line (headings) and is folded into the
// centring, since ImGui's CalcTextSize does not see the per-window font scale.
void text_centered(const char* text, float scale = 1.0f) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float tw = ImGui::CalcTextSize(text).x * scale;
    if (tw < avail) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
    }
    if (scale != 1.0f) {
        ImGui::SetWindowFontScale(scale);
    }
    ImGui::TextUnformatted(text);
    if (scale != 1.0f) {
        ImGui::SetWindowFontScale(1.0f);
    }
}

// Render the status line: centred when it fits on one line, otherwise wrapped
// within the window so a long error message stays inside the fixed-size window
// instead of running off the right edge.
void text_wrapped(const char* text) {
    if (ImGui::CalcTextSize(text).x < ImGui::GetContentRegionAvail().x) {
        text_centered(text);
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

// Load a pleasant system UI font; fall back to the built-in font if none is found.
void load_font(ImGuiIO& io) {
    static const char* const kCandidates[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };
    for (const char* const path : kCandidates) {
        std::ifstream probe(path, std::ios::binary);
        if (probe.good() && io.Fonts->AddFontFromFileTTF(path, 19.0f) != nullptr) {
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

void render_ui() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Rescore", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    text_centered("Rescore", 1.7f);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.47f, 1.0f));
    text_centered("Convert a Finale .mus file to MusicXML. No Finale needed.");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 18.0f));

    // --- drop card ---------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.93f, 0.94f, 0.98f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.66f, 0.71f, 0.85f, 1.0f));
    ImGui::BeginChild("dropcard", ImVec2(0.0f, 168.0f), ImGuiChildFlags_Border);
    {
        ImGui::Dummy(ImVec2(0.0f, 24.0f));
        text_centered("Drop a .mus file here", 1.25f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.52f, 0.57f, 1.0f));
        text_centered("or");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        const float btn_w = 240.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btn_w) * 0.5f);
        if (ImGui::Button("Choose a file...", ImVec2(btn_w, 42.0f))) {
            choose_file();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // --- status ------------------------------------------------------------
    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    if (!g_app.status.empty()) {
        const ImVec4 col = g_app.ok ? ImVec4(0.13f, 0.55f, 0.24f, 1.0f)
                                    : ImVec4(0.78f, 0.20f, 0.16f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        text_wrapped(g_app.status.c_str());
        ImGui::PopStyleColor();

        // On an error, offer a one-click copy so the message can be shared / pasted.
        if (!g_app.ok) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            const float btn_w = 200.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btn_w) * 0.5f);
            if (ImGui::Button("Copy error message", ImVec2(btn_w, 0.0f))) {
                ImGui::SetClipboardText(g_app.status.c_str());
            }
        }
    }

    // Show the detected Finale build / format generation of the loaded file.
    if (!g_app.era.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.47f, 0.52f, 1.0f));
        text_wrapped(("File format: " + g_app.era).c_str());
        ImGui::PopStyleColor();
    }

    // --- footer ------------------------------------------------------------
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 34.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.57f, 0.62f, 1.0f));
    text_centered("Everything runs on your computer. Nothing is uploaded.");
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace

int main() {
    if (glfwInit() == 0) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required on macOS for 3.2+ core
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(600, 440, "Rescore", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // do not litter an imgui.ini file
    load_font(io);

    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 7.0f;
    style.WindowPadding = ImVec2(30.0f, 18.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.42f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.50f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.34f, 0.74f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.13f, 0.14f, 0.17f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    NFD_Init();

    while (glfwWindowShouldClose(window) == 0) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        render_ui();
        ImGui::Render();

        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.97f, 0.97f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    NFD_Quit();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
