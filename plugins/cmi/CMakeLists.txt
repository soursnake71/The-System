# Omega CMI on its own (this folder is enough):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target OmegaCMI_All
# Builds the VST3 (and AU on macOS) instrument and the standalone app, plus the tests (cmi_test, cmi_snapshot).
# The repository's plugins/CMakeLists.txt builds the same targets alongside the other Omega plugins.
cmake_minimum_required(VERSION 3.22)
project(OmegaCMI VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# JUCE: a local checkout (-DJUCE_DIR=...) or fetched
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
juce_add_plugin(OmegaCMI
  PRODUCT_NAME "Omega CMI"
  COMPANY_NAME "The Omega System"
  BUNDLE_ID "com.thesnakedevice.omega.CMI"
  PLUGIN_MANUFACTURER_CODE Omga
  PLUGIN_CODE Ocmi
  FORMATS ${fmts}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Instrument Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(OmegaCMI PRIVATE CmiPlugin.cpp CmiEditor.cpp)
target_compile_definitions(OmegaCMI PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
target_link_libraries(OmegaCMI PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# the engine on its own: cmi_test <dir>
add_executable(cmi_test tools/test.cpp)

# the plugin driven directly, a picture of every page: cmi_snapshot <dir> [disk images...]
juce_add_console_app(cmi_snapshot PRODUCT_NAME "cmi_snapshot")
target_sources(cmi_snapshot PRIVATE tools/snapshot.cpp CmiPlugin.cpp CmiEditor.cpp)
target_compile_definitions(cmi_snapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Omega CMI"
  JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
target_link_libraries(cmi_snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
set_target_properties(cmi_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
