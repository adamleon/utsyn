cmake_minimum_required(VERSION 3.25)
project(utsyn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# Compiler flags
# ============================================================
# Scope strict warnings to OUR targets only via an INTERFACE library. Applying
# them directory-wide (add_compile_options) would force /WX onto FetchContent
# dependencies like threepp, whose benign warnings would then break the build.
add_library(utsyn_warnings INTERFACE)
if(MSVC)
    target_compile_options(utsyn_warnings INTERFACE
        /W4
        /WX             # Warnings as errors
        /permissive-    # Strict conformance
        /Zc:__cplusplus # Correct __cplusplus macro
        /utf-8
    )
else()
    # clang-cl fallback
    target_compile_options(utsyn_warnings INTERFACE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
    )
endif()

# ============================================================
# Dependencies via FetchContent
# ============================================================
include(FetchContent)

# --- threepp (main branch) ---
# We consume only the threepp library. Disable its examples and test suite so
# CMake does not fetch the separate threepp_data asset repo (and so we don't
# compile threepp's examples/tests). This also keeps offline builds working.
set(THREEPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(THREEPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    threepp
    GIT_REPOSITORY https://github.com/markaren/threepp.git
    GIT_TAG        master
    SYSTEM   # treat threepp's headers as system headers so their warnings
             # don't trip our /WX when we include them
)

# --- ImGui (docking branch) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)

# --- ImPlot ---
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        master
)

# --- nanosvg ---
FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        master
)

# --- nlohmann/json (layout persistence) ---
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(threepp imgui implot nanosvg nlohmann_json)

