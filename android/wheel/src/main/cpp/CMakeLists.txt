# libwheel.so: the Omega Wheel of Steel engine for Android (com.thesnakedevice.wheel.Native).
# plugins/wheel/{Engine,Analysis,SeratoTags}.cpp (plain C++17, no JUCE) + the host and JNI / AAudio glue here.
cmake_minimum_required(VERSION 3.22.1)
project(wheel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# this directory is android/wheel/src/main/cpp; the repository root is five levels up
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(WHEEL_DIR "${REPO_ROOT}/plugins/wheel")

add_library(wheel SHARED
  wheel_jni.cpp
  wheel_host.cpp
  ${WHEEL_DIR}/Engine.cpp
  ${WHEEL_DIR}/Analysis.cpp
  ${WHEEL_DIR}/SeratoTags.cpp)
target_include_directories(wheel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WHEEL_DIR})
target_compile_options(wheel PRIVATE -Wall -Wextra -fvisibility=hidden
  $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O3>)
# AAudio (API 26+), logging; drop unused code, keep only the JNI entry points exported
target_link_libraries(wheel PRIVATE aaudio log android)
target_link_options(wheel PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)
# 16 KB page size (Android 15+ devices)
target_link_options(wheel PRIVATE -Wl,-z,max-page-size=16384)
