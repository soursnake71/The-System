# Neural source separation for Drum Scribe, all C++ at run time:
#   Demucs v4 (htdemucs / htdemucs_ft) via demucs.cpp (github.com/sevagh/demucs.cpp, MIT, Eigen)
#   MDX23C DrumSep via ONNX Runtime (prebuilt 1.20.1 release archive; on Android the official 1.20.0 Maven AAR)
# Defines the static library `dsseparate` (src/separate.cpp) and the optional check program `ds-sep-check`.
# Models are NOT part of the build: tools/get_models.sh downloads and converts them (default ~/.cache/drumscribe/models).
include(FetchContent)

if(ANDROID OR CMAKE_CROSSCOMPILING)
  set(_ds_native_default OFF)  # cross builds: no -march=native (arm64 Android has NEON by default)
else()
  set(_ds_native_default ON)
endif()
option(DS_SEP_NATIVE "Compile the separation maths with -march=native (fast here, but not portable to older CPUs)" ${_ds_native_default})
option(DS_SEP_CHECK "Build tools/sep_check.cpp (C++ vs PyTorch separation check)" ${_ds_native_default})

# ---- demucs.cpp: only its src/ is compiled (its own CMakeLists builds CLI apps, gtest, libnyquist, global flags).
# SOURCE_SUBDIR points at a folder without a CMakeLists.txt so MakeAvailable only downloads.
FetchContent_Declare(demucscpp
  GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
  GIT_TAG 4edae497a114de1ccb17ac9b4a2b8fc0144714d7
  GIT_SUBMODULES ""
  SOURCE_SUBDIR _no_cmake_)
