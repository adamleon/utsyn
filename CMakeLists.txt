cmake_minimum_required(VERSION 3.25)
project(utsyn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vulkan backend — opt-in (default OFF so the GL build/ and build-ros/ trees are
# unaffected). Configure the Vulkan tree with -DUTSYN_WITH_VULKAN=ON. Requires the
# Vulkan SDK (VULKAN_SDK env var present) at configure time.
option(UTSYN_WITH_VULKAN "Build utsyn's threepp Vulkan backend (requires Vulkan SDK)" OFF)

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

# SPIKE: build threepp (and its vendored GLFW) as a SHARED library so there is ONE
# threepp instance shared by utsyn_core and every plugin. threepp's renderer uses
# RTTI/dynamic_cast pervasively, so a plugin that creates threepp objects MUST share
# the same instance or its objects' type_info won't match the core renderer's.
# threepp has no explicit lib type, so it honors BUILD_SHARED_LIBS. imgui_lib/
# implot_lib are declared explicit STATIC below, so they stay static regardless.
# --- Vulkan backend deps (opt-in) ---
# threepp's VulkanRenderer #includes <vk_mem_alloc.h> (Vulkan Memory Allocator).
# The LunarG SDK here does NOT bundle the optional VMA component and threepp does
# not vendor it, so fetch the single header and pre-seed threepp's VMA search
# (threepp does find_path(VMA_INCLUDE_DIR ...), satisfied here from the cache).
# Must run before threepp is configured. THREEPP_WITH_VULKAN drives threepp's own
# find_package(Vulkan) + SPIR-V shader compilation.
if(UTSYN_WITH_VULKAN)
    FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.1.0
    )
    FetchContent_GetProperties(vma)
    if(NOT vma_POPULATED)
        FetchContent_Populate(vma)   # download only — we just need the header
    endif()
    set(VMA_INCLUDE_DIR "${vma_SOURCE_DIR}/include" CACHE PATH "VMA include dir for threepp" FORCE)
    set(THREEPP_WITH_VULKAN ON CACHE BOOL "" FORCE)
endif()

set(BUILD_SHARED_LIBS ON)
FetchContent_MakeAvailable(threepp)
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(imgui implot nanosvg nlohmann_json)

if(UTSYN_WITH_VULKAN)
    # threepp links Vulkan::Vulkan PUBLIC, but imgui_lib (built below) is a
    # separate target that needs the Vulkan headers/lib for imgui_impl_vulkan.
    find_package(Vulkan REQUIRED)
    message(STATUS "utsyn: Vulkan backend ON (Vulkan ${Vulkan_VERSION})")
endif()

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

if(UTSYN_WITH_VULKAN)
    # Dual ImGui backend: keep imgui_impl_opengl3 (GL path) and add the Vulkan one.
    # Use utsyn's own docking-branch backend, not threepp's example copy.
    target_sources(imgui_lib PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_link_libraries(imgui_lib PUBLIC Vulkan::Vulkan)
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
    src/ros/PackageResolver.cpp
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

# Absolute path to the source assets dir (fonts, etc.) baked in, so the running exe
# finds them regardless of CWD or which build tree it runs from. Dev-oriented;
# packaging uses install(DIRECTORY assets/ ...) below.
target_compile_definitions(utsyn_core PRIVATE
    UTSYN_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Vulkan backend defines. UTSYN_WITH_VULKAN (PUBLIC) gates utsyn's own code +
# Application.hpp member layout (app + plugins must agree). THREEPP_WITH_VULKAN
# (PRIVATE — only Application.cpp includes the header that needs it) activates the
# Vulkan branch in threepp's ImguiContext.hpp; threepp defines it only PRIVATE to
# itself, so a consumer that uses ImguiContext's Vulkan path must define it too.
if(UTSYN_WITH_VULKAN)
    target_compile_definitions(utsyn_core PUBLIC UTSYN_WITH_VULKAN=1)
    target_compile_definitions(utsyn_core PRIVATE THREEPP_WITH_VULKAN=1)
endif()

if(ROS2_AVAILABLE)
    # Modern ROS2 (Jazzy) target-based linking. ament_target_dependencies() links
    # with a PLAIN target_link_libraries internally, which collides with our
    # keyword-style linking ("cannot mix plain and keyword signatures"). Link the
    # namespaced / <pkg>_TARGETS targets directly instead. PUBLIC so plugins inherit
    # the ROS includes + libs transitively (they instantiate create_subscription<Msg>
    # in their own TUs).
    target_link_libraries(utsyn_core PUBLIC
        rclcpp::rclcpp
        tf2::tf2
        tf2_ros::tf2_ros
        ${std_msgs_TARGETS}
        ${sensor_msgs_TARGETS}
        ${geometry_msgs_TARGETS}
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

# threepp is a SHARED lib, and threepp emits threepp.dll / glfw3.dll into its own
# output dir (build/bin), NOT next to utsyn.exe. Copy every runtime DLL utsyn
# depends on (threepp.dll, glfw3.dll, ...) next to the exe after each build, so the
# conventional build/<config>/utsyn.exe run path works and the plugin loader (which
# scans the exe's directory) finds threepp alongside it.
add_custom_command(TARGET utsyn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:utsyn> $<TARGET_FILE_DIR:utsyn>
    COMMAND_EXPAND_LISTS
)

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
# Plugins link utsyn_core PRIVATE and inherit its PUBLIC ROS usage requirements
# (rclcpp + message includes/libs, marked SYSTEM) transitively, so no explicit ROS
# linkage is needed here — even though subscribe<Msg> instantiates in the plugin's
# own TU.

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
# ROS deps inherited transitively from utsyn_core (see plugin_example).

# ============================================================
# Robot description plugin (URDF -> threepp Robot as an Actor)
# ============================================================
# RobotActor.cpp uses threepp's URDFLoader/Robot directly — this is the first
# plugin to create threepp objects, which is why threepp is built SHARED (one
# instance shared with utsyn_core; see the threepp link section). threepp's headers
# propagate (SYSTEM) from utsyn_core, so /W4 /WX is not tripped by them.
add_library(plugin_robot_description SHARED
    plugins/robot_description/RobotActor.cpp
    plugins/robot_description/RobotDescriptionPlugin.cpp
)
target_link_libraries(plugin_robot_description PRIVATE utsyn_core utsyn_warnings)
set_target_properties(plugin_robot_description PROPERTIES
    PREFIX ""   # No "lib" prefix — plugins load by name
)
# ROS deps inherited transitively from utsyn_core (see plugin_example).

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

# NOTE: no ament_package() — utsyn is a standalone CMake app that *consumes* ROS2
# via find_package, not a colcon/ament package (ament_package() would require a
# package.xml and is for packages built inside a ROS2 workspace).
