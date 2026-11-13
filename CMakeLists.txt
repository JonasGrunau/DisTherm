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

# Release builds pass the tag's version, e.g. -DDISTHERM_VERSION=1.2.0.
set(DISTHERM_VERSION "0.1.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(DisTherm VERSION ${DISTHERM_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DISTHERM_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(DISTHERM_BUILD_TESTS "Build the DSP unit tests" ON)
option(DISTHERM_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: the DSP. Plain C++20, no JUCE, so the tests build in seconds and the
# audio code cannot reach into GUI types. Every .cpp is listed by hand (no
# globbing).
# ---------------------------------------------------------------------------
add_library(distherm_core STATIC
    src/core/model/Params.cpp
    src/core/engine/EffectSlot.cpp
    src/core/engine/Engine.cpp
)
target_include_directories(distherm_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(distherm_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(distherm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(DISTHERM_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    # The fonts src/ui/Theme.cpp loads (BuildApp/PhaseFucker: IBM Plex; PsyBox:
    # Inter + JetBrains Mono). Keep the licence file next to them in resources/fonts.
    # Thermal's factory presets and modulation shapes (presets/thermal), packed into one zip the plugin
    # carries and unpacks into its own folder on first use (src/plugin/ThermalImport.cpp).
    file(GLOB_RECURSE DISTHERM_FACTORY_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/thermal/*.xml")
    set(DISTHERM_FACTORY_ZIP "${CMAKE_CURRENT_BINARY_DIR}/factory.zip")
    add_custom_command(OUTPUT "${DISTHERM_FACTORY_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${DISTHERM_FACTORY_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${DISTHERM_FACTORY_ZIP}" --format=zip Presets SubPresets
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/presets/thermal"
        DEPENDS ${DISTHERM_FACTORY_FILES}
        VERBATIM)

    juce_add_binary_data(distherm_assets SOURCES
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-Medium.ttf
        "${DISTHERM_FACTORY_ZIP}")

    juce_add_plugin(DisTherm
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.distherm"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE DsTh
        FORMATS VST3 AU
        PRODUCT_NAME "DisTherm"
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
        # MICROPHONE_PERMISSION_TEXT "DisTherm listens to its inputs."
        # ---- SIDECHAIN: nothing here. The bus is declared in the processor's
        # BusesProperties (.withInput ("Reference", stereo, true)), see the audio-plugin skill (references/build-and-release.md).
        COPY_PLUGIN_AFTER_BUILD ${DISTHERM_COPY_PLUGIN})

    # Listed once, used by the plugin and the dev tools. The tools compile these
    # sources directly (not the plugin target), so a new src/plugin or src/ui
    # .cpp goes here and nowhere else.
    set(DISTHERM_UI_SOURCES
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

    target_sources(DisTherm PRIVATE ${DISTHERM_UI_SOURCES})
    target_include_directories(DisTherm PRIVATE src)

    target_compile_definitions(DisTherm PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(DisTherm
        PRIVATE
            distherm_core
            distherm_assets
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
    juce_add_console_app(distherm_snapshot PRODUCT_NAME "distherm_snapshot")
    target_sources(distherm_snapshot PRIVATE tools/Snapshot.cpp ${DISTHERM_UI_SOURCES})
    target_include_directories(distherm_snapshot PRIVATE src tests)
    target_compile_definitions(distherm_snapshot PRIVATE
        JucePlugin_Name="DisTherm"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distherm_snapshot PRIVATE
        distherm_core distherm_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: renders audio through DisTherm with an Output Thermal preset loaded, for A/B checks
    # against Thermal (tools/ThermalRender.cpp).
    juce_add_console_app(distherm_render PRODUCT_NAME "distherm_render")
    target_sources(distherm_render PRIVATE tools/ThermalRender.cpp ${DISTHERM_UI_SOURCES})
    target_include_directories(distherm_render PRIVATE src tests)
    target_compile_definitions(distherm_render PRIVATE
        JucePlugin_Name="DisTherm"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distherm_render PRIVATE
        distherm_core distherm_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: the plugin in a window with a built-in psytrance loop (or a
    # file) playing through it. ./start.sh runs it.
    # (An instrument has no demo host: PsyBox's start.sh opens its Standalone.)
    juce_add_gui_app(distherm_demo PRODUCT_NAME "DisTherm Demo")
    target_sources(distherm_demo PRIVATE tools/Demo.cpp ${DISTHERM_UI_SOURCES})
    target_include_directories(distherm_demo PRIVATE src tests)
    target_compile_definitions(distherm_demo PRIVATE
        JucePlugin_Name="DisTherm"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(distherm_demo PRIVATE
        distherm_core distherm_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests (and core-only command-line tools)
# ---------------------------------------------------------------------------
if(DISTHERM_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(distherm_tests
        tests/TestEngine.cpp)
    target_include_directories(distherm_tests PRIVATE tests)
    target_link_libraries(distherm_tests PRIVATE distherm_core Catch2::Catch2WithMain)

    # Optional core-only CLI tool, JUCE-free so it builds in build-core
    # (PsyBox: psybox_render tools/RenderCategories.cpp; PhaseFucker:
    # phasefucker_align tools/AlignFiles.cpp):
    # add_executable(distherm_render tools/Render.cpp)
    # target_link_libraries(distherm_render PRIVATE distherm_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(distherm_tests)
endif()
