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

# -DOMEGA_STATIC_CXX=ON: link the C++ runtime into each plugin (for a bare device whose libstdc++ may be older, such
# as an SD-card OS on the MPC Sample)
option(OMEGA_STATIC_CXX "Link libstdc++ / libgcc statically" OFF)
if(OMEGA_STATIC_CXX AND UNIX AND NOT APPLE)
  add_link_options(-static-libstdc++ -static-libgcc)
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

# Omega Wheel of Steel: two turntables and a DJ mixer as an instrument, played from an MPC (Q-Links, pads) or any
# MIDI controller; loads crates (Serato or folders) from an SD card / drive. Also a standalone app.
if(NOT OMEGA_ONLY OR "Wheel" IN_LIST OMEGA_ONLY)
  set(wfmts ${OMEGA_FORMATS})
  list(APPEND wfmts Standalone)
  juce_add_plugin(OmegaWheel
    PRODUCT_NAME "Omega Wheel of Steel"
    COMPANY_NAME "The Omega System"
    BUNDLE_ID "com.thesnakedevice.omega.Wheel"
    PLUGIN_MANUFACTURER_CODE Omga
    PLUGIN_CODE Owhl
    FORMATS ${wfmts}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
  # the screen's font, built in (the MPC Sample has no fonts installed)
  juce_add_binary_data(WheelFonts HEADER_NAME WheelFonts.h NAMESPACE WheelFonts SOURCES wheel/fonts/DejaVuSans-Bold.ttf)
  set_target_properties(WheelFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_sources(OmegaWheel PRIVATE wheel/Engine.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp wheel/Library.cpp wheel/WheelPlugin.cpp wheel/WheelEditor.cpp wheel/PhoneLibraryView.cpp wheel/PhoneDeckView.cpp)
  target_link_libraries(OmegaWheel PRIVATE WheelFonts)
  if(UNIX AND NOT APPLE)
    # libraries shipped inside the .vst3 next to the .so (CI copies freetype / fontconfig there for the MPC Sample)
    set_target_properties(OmegaWheel_VST3 PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
  endif()
  target_compile_definitions(OmegaWheel PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_WINDOWS_MEDIA_FORMAT=1)
  target_link_libraries(OmegaWheel PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

  # the deck / mixer / FX engine on its own (no JUCE): <build>/wheel_engine_test
  add_executable(wheel_engine_test wheel/tests/engine_test.cpp wheel/Engine.cpp)
  # the Android engine host (everything behind Native.java but JNI / AAudio): <build>/wheel_host_test
  add_executable(wheel_host_test wheel/tests/jni_engine_test.cpp ../android/wheel/src/main/cpp/wheel_host.cpp wheel/Engine.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp)
  target_include_directories(wheel_host_test PRIVATE wheel ../android/wheel/src/main/cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(wheel_host_test PRIVATE Threads::Threads)
  # crates, Serato tags, decoding and analysis: <build>/wheel_lib_test
  juce_add_console_app(wheel_lib_test PRODUCT_NAME "wheel_lib_test")
  target_sources(wheel_lib_test PRIVATE wheel/tests/lib_test.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp wheel/Library.cpp)
  target_compile_definitions(wheel_lib_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
  target_link_libraries(wheel_lib_test PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
  # the processor's MIDI defaults (fader = crossfader), parameter order and mapping state: <build>/wheel_midi_test
  juce_add_console_app(wheel_midi_test PRODUCT_NAME "wheel_midi_test")
  target_sources(wheel_midi_test PRIVATE wheel/tests/midi_test.cpp wheel/Engine.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp wheel/Library.cpp wheel/WheelPlugin.cpp wheel/WheelEditor.cpp wheel/PhoneLibraryView.cpp wheel/PhoneDeckView.cpp)
  target_link_libraries(wheel_midi_test PRIVATE WheelFonts)
  target_compile_definitions(wheel_midi_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Omega Wheel of Steel" JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
  target_link_libraries(wheel_midi_test PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
  # the phone-style library screen rendered to PNGs from a generated music tree (xvfb-run -a <build>/wheel_library_snapshot <dir>)
  juce_add_console_app(wheel_library_snapshot PRODUCT_NAME "wheel_library_snapshot")
  target_sources(wheel_library_snapshot PRIVATE wheel/tests/library_snapshot.cpp wheel/Engine.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp wheel/Library.cpp wheel/WheelPlugin.cpp wheel/WheelEditor.cpp wheel/PhoneLibraryView.cpp)
  target_link_libraries(wheel_library_snapshot PRIVATE WheelFonts)
  target_compile_definitions(wheel_library_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Omega Wheel of Steel" JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
  target_link_libraries(wheel_library_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  set_target_properties(wheel_library_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
  # the phone-style performance screen rendered to PNGs with two playing tracks (xvfb-run -a <build>/wheel_phone_snapshot <dir>)
  juce_add_console_app(wheel_phone_snapshot PRODUCT_NAME "wheel_phone_snapshot")
  set_target_properties(wheel_phone_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
  target_sources(wheel_phone_snapshot PRIVATE wheel/tests/phone_snapshot.cpp wheel/Engine.cpp wheel/Analysis.cpp wheel/SeratoTags.cpp wheel/Library.cpp wheel/WheelPlugin.cpp wheel/WheelEditor.cpp wheel/PhoneDeckView.cpp wheel/PhoneLibraryView.cpp)
  target_link_libraries(wheel_phone_snapshot PRIVATE WheelFonts)
  target_compile_definitions(wheel_phone_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Omega Wheel of Steel" JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
  target_link_libraries(wheel_phone_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

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
