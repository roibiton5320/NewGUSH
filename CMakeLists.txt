# =============================================================================
#  GUSH — granular tape echo / resonator / shimmer
#
#  Builds VST3 + Standalone everywhere, and AU as well on macOS.
#  JUCE is fetched automatically, so a fresh clone needs nothing installed
#  beyond CMake and a compiler.
#
#      cmake -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --parallel
#
#  The DSP smoke test builds alongside it and can be run on its own:
#      ./build/gush_dsp_test
# =============================================================================

cmake_minimum_required(VERSION 3.22)

project(GUSH VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# -----------------------------------------------------------------------------
#  JUCE
# -----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(JUCE)

# -----------------------------------------------------------------------------
#  Sources
#
#  Everything in Source/DSP is plain C++17 with no JUCE dependency, so it is
#  listed separately: the test target compiles exactly the same files the
#  plugin does, with nothing stubbed out.
# -----------------------------------------------------------------------------
set(GUSH_DSP_SOURCES
    Source/DSP/Engine.cpp
    Source/DSP/GranularTapeEcho.cpp
    Source/DSP/ModalResonator.cpp
    Source/DSP/Reverb.cpp)

set(GUSH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GUSH_FORMATS AU)
endif()

juce_add_plugin(GUSH
    COMPANY_NAME                "Roi Biton"
    BUNDLE_ID                   "com.roibiton.gush"
    PLUGIN_MANUFACTURER_CODE    Rbtn
    PLUGIN_CODE                 Gsh1
    FORMATS                     ${GUSH_FORMATS}
    PRODUCT_NAME                "GUSH"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE)

target_sources(GUSH PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    ${GUSH_DSP_SOURCES})

target_compile_definitions(GUSH PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # NOTE: the JUCE splash screen is left enabled on purpose. Turning it off
    # requires a paid JUCE licence — flip JUCE_DISPLAY_SPLASH_SCREEN=0 here
    # once one is in place.
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(GUSH
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
#  DSP smoke test — no JUCE, no DAW, runs anywhere
# -----------------------------------------------------------------------------
option(GUSH_BUILD_TESTS "Build the standalone DSP smoke test" ON)

if(GUSH_BUILD_TESTS)
    add_executable(gush_dsp_test Tools/dsp_smoke_test.cpp ${GUSH_DSP_SOURCES})
    target_compile_features(gush_dsp_test PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(gush_dsp_test PRIVATE /W4)
    else()
        target_compile_options(gush_dsp_test PRIVATE -Wall -Wextra)
    endif()

    enable_testing()
    add_test(NAME dsp_smoke COMMAND gush_dsp_test)

    # Renders demo WAVs through the real engine, no DAW required:
    #     ./build/gush_render demos/
    add_executable(gush_render Tools/render_demo.cpp ${GUSH_DSP_SOURCES})
    target_compile_features(gush_render PRIVATE cxx_std_17)
endif()
