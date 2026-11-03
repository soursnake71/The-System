cmake_minimum_required(VERSION 3.22)
project(omegacmi CXX)
add_library(omegacmi SHARED native.cpp)
# the engine shared with the VST3
target_include_directories(omegacmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugins/cmi/core)
target_compile_options(omegacmi PRIVATE -O2 -ffast-math -fno-finite-math-only)
target_link_libraries(omegacmi aaudio log android)
