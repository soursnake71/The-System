# libdrumscribe.so: the Drum Scribe engine (drumscribe/core: separation + analysis + MIDI) and its JNI bridge.
# libonnxruntime.so (from the official ONNX Runtime Android AAR, see core/cmake/separation.cmake) is packaged next to it.
cmake_minimum_required(VERSION 3.22)
project(DrumScribeAndroid LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(DS_WITH_SEPARATION ON CACHE BOOL "" FORCE)

set(DS_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core)
add_subdirectory(${DS_CORE} core)

add_library(drumscribe SHARED drumscribe_jni.cpp)
target_include_directories(drumscribe PRIVATE ${DS_CORE}/src)
target_compile_definitions(drumscribe PRIVATE DS_HAVE_SEPARATION=1)
target_compile_options(drumscribe PRIVATE -O2)
target_link_libraries(drumscribe PRIVATE dsseparate dscore log android)
# 16 KB page size devices (Android 15+)
target_link_options(drumscribe PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
