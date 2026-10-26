# Sky Sequencer as a VST3 / AU / standalone app (JUCE).
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
# JUCE is downloaded automatically; pass -DJUCE_DIR=/path/to/JUCE to use a local copy.
cmake_minimum_required(VERSION 3.22)
project(SkySequencer VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(SkySequencer
  COMPANY_NAME "The Snake Device"
  COMPANY_WEBSITE "https://github.com/soursnake71/the-system"
  BUNDLE_ID com.thesnakedevice.skysequencer
  PLUGIN_MANUFACTURER_CODE Snak
  PLUGIN_CODE SkyS
  PRODUCT_NAME "Sky Sequencer"
  DESCRIPTION "Live air traffic turned into music"
  FORMATS VST3 AU Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  NEEDS_CURL TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Generator
  AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(SkySequencer PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Sky.cpp Source/Presets.cpp)
# drum patterns and the 50 factory presets, shared with the web app and APK
juce_add_binary_data(SkyData SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../sky/presets.json)
target_compile_definitions(SkySequencer PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)
target_link_libraries(SkySequencer
  PRIVATE juce::juce_audio_utils SkyData
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
