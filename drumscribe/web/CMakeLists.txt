# Drum Scribe in the browser: drumscribe/core compiled to WebAssembly with Emscripten.
#   drumscribe/web/build.sh   (emsdk 4.0.10; builds both variants into drumscribe/web/engine/)
# or by hand:
#   emcmake cmake -S drumscribe/web -B build-web-mt -DDS_WEB_THREADS=ON  && cmake --build build-web-mt
#   emcmake cmake -S drumscribe/web -B build-web-st -DDS_WEB_THREADS=OFF && cmake --build build-web-st
# drumscribe-mt: pthreads + SIMD128 (needs cross-origin isolation: coi-serviceworker.js provides it on GitHub Pages);
# drumscribe-st: SIMD128, one thread (the fallback when the page can't be isolated).
cmake_minimum_required(VERSION 3.22)
project(DrumScribeWeb LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "drumscribe/web is the WebAssembly build: configure it with emcmake (Emscripten)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(DS_WEB_THREADS "pthreads build (SharedArrayBuffer)" ON)

# every object of the module: SIMD128, native WebAssembly exceptions, and the same threading model
add_compile_options(-msimd128 -fwasm-exceptions)
if(DS_WEB_THREADS)
  add_compile_options(-pthread)
  set(_variant mt)
else()
  set(_variant st)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/core)

add_executable(drumscribe_wasm src/drumscribe_wasm.cpp)
target_link_libraries(drumscribe_wasm PRIVATE dsseparate)
target_include_directories(drumscribe_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)
target_compile_options(drumscribe_wasm PRIVATE -O3)
set_target_properties(drumscribe_wasm PROPERTIES OUTPUT_NAME drumscribe-${_variant} SUFFIX .js)
set(_exports _ds_error,_ds_alloc_floats,_ds_free_floats,_ds_set_audio,_ds_demucs,_ds_drumsep_begin,_ds_drumsep_input,_ds_drumsep_output,_ds_drumsep_add,_ds_drumsep_end,_ds_quick_kit,_ds_analyze,_ds_midi,_malloc,_free)
target_link_options(drumscribe_wasm PRIVATE
  -O3 -msimd128 -fwasm-exceptions
  -sMODULARIZE=1 -sEXPORT_NAME=DrumScribeModule -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB
  -sSTACK_SIZE=4MB -sFORCE_FILESYSTEM=1
  -sEXPORTED_FUNCTIONS=${_exports}
  -sEXPORTED_RUNTIME_METHODS=FS,ENV,UTF8ToString,stringToNewUTF8,wasmMemory)
if(DS_WEB_THREADS)
  # the pool is made before main() runs: threads can't be started later while the engine blocks the worker.
  # Module.dsThreads (set by worker.js) = the Demucs workers + DrumSep's iSTFT threads.
  target_link_options(drumscribe_wasm PRIVATE -pthread "-sPTHREAD_POOL_SIZE=Module['dsThreads']||4"
    -sDEFAULT_PTHREAD_STACK_SIZE=4MB)
endif()
