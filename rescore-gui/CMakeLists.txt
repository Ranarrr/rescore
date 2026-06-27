# rescore-gui/CMakeLists.txt
#
# A small Dear ImGui + GLFW desktop front-end around rescore::core. Self-contained:
# GLFW, Dear ImGui, and nativefiledialog-extended are fetched via FetchContent so
# a checkout builds with no system GUI packages (only system OpenGL is required).

include(FetchContent)

# --- GLFW (windowing / input) -----------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui (ships no CMake; compile the core + GLFW/OpenGL3 backends) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

# --- nativefiledialog-extended (the native "Open file" dialog) ---------------
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.2.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nfd)

find_package(OpenGL REQUIRED)

add_library(rescore_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
# Treat ImGui headers as system headers so the project's /W4 /WX posture does not
# trip over third-party code.
target_include_directories(rescore_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(rescore_imgui PUBLIC glfw OpenGL::GL)

# The GUI is the project's primary app, so its binary is simply `rescore`.
add_executable(rescore src/main.cpp)
target_link_libraries(rescore PRIVATE rescore::core rescore_imgui nfd)

# Per-platform app packaging:
#  - Windows: GUI-subsystem app (no console window), keeping a standard main().
#  - macOS:   a .app bundle (double-clickable, no terminal).
#  - Linux:   a plain ELF. Build deps: GLFW needs X11 or Wayland dev headers, and
#             nativefiledialog-extended needs GTK 3 dev (or configure with
#             -DNFD_PORTAL=ON to use the xdg-desktop-portal / DBus backend).
if(WIN32)
    set_target_properties(rescore PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(rescore PRIVATE "/ENTRY:mainCRTStartup")
elseif(APPLE)
    set_target_properties(rescore PROPERTIES MACOSX_BUNDLE TRUE)
endif()

# Install the GUI alongside the CLI when it is built.
install(TARGETS rescore
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin)
