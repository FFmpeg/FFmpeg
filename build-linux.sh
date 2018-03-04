#!/bin/bash

./configure --disable-everything --disable-all --disable-doc --disable-static --enable-avcodec --enable-avformat --enable-avutil --enable-fft --enable-rdft --enable-avfilter --enable-swscale --enable-shared --disable-bzlib --disable-error-resilience --disable-iconv --disable-lzo --disable-network --disable-symver --disable-xlib --disable-zlib --disable-dxva2 --disable-vaapi --disable-vdpau --enable-decoder='theora,vorbis,vp8' --enable-decoder='pcm_u8,pcm_s16le,pcm_s24le,pcm_f32le' --enable-decoder='pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw' --enable-demuxer='ogg,matroska,wav' --enable-parser='opus,vp3,vorbis,vp8' --optflags='\"-O2\"' --enable-pic --enable-decoder='aac,h264,mp3' --enable-demuxer='aac,mp3,mov' --enable-parser='aac,h264,mpegaudio' # https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/branch-m40/chromium/config/Chrome/linux/x64/config.h

make clean
make -j4
npm install
