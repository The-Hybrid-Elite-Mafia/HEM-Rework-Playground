cmake_minimum_required(VERSION 3.25)
# HEM Rework Playground engine on The Hybrid Core idea: C++23, no JUCE.
# Each plug-in is its own library (hem_pg_fx_<name>) with its own tests.
project(HEMPlaygroundEngine VERSION 0.11.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # /fp:precise keeps float maths in the written order (renders must match the 0.10.0 engines)
    add_compile_options(/W4 /permissive- /utf-8 /fp:precise /arch:AVX2 /Zc:__cplusplus /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off -mavx2 -mfma)
endif()

add_library(hem_pg_common STATIC common/src/Param.cpp common/src/Plugin.cpp)
target_include_directories(hem_pg_common PUBLIC common/include)

set(HEM_PG_FX vox eq comp bass channel verb delay master swirl transient multiband width hush deess doubler console lofi tune balance)
set(HEM_PG_FX_LIBS)
foreach(fx IN LISTS HEM_PG_FX)
    file(GLOB fx_src CONFIGURE_DEPENDS fx/${fx}/*.cpp)
    add_library(hem_pg_fx_${fx} STATIC ${fx_src})
    target_include_directories(hem_pg_fx_${fx} PUBLIC fx/${fx})
    target_link_libraries(hem_pg_fx_${fx} PUBLIC hem_pg_common)
    list(APPEND HEM_PG_FX_LIBS hem_pg_fx_${fx})
endforeach()

add_library(hem_pg_registry STATIC fx/Registry.cpp)
target_include_directories(hem_pg_registry PUBLIC fx)
target_link_libraries(hem_pg_registry PUBLIC ${HEM_PG_FX_LIBS})

add_library(hem_pg_pugixml STATIC external/pugixml/pugixml.cpp)
target_include_directories(hem_pg_pugixml PUBLIC external/pugixml)
target_compile_options(hem_pg_pugixml PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)

# Free audio decoders (dr_libs, stb_vorbis: public domain / MIT-0), compiled once.
add_library(hem_pg_decoders STATIC external/Decoders.c)
target_include_directories(hem_pg_decoders PUBLIC external/dr_libs external/stb)
target_compile_options(hem_pg_decoders PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)

add_library(hem_pg_presets STATIC presets/Presets.cpp presets/LegacyFactoryPresets.cpp)
target_include_directories(hem_pg_presets PUBLIC presets)
target_link_libraries(hem_pg_presets PUBLIC hem_pg_common PRIVATE hem_pg_pugixml)

# The mixer engine: media, mixer, sessions, undo, export.
add_library(hem_pg_engine STATIC
    engine/Tree.cpp
    engine/PlaygroundEngine.cpp
    engine/media/MediaLoader.cpp
    engine/media/SincResampler.cpp
    engine/session/SessionFile.cpp
    engine/session/UndoHistory.cpp
    engine/export/WavFile.cpp
    engine/export/Exporter.cpp
    engine/export/Mp3Encoder.cpp)
target_include_directories(hem_pg_engine PUBLIC engine)
target_link_libraries(hem_pg_engine PUBLIC hem_pg_registry hem_pg_presets PRIVATE hem_pg_pugixml hem_pg_decoders)
if(WIN32)
    target_link_libraries(hem_pg_engine PRIVATE mf mfplat mfreadwrite mfuuid ole32)
endif()

add_executable(hem_pg_render tools/render/main.cpp)
target_link_libraries(hem_pg_render PRIVATE hem_pg_registry)

add_executable(hem_pg_session_check tools/session-check/main.cpp)
target_link_libraries(hem_pg_session_check PRIVATE hem_pg_engine hem_pg_decoders)

add_executable(hem_pg_bench tools/bench/main.cpp)
target_link_libraries(hem_pg_bench PRIVATE hem_pg_engine)

enable_testing()
add_executable(hem_pg_tests tests/TestMain.cpp tests/PluginTests.cpp tests/EngineCoreTests.cpp tests/MixerTests.cpp)
target_include_directories(hem_pg_tests PRIVATE tests)
target_link_libraries(hem_pg_tests PRIVATE hem_pg_engine hem_pg_registry hem_pg_presets hem_pg_decoders)
add_test(NAME hem_pg_tests COMMAND hem_pg_tests)


