# the engine and the C64 screen are the plugin's own headers (plugins/sid)
cmake_minimum_required(VERSION 3.22)
project(omegasid CXX)
add_library(omegasid SHARED jni.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/sid/Rev7Spec.cpp)   # the Omega REV7 as the reverb
target_include_directories(omegasid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/sid)
target_compile_options(omegasid PRIVATE -O3 -Wall)
target_link_libraries(omegasid aaudio log android)
