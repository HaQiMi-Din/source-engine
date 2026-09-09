#!/bin/sh
# 构建安卓 arm64（aarch64）引擎，产物安装到 $PWD/dist
# 含 modhub（内置 GMA 自动加载器）集成
set -e

git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export NDK_HOME=$PWD/android-ndk-r10e/
./waf configure -T debug --prefix=$PWD/dist --android=aarch64,4.9,21 --togles --disable-warns --build-games=hl2 || {
    echo "===== config.log (full) ====="
    cat build/config.log 2>/dev/null || true
    exit 1
}
./waf build &&
./waf install &&
# ModHub fork: 共享 libstdc++ 运行时，必须随产物分发，
# 否则各 .so 的 C++ 异常/RTTI 符号运行时无宿主。
# libstdc++.so 是 GCC 驱动自动写进 DT_NEEDED 的兼容层，
# Android 系统不提供，必须一并打进 APK 才能 dlopen。
cp android-ndk-r10e/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/libgnustl_shared.so dist/lib/arm64-v8a/
cp android-ndk-r10e/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/libstdc++.so dist/lib/arm64-v8a/ 2>/dev/null || true
