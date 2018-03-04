#!/bin/bash

pushd ./deps/opus
./autogen.sh
./configure --disable-shared
make -j4
popd
# https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/master/chromium/config/Chrome/linux/x64/config.h
./configure --disable-everything --disable-all --enable-avfilter --enable-swscale --pkg-config=true --extra-libs=-lopus --extra-cflags="-I./deps/opus/include" --extra-ldflags="-L./deps/opus/.libs" --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages --disable-static --enable-avcodec --enable-avformat --enable-avutil --enable-fft --enable-rdft --enable-static --enable-libopus --disable-bzlib --disable-error-resilience --disable-iconv --disable-lzo --disable-network --disable-schannel --disable-sdl2 --disable-symver --disable-xlib --disable-zlib --disable-securetransport --disable-faan --disable-alsa --disable-autodetect --enable-decoder='vorbis,libopus,flac' --enable-decoder='pcm_u8,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,mp3' --enable-decoder='pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw' --enable-demuxer='ogg,matroska,wav,flac,mp3,mov' --enable-parser='opus,vorbis,flac,mpegaudio' --disable-linux-perf --optflags='\"-O2\"' --enable-decoder='theora,vp8' --enable-parser='vp3,vp8' --enable-lto --enable-pic --enable-decoder='aac,h264' --enable-demuxer=aac --enable-parser='aac,h264'
make clean
make -j4
npm install
