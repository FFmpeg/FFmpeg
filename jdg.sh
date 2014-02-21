#!/bin/bash

echo "  _    __   _    __                           _             "
echo " | |  / /  (_)  / /_   ____ _   ____ ___     (_)  ___       "
echo " | | / /  / /  / __/  / __ \/  / __ __  \   / /  / __ \     "
echo " | |/ /  / /  / /_   / /_/ /  / / / / / /  / /  / /_/ /     "
echo " |___/  /_/   \__/   \__,_/  /_/ /_/ /_/  /_/   \____/      "


# Test script

DEST=`pwd`/build/android
SOURCE=`pwd`
SSL=$SOURCE/../openssl


TOOLCHAIN=/tmp/vitamio
SYSROOT=$TOOLCHAIN/sysroot/
if [ -d $TOOLCHAIN ]; then
    echo "Toolchain is already build."
else
		$ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=arm-linux-androideabi-4.8 \
			--system=linux-x86_64 --platform=android-14 --install-dir=$TOOLCHAIN
fi

export PATH=$TOOLCHAIN/bin:$PATH
export CC="ccache arm-linux-androideabi-gcc"
export LD=arm-linux-androideabi-ld
export AR=arm-linux-androideabi-ar

CFLAGS="-std=c99 -O3 -Wall -mthumb -pipe -fpic -fasm \
  -finline-limit=300 -ffast-math \
  -Wno-psabi -Wa,--noexecstack \
  -fdiagnostics-color=always \
  -D__ARM_ARCH_5__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__ \
  -DANDROID -DNDEBUG \
  -I$SSL/include "

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
  --enable-openssl \
  --enable-network \
  --enable-asm \
  --enable-version3"


for version in neon; do

  cd $SOURCE

  FFMPEG_FLAGS="$FFMPEG_FLAGS_COMMON"

  case $version in
    neon)
      FFMPEG_FLAGS="--arch=armv7-a \
        --cpu=cortex-a8 \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp -mvectorize-with-neon-quad"
      EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
      SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
      ;;
    armv7)
      FFMPEG_FLAGS="--arch=armv7-a \
        --cpu=cortex-a8 \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
      EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
      SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
      ;;
    vfp)
      FFMPEG_FLAGS="--arch=arm \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv6 -mfpu=vfp -mfloat-abi=softfp"
      EXTRA_LDFLAGS="-L$SSL/libs/armeabi"
      SSL_OBJS=`find $SSL/obj/local/armeabi/objs/ssl $SSL/obj/local/armeabi/objs/crypto -type f -name "*.o"`
      ;;
    armv6)
      FFMPEG_FLAGS="--arch=arm \
        $FFMPEG_FLAGS"
      EXTRA_CFLAGS="-march=armv6"
      EXTRA_LDFLAGS="-L$SSL/libs/armeabi"
      SSL_OBJS=`find $SSL/obj/local/armeabi/objs/ssl $SSL/obj/local/armeabi/objs/crypto -type f -name "*.o"`
      ;;
    *)
      FFMPEG_FLAGS=""
      EXTRA_CFLAGS=""
      EXTRA_LDFLAGS=""
      SSL_OBJS=""
      ;;
  esac

  PREFIX="$DEST/$version" && rm -rf $PREFIX && mkdir -p $PREFIX
  FFMPEG_FLAGS="$FFMPEG_FLAGS --prefix=$PREFIX"

	 #./configure $FFMPEG_FLAGS --extra-cflags="$CFLAGS $EXTRA_CFLAGS" --extra-ldflags="$LDFLAGS $EXTRA_LDFLAGS" | tee $PREFIX/configuration.txt
	#cp config.* $PREFIX
	#[ $PIPESTATUS == 0 ] || exit 1

	#make clean
	#find . -name "*.o" -type f -delete
  make -j4 || exit 1

  rm libavcodec/log2_tab.o libavformat/log2_tab.o libswresample/log2_tab.o
  $CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS\
    libavutil/*.o libavutil/arm/*.o libavcodec/*.o libavcodec/arm/*.o libavformat/*.o libavfilter/*.o libswresample/*.o libswresample/arm/*.o libswscale/*.o compat/*.o


  cp $PREFIX/libffmpeg.so $PREFIX/libffmpeg-debug.so
  arm-linux-androideabi-strip --strip-unneeded $PREFIX/libffmpeg.so

  adb push $PREFIX/libffmpeg.so /data/data/io.vov.vitamio.demo/libs/

done
