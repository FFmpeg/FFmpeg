#!/bin/bash

source ./Android_local.sh


DEST=`pwd`/build/android && rm -rf $DEST
SOURCE=`pwd`
SSL=$SOURCE/../openssl

TOOLCHAIN=/tmp/vplayer
SYSROOT=$TOOLCHAIN/sysroot/
$ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=arm-linux-androideabi-4.7 \
  --system=$HOST_SYSTEM --platform=android-14 --install-dir=$TOOLCHAIN

export PATH=$TOOLCHAIN/bin:$PATH
export CC="$CCACHE arm-linux-androideabi-gcc"
export LD=arm-linux-androideabi-ld
export AR=arm-linux-androideabi-ar

CFLAGS="-std=c99 -O3 -Wall -mthumb -pipe -fpic -fasm \
  -finline-limit=300 -ffast-math \
  -fstrict-aliasing -Werror=strict-aliasing \
  -fmodulo-sched -fmodulo-sched-allow-regmoves \
  -fgraphite -fgraphite-identity -floop-block -floop-flatten \
  -floop-interchange -floop-strip-mine -floop-parallelize-all -ftree-loop-linear \
  -Wno-psabi -Wa,--noexecstack \
  -D__ARM_ARCH_5__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__ \
  -DANDROID -DNDEBUG \
  -I$SSL/include"

LDFLAGS="-lm -lz -Wl,--no-undefined -Wl,-z,noexecstack"

FFMPEG_FLAGS_COMMON="--target-os=linux \
  --cross-prefix=arm-linux-androideabi- \
  --enable-cross-compile \
  --enable-shared \
  --disable-static \
  --disable-runtime-cpudetect
  --disable-symver \
  --disable-doc \
  --disable-ffplay \
  --disable-ffmpeg \
  --disable-ffprobe \
  --disable-ffserver \
  --disable-avdevice \
  --disable-postproc \
  --disable-encoders \
  --disable-muxers \
  --disable-devices \
  --disable-demuxer=sbg \
  --disable-demuxer=dts \
  --disable-parser=dca \
  --disable-decoder=dca \
  --disable-decoder=svq3 \
  --enable-network \
  --enable-asm \
  --enable-version3"

  # --disable-decoder=ac3 --disable-decoder=eac3 --disable-decoder=mlp \

for version in neon armv7 vfp armv6; do

  cd $SOURCE

  FFMPEG_FLAGS="$FFMPEG_FLAGS_COMMON"

  case $version in
    neon)
      FFMPEG_FLAGS="--arch=armv7-a \
        --cpu=cortex-a8 \
        --enable-openssl \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp -mvectorize-with-neon-quad"
      EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
      SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
      ;;
    armv7)
      FFMPEG_FLAGS="--arch=armv7-a \
        --cpu=cortex-a8 \
        --enable-openssl \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
      EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
      SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
      ;;
    vfp)
      FFMPEG_FLAGS="--arch=arm \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv6 -mfpu=vfp -mfloat-abi=softfp"
      EXTRA_LDFLAGS=""
      SSL_OBJS=""
      ;;
    armv6)
      FFMPEG_FLAGS="--arch=arm \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv6"
      EXTRA_LDFLAGS=""
      SSL_OBJS=""
      ;;
    *)
      FFMPEG_FLAGS=""
      EXTRA_CFLAGS=""
      EXTRA_LDFLAGS=""
      SSL_OBJS=""
      ;;
  esac

  PREFIX="$DEST/$version" && mkdir -p $PREFIX
  FFMPEG_FLAGS="$FFMPEG_FLAGS --prefix=$PREFIX"

  ./configure $FFMPEG_FLAGS --extra-cflags="$CFLAGS $EXTRA_CFLAGS" --extra-ldflags="$LDFLAGS $EXTRA_LDFLAGS" | tee $PREFIX/configuration.txt
  cp config.* $PREFIX
  [ $PIPESTATUS == 0 ] || exit 1

  make clean
  find . -name "*.o" -type f -delete
  make -j4 || exit 1

  rm libavcodec/log2_tab.o libavformat/log2_tab.o libswresample/log2_tab.o
  $CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS \
    libavutil/*.o libavutil/arm/*.o libavcodec/*.o libavcodec/arm/*.o libavformat/*.o libavfilter/*.o libswresample/*.o libswresample/arm/*.o libswscale/*.o compat/*.o

  cp $PREFIX/libffmpeg.so $PREFIX/libffmpeg-debug.so
  arm-linux-androideabi-strip --strip-unneeded $PREFIX/libffmpeg.so

done
