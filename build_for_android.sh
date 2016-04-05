#!/bin/bash

export ANDROID_NDK=/home/aid/workspace/tools/android-ndk
export NDK=$ANDROID_NDK
#export SYSROOT=$ANDROID_NDK/platforms/android-21/arch-arm
#TOOLCHAIN=`echo $ANDROID_NDK/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64`
#export PATH=$TOOLCHAIN/bin:$PATH

# TODO 
export TOOLCHAIN=/home/aid/workspace/tools/toolchain4ffmpeg

export X264=/home/aid/workspace/projects/x264
export FFMPEG=/home/aid/workspace/projects/FFmpeg

export PREFIX=/home/aid/workspace/projects/FFmpegbuild

#export ANDROID_SOURCE=/home/aid/workspace/projects/android-source
#export ANDROID_LIBS=/home/aid/workspace/projects/android-libs

export PATH=$TOOLCHAIN/bin:$PATH
#export PATH=/home/aid/workspace/tools/android-ndk/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64/bin:$PATH
export SYSROOT=$TOOLCHAIN/sysroot/
#export SYSROOT=/home/aid/workspace/tools/android-ndk/platforms/android-21
export CXX=arm-linux-androideabi-g++
export CC=arm-linux-androideabi-gcc
export LD=arm-linux-androideabi-ld
export AR=arm-linux-androideabi-ar
export CXX=arm-linux-androideabi-g++
export AS=arm-linux-androideabi-gcc
#export STRIP=$TOOLCHAIN/bin/arm-linux-androideabi-strip
export STRIP=arm-linux-androideabi-strip

X264_CFLAGS=" -gdwarf-2 -DANDROID -march=armv7-a -mfloat-abi=softfp -mfpu=neon -mvectorize-with-neon-quad"

FFMPEG_CFLAGS="-O3 -Wall -mthumb -pipe -fpic -fasm \
  -finline-limit=300 -ffast-math \
  -fstrict-aliasing -Werror=strict-aliasing \
  -fmodulo-sched -fmodulo-sched-allow-regmoves \
  -Wno-psabi -Wa,--noexecstack \
  -D__ARM_ARCH_5__ -D__ARM_ARCH_5E__ \
  -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__ \
  -DANDROID -DNDEBUG --sysroot=$SYSROOT \
  -march=armv7-a \
  -mfpu=neon \
  -mfloat-abi=softfp \
  -mvectorize-with-neon-quad"
FFMPEG_EXTRA_CFLAGS="-I$X264/build/include" #for ffmpeg
FFMPEG_EXTRA_LDFLAGS="-L$X264/build/lib -Wl,--fix-cortex-a8"

X264_FLAGS="--prefix=$X264/build \
	--cross-prefix=/home/aid/workspace/tools/android-ndk/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64/bin/arm-linux-androideabi- \
	--host=armv7-a-linux \
       --disable-opencl \
       --enable-pic \
       --enable-strip \
       --bit-depth=8 \
       --chroma-format=420 \
       --disable-interlaced \
       --enable-static"
    

