# Headless check of the built plugins: load each .vst3 like a DAW, play notes / audio through it, save a picture of its window.
cmake_minimum_required(VERSION 3.22)
project(OmegaHostTest VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} JUCE)
juce_add_console_app(omega_host_test PRODUCT_NAME "omega_host_test")
target_sources(omega_host_test PRIVATE main.cpp)
target_compile_definitions(omega_host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(omega_host_test PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics juce::juce_recommended_config_flags)

# SPQuencer1200: MIDI out timing in segment, song and notes modes
juce_add_console_app(spq_test PRODUCT_NAME "spq_test")
target_sources(spq_test PRIVATE spq_test.cpp)
target_compile_definitions(spq_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(spq_test PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
