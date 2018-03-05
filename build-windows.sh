#!/bin/bash

pushd ./deps/opus
./autogen.sh
CC=x86_64-w64-mingw32-gcc ./configure --host=x86_64-w64-mingw32 --disable-shared
CC=x86_64-w64-mingw32-gcc make clean
CC=x86_64-w64-mingw32-gcc make -j4
popd
# https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/master/chromium/config/Chrome/linux/x64/config.h
./configure --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- --disable-everything --disable-all --enable-shared --enable-avfilter --enable-swscale --enable-swresample --extra-libs=-lopus --extra-cflags="-I./deps/opus/include" --extra-ldflags="-L./deps/opus/.libs" --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages --enable-avcodec --enable-avformat --enable-avutil --enable-fft --enable-rdft --enable-libopus --disable-bzlib --disable-error-resilience --disable-iconv --disable-lzo --disable-network --disable-schannel --disable-sdl2 --disable-symver --disable-xlib --disable-zlib --disable-securetransport --disable-faan --disable-alsa --disable-autodetect --enable-decoder='vorbis,libopus,flac' --enable-decoder='pcm_u8,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,mp3' --enable-decoder='pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw' --enable-demuxer='ogg,matroska,wav,flac,mp3,mov' --enable-parser='opus,vorbis,flac,mpegaudio' --disable-linux-perf --optflags='\"-O2\"' --enable-decoder='theora,vp8' --enable-parser='vp3,vp8' --enable-pic --enable-decoder='aac,h264' --enable-demuxer=aac --enable-parser='aac,h264'
make clean
make -j4
# npm install
