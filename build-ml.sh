#!/bin/bash

export MLSDK='/mnt/c/Users/avaer/MagicLeap/mlsdk/v0.16.0'

../magicleap-js/hack-toolchain.js

pushd ./deps/opus
# export PATH="$PATH:$MLSDK"
export CC="$MLSDK/tools/toolchains/bin/aarch64-linux-android-gcc"
export CXX="$MLSDK/tools/toolchains/bin/aarch64-linux-android-g++"
export LD="$MLSDK/tools/toolchains/bin/aarch64-linux-android-ld"
export AR="$MLSDK/tools/toolchains/bin/aarch64-linux-android-ar"
rm Makefile
find | grep '\.Plo$' | xargs rm -Rf
find | grep '\.Po$' | xargs rm -Rf
./autogen.sh
CC="$MLSDK/tools/toolchains/bin/aarch64-linux-android-gcc" ./configure --host=aarch64-linux-android --disable-shared
find . -type f -iname '*.Plo' -exec sed -i 's/C:\\/\/mnt\/c\//g' "{}" +;
find . -type f -iname '*.Plo' -exec sed -i 's/\([^ ]\)\\/\1\//g' "{}" +;
find . -type f -iname '*.Po' -exec sed -i 's/C:\\/\/mnt\/c\//g' "{}" +;
find . -type f -iname '*.Po' -exec sed -i 's/\([^ ]\)\\/\1\//g' "{}" +;
CC="$MLSDK/tools/toolchains/bin/aarch64-linux-android-gcc" make clean
CC="$MLSDK/tools/toolchains/bin/aarch64-linux-android-gcc" make -j4
popd

# https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/master/chromium/config/Chrome/linux/x64/config.h
./configure --disable-everything --disable-all --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages --disable-static --enable-avcodec --enable-avformat --enable-avutil --enable-fft --enable-rdft --enable-static --enable-libopus --disable-debug --disable-bzlib --disable-error-resilience --disable-iconv --disable-lzo --disable-network --disable-schannel --disable-sdl2 --disable-symver --disable-xlib --disable-zlib --disable-securetransport --disable-faan --disable-alsa --disable-autodetect --enable-decoder='vorbis,libopus,flac' --enable-decoder='pcm_u8,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,mp3' --enable-decoder='pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw' --enable-demuxer='ogg,matroska,wav,flac,mp3,mov' --enable-parser='opus,vorbis,flac,mpegaudio' \
--disable-linux-perf --x86asmexe=yasm --enable-small --enable-cross-compile \
--sysroot="$MLSDK/lumin" \
--extra-cflags="-I$MLSDK/lumin/stl/libc++/include" \
--extra-cflags="-I$MLSDK/lumin/usr/include" \
--extra-cflags=-I./deps/opus/include \
--extra-cflags='--target=aarch64-linux-android' \
--extra-cflags='-DLUMIN' \
--extra-ldflags='--target=aarch64-linux-android' \
--extra-ldflags="--gcc-toolchain=$MLSDK/tools/toolchains/bin" \
--target-os=android --arch=aarch64 --enable-armv8 --extra-cflags='-march=armv8-a' --enable-pic --enable-avfilter --enable-swscale --enable-swresample --enable-avdevice \
--cc="$MLSDK/tools/toolchains/bin/aarch64-linux-android-gcc" \
--cxx="$MLSDK/tools/toolchains/bin/aarch64-linux-android-g++" \
--ld="$MLSDK/tools/toolchains/bin/aarch64-linux-android-ld" \
--enable-demuxer=aac --enable-parser=aac --enable-decoder=aac

# rm -f */*.d */*/*.d

find . -type f -iname '*.d' -exec sed -i 's/C:\\/\/mnt\/c\//g' "{}" +;
find . -type f -iname '*.d' -exec sed -i 's/\([^ ]\)\\/\1\//g' "{}" +;

make clean
make -j4