cmake_minimum_required(VERSION 3.24)

# Before project(): on Apple platforms project() creates an empty cache entry
# for the deployment target, and a set(... CACHE ...) after it never applies,
# so the plugin only loads on the macOS it was built on (PsyBox, PhaseFucker
# and BuildApp shipped with minos 26.0 this way until 2026-10-17). FORCE also
# fills that empty entry in existing build dirs; an explicit
# -DCMAKE_OSX_DEPLOYMENT_TARGET still wins. Check: otool -l <binary> | grep minos
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Release builds pass the tag's version, e.g. -DDISTITTEN_VERSION=1.2.0.
set(DISTITTEN_VERSION "0.1.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(DisTitten VERSION ${DISTITTEN_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DISTITTEN_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(DISTITTEN_BUILD_TESTS "Build the DSP unit tests" ON)
option(DISTITTEN_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: the DSP. Plain C++20, no JUCE, so the tests build in seconds and the
# audio code cannot reach into GUI types. Every .cpp is listed by hand (no
# globbing).
# ---------------------------------------------------------------------------
add_library(distitten_core STATIC
    src/core/model/Params.cpp
    src/core/engine/EffectSlot.cpp
    src/core/engine/Engine.cpp
)
target_include_directories(distitten_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(distitten_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(distitten_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(DISTITTEN_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    # The fonts src/ui/Theme.cpp loads (BuildApp/PhaseFucker: IBM Plex; PsyBox:
    # Inter + JetBrains Mono). Keep the licence file next to them in resources/fonts.
    juce_add_binary_data(distitten_assets SOURCES
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-Medium.ttf)

    juce_add_plugin(DisTitten
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.distitten"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE DsTt
        FORMATS VST3 AU
        PRODUCT_NAME "DisTitten"
        # ---- EFFECT (BuildApp, PhaseFucker) -------------------------------
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Filter          # BuildApp: Fx Filter; PhaseFucker: Fx Tools
        AU_MAIN_TYPE kAudioUnitType_Effect # auval type aufx
        # ---- INSTRUMENT (PsyBox): replace the block above with ------------
        # FORMATS VST3 AU Standalone       # (replaces the FORMATS line above)
        # IS_SYNTH TRUE
        # NEEDS_MIDI_INPUT TRUE
        # NEEDS_MIDI_OUTPUT FALSE
        # IS_MIDI_EFFECT FALSE
        # EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        # VST3_CATEGORIES Instrument Synth
        # AU_MAIN_TYPE kAudioUnitType_MusicDevice   # auval type aumu
        # ---- STANDALONE that takes audio input (PhaseFucker had one before
        # its demo host replaced it; it never shipped it):
        # FORMATS VST3 AU Standalone
        # MICROPHONE_PERMISSION_ENABLED TRUE
        # MICROPHONE_PERMISSION_TEXT "DisTitten listens to its inputs."
        # ---- SIDECHAIN: nothing here. The bus is declared in the processor's
        # BusesProperties (.withInput ("Reference", stereo, true)), see the audio-plugin skill (references/build-and-release.md).
        COPY_PLUGIN_AFTER_BUILD ${DISTITTEN_COPY_PLUGIN})

    # Listed once, used by the plugin and the dev tools. The tools compile these
    # sources directly (not the plugin target), so a new src/plugin or src/ui
    # .cpp goes here and nowhere else.
    set(DISTITTEN_UI_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/ThermalImport.cpp
        src/ui/Theme.cpp
        src/ui/Controls.cpp
        src/ui/Widgets.cpp
        src/ui/StageView.cpp
        src/ui/MainViews.cpp
        src/ui/EditViews.cpp
        src/ui/WaveView.cpp)

    target_sources(DisTitten PRIVATE ${DISTITTEN_UI_SOURCES})
    target_include_directories(DisTitten PRIVATE src)

    target_compile_definitions(DisTitten PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(DisTitten
        PRIVATE
            distitten_core
            distitten_assets
            juce::juce_audio_utils
            # juce::juce_dsp               # PsyBox links it; add only if the plugin layer uses it
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    # The tools define the JucePlugin_* macros the processor/editor use
    # themselves (getName() uses JucePlugin_Name, the editor header prints
    # JucePlugin_VersionString). Add any other JucePlugin_* macro you use here
    # and on the demo target too.
    juce_add_console_app(distitten_snapshot PRODUCT_NAME "distitten_snapshot")
    target_sources(distitten_snapshot PRIVATE tools/Snapshot.cpp ${DISTITTEN_UI_SOURCES})
    target_include_directories(distitten_snapshot PRIVATE src tests)
    target_compile_definitions(distitten_snapshot PRIVATE
        JucePlugin_Name="DisTitten"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distitten_snapshot PRIVATE
        distitten_core distitten_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: renders audio through DisTitten with an Output Thermal preset loaded, for A/B checks
    # against Thermal (tools/ThermalRender.cpp).
    juce_add_console_app(distitten_render PRODUCT_NAME "distitten_render")
    target_sources(distitten_render PRIVATE tools/ThermalRender.cpp ${DISTITTEN_UI_SOURCES})
    target_include_directories(distitten_render PRIVATE src tests)
    target_compile_definitions(distitten_render PRIVATE
        JucePlugin_Name="DisTitten"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distitten_render PRIVATE
        distitten_core distitten_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: the plugin in a window with a built-in psytrance loop (or a
    # file) playing through it. ./start.sh runs it.
    # (An instrument has no demo host: PsyBox's start.sh opens its Standalone.)
    juce_add_gui_app(distitten_demo PRODUCT_NAME "DisTitten Demo")
    target_sources(distitten_demo PRIVATE tools/Demo.cpp ${DISTITTEN_UI_SOURCES})
    target_include_directories(distitten_demo PRIVATE src tests)
    target_compile_definitions(distitten_demo PRIVATE
        JucePlugin_Name="DisTitten"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distitten_demo PRIVATE
        distitten_core distitten_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests (and core-only command-line tools)
# ---------------------------------------------------------------------------
if(DISTITTEN_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(distitten_tests
        tests/TestEngine.cpp)
    target_include_directories(distitten_tests PRIVATE tests)
    target_link_libraries(distitten_tests PRIVATE distitten_core Catch2::Catch2WithMain)

    # Optional core-only CLI tool, JUCE-free so it builds in build-core
    # (PsyBox: psybox_render tools/RenderCategories.cpp; PhaseFucker:
    # phasefucker_align tools/AlignFiles.cpp):
    # add_executable(distitten_render tools/Render.cpp)
    # target_link_libraries(distitten_render PRIVATE distitten_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(distitten_tests)
endif()
