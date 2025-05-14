#!/bin/bash

if [ -z "$ANDROID_NDK_HOME" ]; then
  echo "ERROR: ANDROID_NDK_HOME not defined"
  exit 1
fi

ABI=arm64-v8a
PLATFORM=android-21

mkdir -p build_android && cd build_android

cmake ../android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=$ABI \
  -DANDROID_PLATFORM=$PLATFORM \
  -DCMAKE_BUILD_TYPE=Release

make
