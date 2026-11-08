# Omega Synsonics: Mattel Synsonics Drums circuit model, stock and Circuitbenders-modded, as a VST3 (AU on macOS)
# and a standalone app. The Android app is in android/ and builds the same core/ sources.
#   cmake -S synsonics -B build-syn -DCMAKE_BUILD_TYPE=Release && cmake --build build-syn --config Release
#   -DSYN_PLUGIN=OFF builds only the tests (no JUCE download).
cmake_minimum_required(VERSION 3.22)
project(OmegaSynsonics VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()
option(SYN_PLUGIN "Build the plugin (fetches JUCE)" ON)

# the shared core that isn't header-only: file import/export with the MP3 libraries (third_party/)
file(GLOB SHINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shine/*.c)
add_library(syn_core STATIC core/Files.cpp ${SHINE_SOURCES})
set_target_properties(syn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(syn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(NOT MSVC)
  set_source_files_properties(${SHINE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# tests: the circuit model, sequencer, mixer, effects, files and the panel renderer
add_executable(syn_engine_test tests/engine_test.cpp)
add_executable(syn_features_test tests/features_test.cpp)
target_link_libraries(syn_features_test PRIVATE syn_core)
add_executable(syn_panel_snapshot tests/panel_snapshot.cpp)
target_link_libraries(syn_panel_snapshot PRIVATE syn_core)

if(SYN_PLUGIN)
  if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()
  set(fmts VST3 Standalone)
  if(APPLE)
    list(APPEND fmts AU)
  endif()
  juce_add_plugin(OmegaSynsonics
    PRODUCT_NAME "Omega Synsonics"
    COMPANY_NAME "The Omega System"
    BUNDLE_ID "com.thesnakedevice.omega.synsonics"
    PLUGIN_MANUFACTURER_CODE Omga
    PLUGIN_CODE Osyn
    FORMATS ${fmts}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(OmegaSynsonics PRIVATE plugin/SynsonicsPlugin.cpp)
  target_compile_definitions(OmegaSynsonics PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(OmegaSynsonics PRIVATE syn_core juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()
