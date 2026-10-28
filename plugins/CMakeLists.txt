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

# a plain C++ test runner for the DSP engines (no JUCE), used by tools/check
add_executable(omega_dsp_test tools/dsp_test.cpp)

# Omega Twelve: the 12-bit sampler emulator (twelve/ in the app). Two builds from one source: an effect that also
# plays samples from MIDI (plus a standalone app), and an instrument for hosts that only send MIDI to instruments.
foreach(variant "Twelve|Omega Twelve|Otw1|0" "TwelveInst|Omega Twelve Instrument|Otw2|1")
  string(REPLACE "|" ";" f "${variant}")
  list(GET f 0 tgt)
  list(GET f 1 pname)
  list(GET f 2 pcode)
  list(GET f 3 inst)
  if(OMEGA_ONLY AND NOT tgt IN_LIST OMEGA_ONLY)
    continue()
  endif()
  set(fmts ${OMEGA_FORMATS})
  if(inst)
    set(cats Instrument Sampler)
    set(ausub kAudioUnitType_MusicDevice)
  else()
    list(APPEND fmts Standalone)
    set(cats Fx Sampler)
    set(ausub kAudioUnitType_MusicEffect)
  endif()
  juce_add_plugin(Omega${tgt}
    PRODUCT_NAME "${pname}"
    COMPANY_NAME "The Omega System"
    BUNDLE_ID "com.thesnakedevice.omega.${tgt}"
    PLUGIN_MANUFACTURER_CODE Omga
    PLUGIN_CODE ${pcode}
    FORMATS ${fmts}
    IS_SYNTH ${inst}
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES ${cats}
    AU_MAIN_TYPE ${ausub}
    MICROPHONE_PERMISSION_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(Omega${tgt} PRIVATE twelve/TwelvePlugin.cpp twelve/TwelveEditor.cpp)
  target_compile_definitions(Omega${tgt} PUBLIC TWELVE_INSTRUMENT=${inst} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_WINDOWS_MEDIA_FORMAT=1)
  target_link_libraries(Omega${tgt} PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endforeach()

# Twelve.h against engine.js: node twelve/tools/compare.mjs <build>/twelve_test <dir>
add_executable(twelve_test twelve/test.cpp)

# drives the Twelve processor directly (sampler, MIDI, exports, analysis): <build>/twelve_proc_test <dir>
if(TARGET OmegaTwelve)
  juce_add_console_app(twelve_proc_test PRODUCT_NAME "twelve_proc_test")
  target_sources(twelve_proc_test PRIVATE twelve/proctest.cpp twelve/TwelvePlugin.cpp twelve/TwelveEditor.cpp)
  target_compile_definitions(twelve_proc_test PRIVATE TWELVE_INSTRUMENT=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Omega Twelve"
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
  target_link_libraries(twelve_proc_test PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  set_target_properties(twelve_proc_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
