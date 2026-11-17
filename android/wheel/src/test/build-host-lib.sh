#!/usr/bin/env bash
# Builds libwheel.so for this Linux machine (the real engine and JNI code, with AAudio / liblog stand-ins from
# cpp/aaudio_stub.cpp) so the Robolectric launch test runs the app with its engine:
#   android/wheel/src/test/build-host-lib.sh <out dir>
#   gradle -p android :wheel:testDebugUnitTest -PwheelHostLib=<out dir>     (screenshots: -PwheelShots=<dir>)
# Needs clang++, a JDK and the Android NDK (ANDROID_HOME or ANDROID_NDK_HOME) for the AAudio / log headers.
set -euo pipefail
out=${1:?out dir}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../../../.." && pwd)
ndk=${ANDROID_NDK_HOME:-$(ls -d "${ANDROID_HOME:?set ANDROID_HOME}"/ndk/* | sort | tail -1)}
inc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
jdk=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
mkdir -p "$out/inc/aaudio" "$out/inc/android"
cp "$inc/aaudio/AAudio.h" "$out/inc/aaudio/"; cp "$inc/android/log.h" "$out/inc/android/"
c="$repo/android/wheel/src/main/cpp"; w="$repo/plugins/wheel"
clang++ -std=c++17 -O1 -g -fPIC -shared -w "-D__INTRODUCED_IN(x)=" "-D__DEPRECATED_IN(x,...)=" "-D__REMOVED_IN(x,...)=" -D__ANDROID_API__=35 \
  -I"$out/inc" -I"$jdk/include" -I"$jdk/include/linux" -I"$w" -I"$c" -o "$out/libwheel.so" \
  "$c/wheel_jni.cpp" "$c/wheel_host.cpp" "$w/Engine.cpp" "$w/Analysis.cpp" "$w/SeratoTags.cpp" "$here/cpp/aaudio_stub.cpp" -lpthread
echo "$out/libwheel.so"
