cmake_minimum_required(VERSION 3.22)

project(HEMPlayground VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PG_BUILD_TESTS "Build the Playground unit test console app" ON)
option(PG_BUILD_APP "Build the Playground application" ON)

set(HEM_JUCE_DIR "" CACHE PATH "Optional local JUCE 8.0.15 checkout")
if(HEM_JUCE_DIR)
    add_subdirectory(${HEM_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Engine + plugin DSP: no UI code (the UI registers the editor factory at startup).
set(PG_CORE_SOURCES
    Source/Plugins/ElitePlugin.cpp
    Source/Plugins/EliteVox.cpp
    Source/Plugins/EliteEq.cpp
    Source/Plugins/EliteComp.cpp
    Source/Plugins/EliteBass.cpp
    Source/Plugins/EliteChannel.cpp
    Source/Plugins/EliteVerb.cpp
    Source/Plugins/EliteDelay.cpp
    Source/Plugins/EliteMaster.cpp
    Source/Plugins/EliteSwirl.cpp
    Source/Plugins/PluginRegistry.cpp
    Source/Engine/PlaygroundEngine.cpp
    Source/Engine/MediaLoader.cpp
    Source/Engine/SincResampler.cpp
    Source/Engine/Exporter.cpp
    Source/Session/SessionFile.cpp
    Source/Session/UndoHistory.cpp
    Source/Band/BandTypes.cpp
    Source/Band/BandComposer.cpp
    Source/Band/BandInstruments.cpp
    Source/Band/BandRender.cpp
    Source/Band/BandSampler.cpp
    Source/Library/AudioFacts.cpp
    Source/Library/Library.cpp
    Source/Analysis/Waterfall.cpp)

set(PG_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra)

set(PG_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=0)

juce_add_binary_data(PlaygroundAssets
    NAMESPACE PlaygroundBinary
    HEADER_NAME PlaygroundBinaryData.h
    SOURCES
        Assets/Faceplates/Main-Faceplate.png
        Assets/Icon/HEM-Icon-1024.png
        Assets/Fonts/Poppins-Bold.ttf
        Assets/Fonts/Poppins-Medium.ttf)

set(PG_UI_SOURCES
    Source/UI/FaceplateKit.cpp
    Source/UI/PlaygroundLookAndFeel.cpp
    Source/UI/Editors/EliteEditor.cpp
    Source/UI/Editors/SwirlEditor.cpp
    Source/UI/Spectrum3D.cpp)

set(PG_APP_SOURCES
    Source/SongForge/AceStepClient.cpp
    Source/UI/AppContext.cpp
    Source/UI/PluginWindows.cpp
    Source/UI/WaveCache.cpp
    Source/UI/Pages/EditPage.cpp
    Source/UI/Pages/MixPage.cpp
    Source/UI/Pages/ForgePage.cpp
    Source/UI/Pages/BandForgePanel.cpp
    Source/UI/Pages/LibraryPanel.cpp
    Source/UI/Pages/MasterPage.cpp
    Source/UI/MainComponent.cpp)

option(PG_BUILD_PREVIEW "Build the headless faceplate preview tool" OFF)
if(PG_BUILD_PREVIEW)
    juce_add_console_app(FaceplatePreview PRODUCT_NAME "FaceplatePreview")
    target_sources(FaceplatePreview PRIVATE ${PG_CORE_SOURCES} ${PG_UI_SOURCES} Tools/FaceplatePreview.cpp)
    target_include_directories(FaceplatePreview PRIVATE Source)
    set(PG_PREVIEW_DEFS ${PG_COMMON_DEFS})
    list(FILTER PG_PREVIEW_DEFS EXCLUDE REGEX "JUCE_MODAL_LOOPS_PERMITTED")
    target_compile_definitions(FaceplatePreview PRIVATE ${PG_PREVIEW_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(FaceplatePreview PRIVATE PlaygroundAssets ${PG_JUCE_MODULES} PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(ScreenPreview PRODUCT_NAME "ScreenPreview")
    target_sources(ScreenPreview PRIVATE ${PG_CORE_SOURCES} ${PG_UI_SOURCES} ${PG_APP_SOURCES} Tools/ScreenPreview.cpp)
    target_include_directories(ScreenPreview PRIVATE Source)
    target_compile_definitions(ScreenPreview PRIVATE ${PG_PREVIEW_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 HEM_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(ScreenPreview PRIVATE PlaygroundAssets ${PG_JUCE_MODULES} PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(BandDemo PRODUCT_NAME "BandDemo")
    target_sources(BandDemo PRIVATE ${PG_CORE_SOURCES} Tools/BandDemo.cpp)
    target_include_directories(BandDemo PRIVATE Source)
    target_compile_definitions(BandDemo PRIVATE ${PG_PREVIEW_DEFS})
    target_link_libraries(BandDemo PRIVATE ${PG_JUCE_MODULES} PUBLIC juce::juce_recommended_config_flags)
endif()

if(PG_BUILD_APP)
    juce_add_gui_app(ReworkPlayground
        PRODUCT_NAME "HEM Rework Playground"
        COMPANY_NAME "Hybrid Elite Mafia"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia. All rights reserved."
        BUNDLE_ID com.hybridelitemafia.reworkplayground
        VERSION ${PROJECT_VERSION}
        ICON_BIG   ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png
        ICON_SMALL ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png)
    target_sources(ReworkPlayground PRIVATE ${PG_CORE_SOURCES} ${PG_UI_SOURCES} ${PG_APP_SOURCES} Source/Main.cpp)
    target_include_directories(ReworkPlayground PRIVATE Source)
    target_compile_definitions(ReworkPlayground PRIVATE ${PG_COMMON_DEFS} HEM_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(ReworkPlayground
        PRIVATE PlaygroundAssets ${PG_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

if(PG_BUILD_TESTS)
    juce_add_console_app(PlaygroundTests PRODUCT_NAME "PlaygroundTests")
    target_sources(PlaygroundTests PRIVATE ${PG_CORE_SOURCES} ${PG_TEST_EXTRA_SOURCES}
        Tests/TestMain.cpp
        Tests/PluginTests.cpp
        Tests/EngineTests.cpp
        Tests/SessionExportTests.cpp
        Tests/BandTests.cpp
        Tests/BandSamplerTests.cpp
        Tests/LibraryTests.cpp)
    target_include_directories(PlaygroundTests PRIVATE Source)
    target_compile_definitions(PlaygroundTests PRIVATE ${PG_COMMON_DEFS} JUCE_UNIT_TESTS=1)
    target_link_libraries(PlaygroundTests
        PRIVATE ${PG_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_link_options(PlaygroundTests PRIVATE /STACK:16777216)
    endif()
    enable_testing()
    add_test(NAME PlaygroundTests COMMAND PlaygroundTests)
endif()