# ============================================================
# ImGui library target
# (ImGui does not ship its own CMakeLists — build it manually)
# ============================================================
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    # Docking-specific
    # Backend — adjust if switching from OpenGL
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
# SYSTEM so ImGui's headers don't trip consumers' /WX.
target_include_directories(imgui_lib SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# The GLFW backend needs GLFW/glfw3.h. Reuse threepp's vendored GLFW target so
# imgui_lib inherits its include directory and links against it.
target_link_libraries(imgui_lib PUBLIC glfw)

# ImGui is delivered to plugins through utsyn_core.dll as a SINGLE shared instance
# (see the utsyn_core link section for why a second copy crashes). Mark ImGui's API
# dllexport so its symbols are exported from whichever DLL the imgui objects land in
# (utsyn_core.dll). WINDOWS_EXPORT_ALL_SYMBOLS does NOT cover symbols pulled from a
# static lib, so this explicit dllexport is required. PUBLIC so utsyn_core's own TUs
# that include imgui.h agree on the dllexport linkage.
if(WIN32)
    target_compile_definitions(imgui_lib PUBLIC "IMGUI_API=__declspec(dllexport)")
endif()

# ============================================================
# ImPlot library target
# ============================================================
add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot_lib SYSTEM PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ============================================================
# ROS2
# ============================================================
find_package(ament_cmake QUIET)
if(ament_cmake_FOUND)
    find_package(rclcpp REQUIRED)
    find_package(std_msgs REQUIRED)
    find_package(sensor_msgs REQUIRED)
    find_package(geometry_msgs REQUIRED)
    find_package(tf2 REQUIRED)
    find_package(tf2_ros REQUIRED)
    set(ROS2_AVAILABLE TRUE)
    message(STATUS "ROS2 found — building with ROS2 support")
else()
    set(ROS2_AVAILABLE FALSE)
    message(WARNING "ROS2 not found — building without ROS2 support")
endif()

# ============================================================
# Core library (shared between main app and plugins)
# ============================================================
add_library(utsyn_core SHARED
    src/app/Application.cpp
    src/app/LayoutManager.cpp
    src/app/Logger.cpp
    src/rendering/Viewport.cpp
    src/rendering/ViewportManager.cpp
    src/rendering/SceneManager.cpp
    src/widgets/ViewportPanel.cpp
    src/widgets/MessageMonitor.cpp
    src/ros/RosCore.cpp
    src/ros/SubscriptionRegistry.cpp
    src/plugins/PluginLoader.cpp
)

# utsyn_core is SHARED so the app and every plugin DLL share one instance of
# core state (e.g. the Logger singleton). On Windows a DLL only produces an
# import .lib if it exports symbols; auto-export them all so utsyn and plugins
# can link against it without annotating every class with __declspec(dllexport).
set_target_properties(utsyn_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(utsyn_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# nanosvg is third-party — SYSTEM so its header warnings don't trip our /WX.
target_include_directories(utsyn_core SYSTEM PUBLIC
    ${nanosvg_SOURCE_DIR}/src
)

target_link_libraries(utsyn_core PUBLIC
    threepp
    nlohmann_json::nlohmann_json
)

# ImGui/ImPlot must exist in EXACTLY ONE place. ImGui keeps global state (GImGui)
# per copy of its code: the app creates the context in utsyn_core, so utsyn_core's
# GImGui is the only valid one. If a plugin statically linked its own second copy
# of ImGui, that copy's GImGui would be NULL and the plugin's first ImGui::Begin()
# would dereference null and crash. So link ImGui/ImPlot PRIVATE — they are NOT
# propagated to plugins as link inputs. Plugins instead resolve ImGui from
# utsyn_core's exports (one instance, one context; utsyn_core is SHARED with
# WINDOWS_EXPORT_ALL_SYMBOLS) and get the headers from the PUBLIC include dirs below.
target_link_libraries(utsyn_core PRIVATE imgui_lib implot_lib)
target_include_directories(utsyn_core SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)
if(WIN32)
    # Consumers of utsyn_core (the plugins) import ImGui from utsyn_core.dll.
    target_compile_definitions(utsyn_core INTERFACE "IMGUI_API=__declspec(dllimport)")
endif()

# dlopen/dlsym for the plugin loader on Linux; empty string on Windows (LoadLibrary
# lives in kernel32, linked by default).
target_link_libraries(utsyn_core PUBLIC ${CMAKE_DL_LIBS})
target_link_libraries(utsyn_core PRIVATE utsyn_warnings)

if(ROS2_AVAILABLE)
    target_link_libraries(utsyn_core PUBLIC
        rclcpp::rclcpp
        tf2::tf2
        tf2_ros::tf2_ros
    )
    ament_target_dependencies(utsyn_core
        rclcpp std_msgs sensor_msgs geometry_msgs tf2 tf2_ros
    )
    target_compile_definitions(utsyn_core PUBLIC UTSYN_ROS2=1)
endif()

# ============================================================
# Main executable
# ============================================================
add_executable(utsyn
    src/main.cpp
)
target_link_libraries(utsyn PRIVATE utsyn_core utsyn_warnings)

# ============================================================
# Example plugin
# ============================================================
add_library(plugin_example SHARED
    plugins/example_plugin/ExamplePlugin.cpp
)
target_link_libraries(plugin_example PRIVATE utsyn_core utsyn_warnings)
set_target_properties(plugin_example PROPERTIES
    PREFIX ""   # No "lib" prefix — plugins load by name
)

# When ROS2 is present, a plugin that calls broker.subscribe<Msg>() instantiates
# rclcpp::create_subscription<Msg> in its OWN translation unit, so the plugin
# target must link rclcpp and the message packages directly (it cannot rely on
# utsyn_core's usage requirements for template instantiation). UTSYN_ROS2 itself
# propagates from utsyn_core. ExamplePlugin doesn't subscribe yet, but wiring this
# now keeps the demo plugin (next deliverable) buildable.
if(ROS2_AVAILABLE)
    ament_target_dependencies(plugin_example
        rclcpp std_msgs sensor_msgs geometry_msgs tf2 tf2_ros
    )
endif()

# ============================================================
# Robot monitor demo plugin (topic-monitoring base demo)
# ============================================================
add_library(plugin_robot_monitor SHARED
    plugins/robot_monitor/RobotMonitorPlugin.cpp
)
target_link_libraries(plugin_robot_monitor PRIVATE utsyn_core utsyn_warnings)
set_target_properties(plugin_robot_monitor PROPERTIES
    PREFIX ""   # No "lib" prefix — plugins load by name
)
# This plugin subscribes (broker.subscribe<JointState/String>), so when ROS2 is
# present it instantiates create_subscription<Msg> in its own TU and must link the
# message packages directly (UTSYN_ROS2 itself propagates from utsyn_core). ament
# marks these include dirs SYSTEM, so /W4 /WX is not tripped by rclcpp headers.
if(ROS2_AVAILABLE)
    ament_target_dependencies(plugin_robot_monitor
        rclcpp std_msgs sensor_msgs geometry_msgs tf2 tf2_ros
    )
endif()

# ============================================================
# Assets
# ============================================================
# Copy font to build directory (skip until the asset is added)
set(UTSYN_FONT ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf)
if(EXISTS ${UTSYN_FONT})
    configure_file(
        ${UTSYN_FONT}
        ${CMAKE_CURRENT_BINARY_DIR}/assets/fonts/JetBrainsMono-Regular.ttf
        COPYONLY
    )
else()
    message(WARNING "Font not found at ${UTSYN_FONT} — skipping asset copy")
endif()

# ============================================================
# Install
# ============================================================
install(TARGETS utsyn utsyn_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
install(DIRECTORY assets/ DESTINATION share/utsyn/assets)

# ============================================================
# Clang-tidy (optional, enable with -DENABLE_CLANG_TIDY=ON)
# ============================================================
option(ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set_target_properties(utsyn_core utsyn PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
        )
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found")
    endif()
endif()

if(ROS2_AVAILABLE)
    ament_package()
endif()
