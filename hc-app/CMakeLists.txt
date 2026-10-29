# =============================================================================
#  HEM Rework Playground 0.11 — the app on The Hybrid Core (C++23, MSVC, Windows only, no JUCE).
#
#    external/HybridCore  : The Hybrid Core, a pinned git submodule (GPLv3): audio devices (WASAPI shared /
#                           exclusive, ASIO), the GPU renderer, faceplate parts, Poppins, FxPanel faceplates.
#    hc-engine/           : the Playground engine (mixer, 19 plug-ins, sessions, export, Band Forge,
#                           Library, Song Forge client). Built with its own flags (/fp:precise, no
#                           link-time code generation) so it renders exactly like the verified engine.
#    hc-app/src           : this app (pages, plug-in windows, settings, sound card).
#
#  ASIO: Steinberg's ASIO SDK is fetched at build time (external/HybridCore/tools/fetch-asio-sdk.ps1,
#  SHA-512 checked) and used under its GPLv3 option. It is never committed.
# =============================================================================
cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "[Playground] Windows host required.")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Playground configurations" FORCE)

project(HEMReworkPlayground
    VERSION 0.11.1
    DESCRIPTION "HEM Rework Playground"
    LANGUAGES C CXX RC)

set(HEM_HC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/HybridCore")
if(NOT EXISTS "${HEM_HC_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "[Playground] external/HybridCore is empty: git submodule update --init")
endif()

list(APPEND CMAKE_MODULE_PATH "${HEM_HC_DIR}/cmake")
include(HybridCorePlatform)

# One floating-point rule for the whole program: no FMA contraction anywhere. The engine's inline maths
# (control ranges, gains) is also compiled into the app's own files; with two different rules the linker
# could keep either copy and a loaded session could land 1 bit away from the verified engine.
get_target_property(_hc_opts hc_build_flags INTERFACE_COMPILE_OPTIONS)
list(REMOVE_ITEM _hc_opts "/fp:contract")
set_target_properties(hc_build_flags PROPERTIES INTERFACE_COMPILE_OPTIONS "${_hc_opts}")

# The Hybrid Core as a library (its own app, tools and tests are not built here).
set(HC_BUILD_APPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${HEM_HC_DIR}" HybridCore EXCLUDE_FROM_ALL)

# The engine with its own flags and without link-time code generation (bit-identical renders).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../hc-engine" hc-engine)

# ---- the app's own code (no Win32 in the gui/ and pages/ code: it is also syntax-checked on Linux) ----
add_library(hem_app_core STATIC
    src/gui/Paint.cpp
    src/gui/Ui.cpp
    src/gui/Dialog.cpp
    src/Settings.cpp
    src/WaveCache.cpp
    src/PluginPanels.cpp)
target_include_directories(hem_app_core PUBLIC src)
target_link_libraries(hem_app_core PUBLIC hc::build_flags hc::ui hem_pg_engine hem_pg_registry hem_pg_presets PRIVATE hem_pg_pugixml)

configure_file(resources/Playground.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Playground.rc" @ONLY)
add_executable(ReworkPlayground WIN32
    src/main.cpp
    src/App.cpp
    src/AudioHost.cpp
    src/PluginWindows.cpp
    src/platform/Platform.cpp
    src/pages/EditPage.cpp
    src/pages/MixPage.cpp
    src/pages/Strips.cpp
    src/pages/MasterPage.cpp
    src/pages/Spectrum3D.cpp
    src/pages/ForgePage.cpp
    src/pages/BandForgePanel.cpp
    src/pages/LibraryPanel.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/Playground.rc")
target_link_libraries(ReworkPlayground PRIVATE hem_app_core hc::io hc::ui hc::core
    hem_pg_engine hem_pg_band hem_pg_library hem_pg_analysis hem_pg_songforge hem_pg_registry hem_pg_presets
    opengl32 shell32 ole32 comctl32 comdlg32 user32 gdi32)
set_target_properties(ReworkPlayground PROPERTIES OUTPUT_NAME "HEM Rework Playground")
target_compile_definitions(ReworkPlayground PRIVATE HEM_PLAYGROUND_VERSION="${PROJECT_VERSION}")
target_link_options(ReworkPlayground PRIVATE /MANIFEST:NO)   # the manifest is in the .rc (Common Controls 6, per-monitor DPI)

# runtime art next to the exe: Poppins (The Hybrid Core), the main faceplate and the HEM logo (this repo)
add_custom_command(TARGET ReworkPlayground POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ReworkPlayground>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ReworkPlayground>/assets/faceplates"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ReworkPlayground>/assets/brand"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${HEM_HC_DIR}/assets/fonts" "$<TARGET_FILE_DIR:ReworkPlayground>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Faceplates/Main-Faceplate.png" "$<TARGET_FILE_DIR:ReworkPlayground>/assets/faceplates/Main-Faceplate.png"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Icon/HEM-Icon-1024.png" "$<TARGET_FILE_DIR:ReworkPlayground>/assets/brand/HEM-Icon-1024.png"
    VERBATIM)

# ---- tests of the app's own code (settings import, controls, plug-in faceplates) ----
enable_testing()
add_executable(hem_app_tests tests/AppTests.cpp)
target_link_libraries(hem_app_tests PRIVATE hem_app_core hem_pg_engine hem_pg_registry hem_pg_presets)
add_test(NAME hem_app_tests COMMAND hem_app_tests)
