#!/bin/sh
# 构建安卓 arm64（aarch64）引擎，产物安装到 $PWD/dist
# 含 modhub（内置 GMA 自动加载器）集成
set -e

git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export NDK_HOME=$PWD/android-ndk-r10e/
./waf configure -T debug --prefix=$PWD/dist --android=aarch64,4.9,21 --togles --disable-warns --build-games=hl2 &&
./waf build &&
./waf install
