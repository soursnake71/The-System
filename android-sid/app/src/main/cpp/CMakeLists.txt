# the engine and the C64 screen are the plugin's own headers (plugins/sid)
cmake_minimum_required(VERSION 3.22)
project(omegasid C CXX)
add_library(omegasid SHARED jni.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/sid/Rev7Spec.cpp)   # the Omega REV7 as the reverb
target_include_directories(omegasid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/sid)
target_compile_options(omegasid PRIVATE -O3 -Wall)
# shine: the MP3 encoder (plugins/third_party/shine, LGPL), two short global names renamed
file(GLOB SHINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/third_party/shine/*.c)
add_library(shine STATIC ${SHINE_SOURCES})
target_compile_definitions(shine PRIVATE dmask=shine_dmask hs=shine_hs)
target_compile_options(shine PRIVATE -w -O2)
target_link_libraries(omegasid shine aaudio log android)
