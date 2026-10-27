# The Omega System as VST3 plugins (and AU on macOS): each instrument and effect from the app as its own plugin.
# Build: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
cmake_minimum_required(VERSION 3.22)
project(OmegaPlugins VERSION 1.0.0 LANGUAGES C CXX)
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

set(OMEGA_FORMATS VST3)
if(APPLE)
  list(APPEND OMEGA_FORMATS AU)
endif()

# Only build some: -DOMEGA_ONLY="Prophet5;Vox"
set(OMEGA_PLUGINS
  # target       display name             code  instrument  midi-in
  "Prophet5|Omega Prophet-5|Op5a|1|1"
  "Vox|Omega Vox|Ovox|1|1"
  "Speech|Omega Speech|Ospk|1|1"
  "Vocoder|Omega Vocoder 350|Ovoc|0|1"
  "Lexicon224|Omega Lexicon 224|Olx2|0|0"
  "PrimeTime|Omega PrimeTime|Opt9|0|0"
  "Effectron|Omega Effectron|Oefx|0|0"
  "DP4|Omega DP4|Odp4|0|0"
  "Microverb|Omega Microverb|Omvb|0|0"
  "ADAFlanger|Omega ADA Flanger|Oada|0|0"
  "UREI1176|Omega 1176|O176|0|0"
  "PhilCollins|Omega Phil Collins Sim|Opcs|0|0"
  "H910|Omega H910|Oh91|0|0"
  "MXR113|Omega MXR 113|Omx1|0|0"
  "BusComp|Omega Bus Compressor|Obus|0|0"
  "Soother|Omega Resonance Tamer|Osth|0|0"
  "GateStepper|Omega Gate Stepper|Ogat|0|0"
)

foreach(entry ${OMEGA_PLUGINS})
  string(REPLACE "|" ";" f "${entry}")
  list(GET f 0 tgt)
  list(GET f 1 pname)
  list(GET f 2 pcode)
  list(GET f 3 isSynth)
  list(GET f 4 midiIn)
  if(OMEGA_ONLY AND NOT tgt IN_LIST OMEGA_ONLY)
    continue()
  endif()
  if(isSynth)
    set(cats Instrument Synth)
    set(ausub kAudioUnitType_MusicDevice)
  else()
    set(cats Fx)
    set(ausub kAudioUnitType_Effect)
    if(midiIn)
      set(ausub kAudioUnitType_MusicEffect)
    endif()
  endif()
  juce_add_plugin(Omega${tgt}
    PRODUCT_NAME "${pname}"
    COMPANY_NAME "The Omega System"
    BUNDLE_ID "com.thesnakedevice.omega.${tgt}"
    PLUGIN_MANUFACTURER_CODE Omga
    PLUGIN_CODE ${pcode}
    FORMATS ${OMEGA_FORMATS}
    IS_SYNTH ${isSynth}
    NEEDS_MIDI_INPUT ${midiIn}
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES ${cats}
    AU_MAIN_TYPE ${ausub}
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(Omega${tgt} PRIVATE common/Plugin.cpp common/Editor.cpp src/${tgt}.cpp)
  target_compile_definitions(Omega${tgt} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(Omega${tgt} PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endforeach()

# SPQuencer1200: the 24 PPQ SP-1200 sequencer (E-mu and Rossum) as an instrument with MIDI out, its editor the web
# page in ../spquencer shown in a web view (WebView2 on Windows, WKWebView on Mac, WebKitGTK on Linux)
if(NOT OMEGA_ONLY OR "SPQuencer" IN_LIST OMEGA_ONLY)
  juce_add_binary_data(SPQuencerWeb SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../spquencer/index.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../spquencer/app.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../spquencer/icon.svg)
  set(spq_webview2 FALSE)
  if(WIN32)
    set(spq_webview2 TRUE)
  endif()
  juce_add_plugin(SPQuencer1200
    PRODUCT_NAME "SPQuencer1200"
    COMPANY_NAME "The Omega System"
    BUNDLE_ID "com.thesnakedevice.omega.spquencer1200"
    PLUGIN_MANUFACTURER_CODE Omga
    PLUGIN_CODE Spq1
    FORMATS ${OMEGA_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 ${spq_webview2}
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(SPQuencer1200 PRIVATE spquencer/SPQuencer.cpp)
  target_compile_definitions(SPQuencer1200 PUBLIC JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
  target_link_libraries(SPQuencer1200 PRIVATE SPQuencerWeb juce::juce_audio_utils juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

# a plain C++ test runner for the DSP engines (no JUCE), used by tools/check
add_executable(omega_dsp_test tools/dsp_test.cpp)
