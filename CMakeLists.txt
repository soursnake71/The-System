cmake_minimum_required(VERSION 3.22)
# link the C++ runtime into the plugin so it doesn't need the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(FARP VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(FARP_WEB "Build the web-view editor (needs WebView2 on Windows)" ON)

set(FARP_FORMATS VST3 Standalone)

juce_add_plugin(FARP
    COMPANY_NAME "FARP"
    COMPANY_WEBSITE ""
    PRODUCT_NAME "The F.A.R.P."
    BUNDLE_ID "com.farp.thefarp"
    PLUGIN_MANUFACTURER_CODE Farp
    PLUGIN_CODE Frp2
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEBVIEW2 ${FARP_WEB}
    VST3_CATEGORIES Instrument Synth
    FORMATS ${FARP_FORMATS})

juce_generate_juce_header(FARP)
juce_add_binary_data(FARPWeb SOURCES web/index.html)

target_sources(FARP PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(FARP PRIVATE Source)

if(FARP_WEB)
    set(FARP_WEB_DEFS JUCE_WEB_BROWSER=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
else()
    set(FARP_WEB_DEFS JUCE_WEB_BROWSER=0)
endif()

target_compile_definitions(FARP PUBLIC
    ${FARP_WEB_DEFS}
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FARP
    PRIVATE
        FARPWeb
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(MSVC)
    target_compile_options(FARP PRIVATE /bigobj)
endif()

# headless check of the processor (local builds only)
option(FARP_TEST "Build the processor test" OFF)
if(FARP_TEST)
    juce_add_console_app(FarpTest PRODUCT_NAME "FarpTest")
    juce_generate_juce_header(FarpTest)
    target_sources(FarpTest PRIVATE test/processor_test.cpp)
    target_include_directories(FarpTest PRIVATE Source)
    target_compile_definitions(FarpTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FarpTest PRIVATE FARP FARPWeb juce::juce_audio_utils)
endif()
