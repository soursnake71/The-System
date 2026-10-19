# XL-Desk VST3 (and a Standalone app). Builds on Windows, macOS and Linux.
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
# JUCE is fetched automatically; pass -DJUCE_DIR=/path/to/JUCE to use a local copy.
cmake_minimum_required(VERSION 3.22)
project(XLDesk VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

include(cmake/juce-linux-webview-fix.cmake)
if(JUCE_DIR)
  xl_fix_juce_linux_webview(${JUCE_DIR})
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
  FetchContent_Populate(juce)
  xl_fix_juce_linux_webview(${juce_SOURCE_DIR})
  add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR})
endif()

juce_add_plugin(XLDesk
  COMPANY_NAME "Absolute Future Studios"
  COMPANY_WEBSITE "https://soursnake71.github.io/The-System/xldesk/"
  BUNDLE_ID com.absolutefuturestudios.xldesk
  PLUGIN_MANUFACTURER_CODE Afst
  PLUGIN_CODE Xlds
  FORMATS VST3 Standalone
  PRODUCT_NAME "XL-Desk"
  DESCRIPTION "SSL XL-Desk analogue console model"
  VST3_CATEGORIES Fx Tools
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  NEEDS_WEB_BROWSER TRUE
  NEEDS_WEBVIEW2 TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(XLDesk)

target_sources(XLDesk PRIVATE
  Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Engine.cpp Source/Modules.cpp)

# the console page itself, shared with the web version
set(WEB ${CMAKE_CURRENT_SOURCE_DIR}/..)
juce_add_binary_data(XLDeskWeb SOURCES
  ${WEB}/index.html ${WEB}/app.js ${WEB}/spec.js ${WEB}/help.js ${WEB}/midi.js ${WEB}/host.js ${WEB}/icon.svg ${WEB}/art/afs-logo.jpg)

target_compile_definitions(XLDesk PUBLIC
  JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(XLDesk
  PRIVATE XLDeskWeb juce::juce_audio_utils juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# engine unit test (no JUCE): ctest
enable_testing()
add_executable(engine_test tests/engine_test.cpp Source/Engine.cpp Source/Modules.cpp)
target_include_directories(engine_test PRIVATE Source)
add_test(NAME engine COMMAND engine_test)