FFMPEG_FLAGS=" \
  --target-os=linux \
  --arch=arm \
  --cpu=armv7-a \
  --enable-cross-compile \
  --cross-prefix=arm-linux-androideabi- \
  --enable-static \
  --enable-shared \
  --disable-debug \
  --disable-symver \
  --disable-doc \
  --disable-htmlpages \
  --disable-manpages \
  --disable-podpages \
  --disable-txtpages \
  --disable-ffplay \
  --disable-ffmpeg \
  --disable-ffprobe \
  --disable-ffserver \
  --disable-avdevice\
  --disable-postproc \
  --enable-jni \
  --enable-protocols \
  --enable-parsers \
  --enable-avfilter \
  --enable-demuxers \
  --disable-demuxer=sbg \
  --enable-decoders \
  --enable-bsfs \
  --enable-swscale \
  --enable-asm \
  --enable-version3 \
  --enable-demuxer=image2  \
  --enable-demuxer=image2pipe  \
  --enable-demuxer=image_png_pipe  \
  --enable-demuxer=matroska  \
  --enable-demuxer=mov  \
  --enable-demuxer=mpegts  \
  --enable-demuxer=mp3  \
  --enable-demuxer=pcm_s16le  \
  --enable-demuxer=rawvideo  \
  --enable-muxer=image2  \
  --enable-muxer=image2pipe  \
  --enable-muxer=matroska  \
  --enable-muxer=mov  \
  --enable-muxer=mpegts  \
  --enable-muxer=mp4  \
  --enable-muxer=pcm_s16le  \
  --enable-muxer=rawvideo  \
  --enable-decoder=amrnb  \
  --enable-decoder=amrwb  \
  --enable-decoder=mp3  \
  --enable-decoder=pcm_s16le  \
  --enable-decoder=ffv1  \
  --enable-decoder=hevc  \
  --enable-decoder=h263  \
  --enable-decoder=h263p  \
  --enable-decoder=mpeg4  \
  --enable-decoder=dca \
  --enable-decoder=png  \
  --enable-decoder=rawvideo  \
  --enable-encoder=pcm_s16le  \
  --enable-encoder=ffv1  \
  --enable-gpl \
  --enable-nonfree \
  --enable-encoder=png  \
  --enable-encoder=rawvideo  \
  --enable-decoder=aac  \
  --enable-encoder=aac  \
  --enable-bsf=aac_adtstoasc  \
  --enable-libx264  \
  --enable-encoder=libx264  \
  --enable-mediacodec \
  --enable-decoder=h264 \
  --enable-decoder=h264_mediacodec \
  --enable-bsf=h264_mp4toannexb  \
  --enable-filter=amovie  \
  --enable-filter=movie  \
  --enable-filter=amerge  \
  --enable-filter=amix  \
  --enable-filter=aresample  \
  --enable-filter=pan  \
  --enable-filter=resample  \
  --enable-filter=volume  \
  --enable-filter=null  \
  --enable-filter=anull  \
  --enable-filter=crop  \
  --enable-filter=transpose  \
  --enable-filter=scale  \
  --enable-filter=alphaextract  \
  --enable-parser=aac  \
  --enable-parser=aac_latm  \
  --enable-parser=mpegaudio  \
  --enable-parser=h263  \
  --enable-parser=h264  \
  --enable-parser=mpeg4video  \
  --enable-parser=png  \
  --enable-protocol=file"

rm -rf $X264/build

cd $X264
export CFLAGS=$X264_CFLAGS
./configure $X264_FLAGS  || exit 1
make clean
make -j4 || exit 1
make install || exit 1
  
cd $FFMPEG
export CFLAGS=""
export EXTRA_CFLAGS=""
export EXTRA_LDFLAGS=""
./configure $FFMPEG_FLAGS --extra-cflags="$FFMPEG_CFLAGS $FFMPEG_EXTRA_CFLAGS" \
  --extra-ldflags="$FFMPEG_EXTRA_LDFLAGS" --prefix=$PREFIX || exit 1
make clean
make -j4 || exit 1
make install || exit 1

#mkdir $PREFIX/objs
#find $X264 -name "*.o" -exec cp -f {} $PREFIX/objs/ \;  
#find $FDK_AAC -name "*.o" -exec cp -f {} $PREFIX/objs/ \; 
#find $FFMPEG -name "*.o" -exec cp -f {} $PREFIX/objs/ \; 
#$CC -lm -lz -shared --sysroot=$SYSROOT -Wl,--no-undefined -Wl,-z,noexecstack $FFMPEG_EXTRA_LDFLAGS $PREFIX/objs/*.o -o $PREFIX/libffmpeg.so
#rm -rf $PREFIX/objs

#$STRIP --strip-unneeded $PREFIX/libffmpeg.so
