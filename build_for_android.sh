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
FFMPEG_EXTRA_CFLAGS="-I$X264/build/include -DHAVE_ISNAN -DHAVE_ISINF" #for ffmpeg
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
  --enable-shared \
  --enable-static \
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
  --enable-hwaccels \
  --enable-avdevice\
  --enable-postproc \
  --enable-avfilter \
  --enable-swscale \
  --enable-avresample \
  --enable-jni \
  --enable-protocols \
  --enable-parsers \
  --enable-demuxers \
  --disable-demuxer=sbg \
  --enable-decoders \
  --enable-bsfs \
  --enable-asm \
  --enable-version3 \
  --enable-demuxers \
  --enable-muxers \
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
  --enable-libx264  \
  --enable-encoder=libx264  \
  --enable-mediacodec \
  --enable-decoder=h264 \
  --enable-decoder=h264_mediacodec \
  --enable-filters \
  --enable-parsers"

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

$CC -c ffmpeg.c ffmpeg_opt.c cmdutils.c ffmpeg_filter.c \
    $FFMPEG_CFLAGS \
    -I"$PREFIX/include" \
    -I"$SYSROOT/usr/include" \
    -I"$FFMPEG" \
    -L"$PREFIX/lib" \
    -Wl,--fix-cortex-a8 || exit 1


$AR -crv $FFMPEG/ffmpeg.a ffmpeg.o
$AR -crv $FFMPEG/ffmpeg_opt.a ffmpeg_opt.o
$AR -crv $FFMPEG/ffmpeg_filter.a ffmpeg_filter.o
$AR -crv $FFMPEG/cmdutils.a cmdutils.o

cp -f ffmpeg.h $PREFIX/include/ 
cp -f cmdutils.h $PREFIX/include/
cp -f config.h $PREFIX/include/


$LD -rpath-link=$SYSROOT/usr/lib \
    -L$SYSROOT/usr/lib \
    -soname libffmpeg.so \
    -shared -nostdlib  \
    -Bsymbolic \
    --whole-archive --no-undefined \
    -o $PREFIX/libffmpeg.so \
    $X264/build/lib/libx264.a \
    $PREFIX/lib/libavcodec.a \
    $PREFIX/lib/libavfilter.a \
    $PREFIX/lib/libswresample.a \
    $PREFIX/lib/libavformat.a \
    $PREFIX/lib/libavutil.a \
    $PREFIX/lib/libswscale.a \
    $PREFIX/lib/libavdevice.a \
    $PREFIX/lib/libpostproc.a \
    $PREFIX/lib/libavresample.a \
    $FFMPEG/cmdutils.a \
    $FFMPEG/ffmpeg_opt.a \
    $FFMPEG/ffmpeg_filter.a \
    $FFMPEG/ffmpeg.a \
    -lc -lz -lm -ldl -llog \
    $TOOLCHAIN/lib/gcc/arm-linux-androideabi/4.8/libgcc.a # 这里使用的工具链包含gcc4.8
