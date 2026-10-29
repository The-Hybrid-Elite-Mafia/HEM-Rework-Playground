cmake_minimum_required(VERSION 3.22)
# Builds the 0.10.0 JUCE plug-in code into a scenario renderer, only to prove the C++23 port.
project(PlaygroundLegacyRender VERSION 0.10.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(PG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(HEM_JUCE_DIR "" CACHE PATH "Optional local JUCE 8.0.15 checkout")
if(HEM_JUCE_DIR)
    add_subdirectory(${HEM_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
juce_add_console_app(LegacyRender PRODUCT_NAME "legacy-render")
target_sources(LegacyRender PRIVATE main.cpp sessions.cpp
    ${PG_ROOT}/Source/Engine/PlaygroundEngine.cpp
    ${PG_ROOT}/Source/Engine/MediaLoader.cpp
    ${PG_ROOT}/Source/Engine/SincResampler.cpp
    ${PG_ROOT}/Source/Engine/Exporter.cpp
    ${PG_ROOT}/Source/Engine/Mp3Encoder.cpp
    ${PG_ROOT}/Source/Session/SessionFile.cpp
    ${PG_ROOT}/Source/Plugins/ElitePlugin.cpp
    ${PG_ROOT}/Source/Plugins/EliteVox.cpp
    ${PG_ROOT}/Source/Plugins/EliteEq.cpp
    ${PG_ROOT}/Source/Plugins/EliteComp.cpp
    ${PG_ROOT}/Source/Plugins/EliteBass.cpp
    ${PG_ROOT}/Source/Plugins/EliteChannel.cpp
    ${PG_ROOT}/Source/Plugins/EliteVerb.cpp
    ${PG_ROOT}/Source/Plugins/EliteDelay.cpp
    ${PG_ROOT}/Source/Plugins/EliteMaster.cpp
    ${PG_ROOT}/Source/Plugins/EliteSwirl.cpp
    ${PG_ROOT}/Source/Plugins/EliteTransient.cpp
    ${PG_ROOT}/Source/Plugins/EliteMultiband.cpp
    ${PG_ROOT}/Source/Plugins/EliteWidth.cpp
    ${PG_ROOT}/Source/Plugins/EliteHush.cpp
    ${PG_ROOT}/Source/Plugins/EliteDeEss.cpp
    ${PG_ROOT}/Source/Plugins/EliteDoubler.cpp
    ${PG_ROOT}/Source/Plugins/EliteConsole.cpp
    ${PG_ROOT}/Source/Plugins/EliteLofi.cpp
    ${PG_ROOT}/Source/Plugins/EliteTune.cpp
    ${PG_ROOT}/Source/Plugins/EliteBalance.cpp
    ${PG_ROOT}/Source/Plugins/PluginRegistry.cpp
)
target_include_directories(LegacyRender PRIVATE ${PG_ROOT}/Source)
target_compile_definitions(LegacyRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_PLUGINHOST_VST3=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(LegacyRender PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)
if(WIN32)
    target_link_libraries(LegacyRender PRIVATE mf mfplat mfreadwrite mfuuid ole32)
endif()