# Eigen at the commit demucs.cpp pins as its vendor/eigen submodule
FetchContent_Declare(ds_eigen
  URL https://gitlab.com/libeigen/eigen/-/archive/dd8c71e62852b2fe429edb6682ac91fd1c578a26/eigen-dd8c71e62852b2fe429edb6682ac91fd1c578a26.tar.gz
  SOURCE_SUBDIR _no_cmake_ DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- ONNX Runtime prebuilt
set(_ort_ver 1.20.1)
set(_ort_base https://github.com/microsoft/onnxruntime/releases/download/v${_ort_ver})
if(ANDROID)
  # the AAR (a zip) holds headers/ and jni/<abi>/libonnxruntime.so; Maven Central first, then Google's mirror of it.
  # -DDS_ORT_ANDROID_AAR=/path/to/onnxruntime-android-1.20.0.aar uses a file already downloaded.
  set(_ort_ver 1.20.0)
  set(_ort_path com/microsoft/onnxruntime/onnxruntime-android/${_ort_ver}/onnxruntime-android-${_ort_ver}.aar)
  set(_ort_sha 07a8f71ef890afed8c6087a56220e6d558a492804276ee2dd7cb7f6262242027)
  if(DS_ORT_ANDROID_AAR)
    set(_ort_urls file://${DS_ORT_ANDROID_AAR})
  else()
    set(_ort_urls https://repo1.maven.org/maven2/${_ort_path}
                  https://maven-central.storage-download.googleapis.com/maven2/${_ort_path}
                  https://repo.maven.apache.org/maven2/${_ort_path})
  endif()
elseif(WIN32)
  set(_ort_pkg onnxruntime-win-x64-${_ort_ver}.zip)
  set(_ort_sha 78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581)
elseif(APPLE)
  set(_ort_pkg onnxruntime-osx-universal2-${_ort_ver}.tgz)
  set(_ort_sha da4349e01a7e997f5034563183c7183d069caadc1d95f499b560961787813efd)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  set(_ort_pkg onnxruntime-linux-aarch64-${_ort_ver}.tgz)
  set(_ort_sha ae4fedbdc8c18d688c01306b4b50c63de3445cdf2dbd720e01a2fa3810b8106a)
else()
  set(_ort_pkg onnxruntime-linux-x64-${_ort_ver}.tgz)
  set(_ort_sha 67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105)
endif()
if(ANDROID)
  FetchContent_Declare(ds_onnxruntime URL ${_ort_urls} URL_HASH SHA256=${_ort_sha}
    DOWNLOAD_NAME onnxruntime-android-${_ort_ver}.zip SOURCE_SUBDIR _no_cmake_ DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else()
  FetchContent_Declare(ds_onnxruntime URL ${_ort_base}/${_ort_pkg} URL_HASH SHA256=${_ort_sha}
    SOURCE_SUBDIR _no_cmake_ DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

FetchContent_MakeAvailable(demucscpp ds_eigen ds_onnxruntime)

# ONNX Runtime as an imported shared library
add_library(ds_onnxruntime SHARED IMPORTED GLOBAL)
if(ANDROID)
  set(DS_ONNXRUNTIME_LIB_DIR ${ds_onnxruntime_SOURCE_DIR}/jni/${ANDROID_ABI} CACHE INTERNAL "ONNX Runtime shared library")
  set_target_properties(ds_onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ds_onnxruntime_SOURCE_DIR}/headers
    IMPORTED_LOCATION ${DS_ONNXRUNTIME_LIB_DIR}/libonnxruntime.so)
else()
  set(DS_ONNXRUNTIME_LIB_DIR ${ds_onnxruntime_SOURCE_DIR}/lib CACHE INTERNAL "ONNX Runtime shared libraries (ship them with the programs)")
  set_target_properties(ds_onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ds_onnxruntime_SOURCE_DIR}/include)
  if(WIN32)
    set_target_properties(ds_onnxruntime PROPERTIES
      IMPORTED_LOCATION ${ds_onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll
      IMPORTED_IMPLIB ${ds_onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib)
  elseif(APPLE)
    set_target_properties(ds_onnxruntime PROPERTIES IMPORTED_LOCATION ${ds_onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib)
  else()
    set_target_properties(ds_onnxruntime PROPERTIES IMPORTED_LOCATION ${ds_onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so)
  endif()
endif()

# -march=native flags (the pinned Eigen doesn't compile with AVX512-FP16, so that extension is switched off)
set(DS_SEP_ARCH_FLAGS "")
if(DS_SEP_NATIVE AND NOT MSVC)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-march=native DS_HAVE_MARCH_NATIVE)
  check_cxx_compiler_flag(-mno-avx512fp16 DS_HAVE_NO_AVX512FP16)
  if(DS_HAVE_MARCH_NATIVE)
    list(APPEND DS_SEP_ARCH_FLAGS -march=native)
    if(DS_HAVE_NO_AVX512FP16)
      list(APPEND DS_SEP_ARCH_FLAGS -mno-avx512fp16)
    endif()
  endif()
endif()

# ---- demucs.cpp library (Eigen only; no OpenMP/BLAS: src/separate.cpp runs segments on its own threads)
file(GLOB _demucs_src ${demucscpp_SOURCE_DIR}/src/*.cpp)
add_library(ds_demucscpp STATIC ${_demucs_src})
target_include_directories(ds_demucscpp SYSTEM PUBLIC ${demucscpp_SOURCE_DIR}/src ${ds_eigen_SOURCE_DIR})
set_target_properties(ds_demucscpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(ds_demucscpp PUBLIC $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
if(MSVC)
  target_compile_options(ds_demucscpp PRIVATE /O2 /bigobj /w)
else()
  target_compile_options(ds_demucscpp PRIVATE -O3 -w ${DS_SEP_ARCH_FLAGS})
endif()

# ---- dsseparate
add_library(dsseparate STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/separate.cpp)
target_link_libraries(dsseparate PUBLIC dscore PRIVATE ds_demucscpp ds_onnxruntime Threads::Threads)
target_link_libraries(dsseparate INTERFACE ds_onnxruntime)  # shared lib: consumers need it at link + run time
if(NOT MSVC)
  target_compile_options(dsseparate PRIVATE -O3 ${DS_SEP_ARCH_FLAGS})
endif()

# Windows: copy onnxruntime.dll next to a program (Linux/macOS builds find it through the build RPATH; ship
# ${DS_ONNXRUNTIME_LIB_DIR}/libonnxruntime.so* / .dylib with installed programs)
function(ds_copy_onnxruntime tgt)
  if(WIN32)
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ds_onnxruntime> $<TARGET_FILE_DIR:${tgt}>)
  endif()
endfunction()

# ---- the heavy check (not a ctest: needs the models): C++ separation vs PyTorch reference stems, SNR + timing
if(DS_SEP_CHECK)
  add_executable(ds-sep-check ${CMAKE_CURRENT_SOURCE_DIR}/tools/sep_check.cpp)
  target_link_libraries(ds-sep-check PRIVATE dsseparate)
  target_include_directories(ds-sep-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  ds_copy_onnxruntime(ds-sep-check)
endif()
