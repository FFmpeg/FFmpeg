#!/bin/bash

echo "  _    __   _    __                           _             "
echo " | |  / /  (_)  / /_   ____ _   ____ ___     (_)  ___       "
echo " | | / /  / /  / __/  / __ \/  / __ __  \   / /  / __ \     "
echo " | |/ /  / /  / /_   / /_/ /  / / / / / /  / /  / /_/ /     "
echo " |___/  /_/   \__/   \____/  /_/ /_/ /_/  /_/   \____/      "
echo "                                                            "

# export ANDROID_NDK=
# Detect ANDROID_NDK
if [ -z "$ANDROID_NDK" ]; then
  echo "You must define ANDROID_NDK before starting."
  echo "They must point to your NDK directories.\n"
  exit 1
fi

#SSL=$SSL
if [ -z "$SSL" ]; then
  echo "No define SSL before starting"
  echo "Please clone from git@github.com:yixia/OpenSSL-Vitamio.git, and run ndk-build ";
  exit 1
fi

# Detect OS
OS=`uname`
HOST_ARCH=`uname -m`
export CCACHE=; type ccache >/dev/null 2>&1 && export CCACHE=ccache
if [ $OS == 'Linux' ]; then
  export HOST_SYSTEM=linux-$HOST_ARCH
elif [ $OS == 'Darwin' ]; then
  export HOST_SYSTEM=darwin-$HOST_ARCH
fi



platform="$1"
version_type="$2"

function arm_toolchain()
{
  export CROSS_PREFIX=arm-linux-androideabi-
  $ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=${CROSS_PREFIX}4.8 \
    --system=$HOST_SYSTEM --install-dir=$TOOLCHAIN
}

function x86_toolchain()
{
  export CROSS_PREFIX=i686-linux-android-
  $ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=x86-4.8 \
    --system=$HOST_SYSTEM --install-dir=$TOOLCHAIN
}

function mips_toolchain()
{
  export CROSS_PREFIX=mipsel-linux-android-
  $ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=${CROSS_PREFIX}4.8 \
    --system=$HOST_SYSTEM --install-dir=$TOOLCHAIN
}


SOURCE=`pwd`
DEST=$SOURCE/build/android

TOOLCHAIN=/tmp/vitamio
SYSROOT=$TOOLCHAIN/sysroot/

if [ "$platform" = "x86" ];then
  echo "Build Android x86 ffmpeg\n"
  x86_toolchain
  TARGET="x86"
elif [ "$platform" = "mips" ];then
  echo "Build Android mips ffmpeg\n"
  mips_toolchain
  TARGET="mips"
else
  echo "Build Android arm ffmpeg\n"
  arm_toolchain
  TARGET="neon armv7 vfp armv6"
fi
export PATH=$TOOLCHAIN/bin:$PATH
export CC="$CCACHE ${CROSS_PREFIX}gcc"
export CXX=${CROSS_PREFIX}g++
export LD=${CROSS_PREFIX}ld
export AR=${CROSS_PREFIX}ar
export STRIP=${CROSS_PREFIX}strip


CFLAGS="-std=c99 -O3 -Wall -pipe -fpic -fasm \
  -finline-limit=300 -ffast-math \
  -fstrict-aliasing -Werror=strict-aliasing \
  -Wno-psabi -Wa,--noexecstack \
  -fdiagnostics-color=always \
  -DANDROID -DNDEBUG \
  -I$SSL/include"


LDFLAGS="-lm -lz -Wl,--no-undefined -Wl,-z,noexecstack"

case $CROSS_PREFIX in
  arm-*)
    CFLAGS="-mthumb $CFLAGS \
      -D__ARM_ARCH_5__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__"
    ;;
  x86-*)
    ;;
  mipsel-*)
    CFLAGS="-std=c99 -O3 -Wall -pipe -fpic -fasm \
      -ftree-vectorize -ffunction-sections -funwind-tables -fomit-frame-pointer -funswitch-loops \
      -finline-limit=300 -finline-functions -fpredictive-commoning -fgcse-after-reload -fipa-cp-clone \
      -Wno-psabi -Wa,--noexecstack \
      -DANDROID -DNDEBUG \
      -I$SSL/include"
    ;;
esac

if [ "$version_type" = "online" ]; then
  FFMPEG_FLAGS_COMMON="--target-os=linux \
    --enable-cross-compile \
    --cross-prefix=$CROSS_PREFIX \
    --enable-version3 \
    --enable-shared \
    --disable-static \
    --disable-symver \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-encoders  \
    --disable-muxers \
    --disable-devices \
    --disable-everything \
    --disable-protocols  \
    --disable-demuxers \
    --disable-decoders \
    --disable-bsfs \
    --disable-debug \
    --enable-optimizations \
    --enable-filters \
    --enable-parsers \
    --disable-parser=hevc \
    --enable-swscale  \
    --enable-network \
    --enable-protocol=file \
    --enable-protocol=http \
    --enable-protocol=rtmp \
    --enable-protocol=rtp \
    --enable-protocol=mmst \
    --enable-protocol=mmsh \
    --enable-demuxer=hls \
    --enable-demuxer=mpegts \
    --enable-demuxer=mpegtsraw \
    --enable-demuxer=mpegvideo \
    --enable-demuxer=concat \
    --enable-demuxer=mov \
    --enable-demuxer=flv \
    --enable-demuxer=rtsp \
    --enable-demuxer=mp3 \
    --enable-demuxer=matroska \
    --enable-decoder=mpeg4 \
    --enable-decoder=mpegvideo \
    --enable-decoder=mpeg1video \
    --enable-decoder=mpeg2video \
    --enable-decoder=h264 \
    --enable-decoder=h263 \
    --enable-decoder=flv \
    --enable-decoder=vp8 \
    --enable-decoder=wmv3 \
    --enable-decoder=aac \
    --enable-decoder=ac3 \
    --enable-decoder=mp3 \
    --enable-decoder=nellymoser \
    --enable-muxer=mp4 \
    --enable-asm \
    --enable-pic"
else
  FFMPEG_FLAGS_COMMON="--target-os=linux \
    --enable-cross-compile \
    --cross-prefix=$CROSS_PREFIX \
    --enable-version3 \
    --enable-optimizations \
    --enable-shared \
    --disable-static \
    --disable-symver \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-postproc \
    --disable-encoders \
    --disable-muxers \
    --enable-muxer=mp4 \
    --disable-devices \
    --disable-demuxer=sbg \
    --disable-demuxer=dts \
    --disable-parser=dca \
    --disable-decoder=dca \
    --disable-decoder=svq3 \
    --disable-debug \
    --enable-network \
    --enable-asm"
fi

# --disable-decoder=ac3 --disable-decoder=eac3 --disable-decoder=mlp \

  for version in $TARGET; do

    cd $SOURCE

    FFMPEG_FLAGS="$FFMPEG_FLAGS_COMMON"

    case $version in
      neon)
        FFMPEG_FLAGS="--arch=armv7-a \
          --cpu=cortex-a8 \
          --enable-openssl \
          --disable-runtime-cpudetect \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp -mvectorize-with-neon-quad"
        EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
        SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
        ;;
      armv7)
        FFMPEG_FLAGS="--arch=armv7-a \
          --cpu=cortex-a8 \
          --enable-openssl \
          --disable-runtime-cpudetect \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
        EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$SSL/libs/armeabi-v7a"
        SSL_OBJS=`find $SSL/obj/local/armeabi-v7a/objs/ssl $SSL/obj/local/armeabi-v7a/objs/crypto -type f -name "*.o"`
        ;;
      vfp)
        FFMPEG_FLAGS="--arch=arm \
          --disable-runtime-cpudetect \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-march=armv6 -mfpu=vfp -mfloat-abi=softfp"
        EXTRA_LDFLAGS=""
        SSL_OBJS=""
        ;;
      armv6)
        FFMPEG_FLAGS="--arch=arm \
          --disable-runtime-cpudetect \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-march=armv6 -msoft-float"
        EXTRA_LDFLAGS=""
        SSL_OBJS=""
        ;;
      x86)
        FFMPEG_FLAGS="--arch=x86 \
          --cpu=i686 \
          --enable-runtime-cpudetect
        --enable-openssl \
          --enable-yasm \
          --disable-amd3dnow \
          --disable-amd3dnowext \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-march=atom -msse3 -ffast-math -mfpmath=sse"
        EXTRA_LDFLAGS="-L$SSL/libs/x86"
        SSL_OBJS=`find $SSL/obj/local/x86/objs/ssl $SSL/obj/local/x86/objs/crypto -type f -name "*.o"`
        ;;
      mips)
        FFMPEG_FLAGS="--arch=mips \
          --cpu=mips32r2 \
          --enable-runtime-cpudetect \
          --enable-openssl \
          --enable-yasm \
          --disable-mipsfpu \
          --disable-mipsdspr1 \
          --disable-mipsdspr2 \
          $FFMPEG_FLAGS"
        EXTRA_CFLAGS="-fno-strict-aliasing -fmessage-length=0 -fno-inline-functions-called-once -frerun-cse-after-loop -frename-registers"
        EXTRA_LDFLAGS="-L$SSL/libs/mips"
        SSL_OBJS=`find $SSL/obj/local/mips/objs/ssl $SSL/obj/local/mips/objs/crypto -type f -name "*.o"`
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

    ./configure $FFMPEG_FLAGS --extra-cflags="$CFLAGS $EXTRA_CFLAGS" --extra-ldflags="$LDFLAGS $EXTRA_LDFLAGS" | tee $PREFIX/configuration.txt
    cp config.* $PREFIX
    [ $PIPESTATUS == 0 ] || exit 1

    make clean
    find . -name "*.o" -type f -delete
    make -j4 || exit 1

    rm libavcodec/log2_tab.o libavformat/log2_tab.o libswresample/log2_tab.o

    case $CROSS_PREFIX in
      arm-*)
        $CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS \
          libavutil/*.o libavutil/arm/*.o libavcodec/*.o libavcodec/arm/*.o libavformat/*.o libavfilter/*.o libswresample/*.o libswresample/arm/*.o libswscale/*.o compat/*.o
        ;;
      i686-*)
        $CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS \
          libavutil/*.o libavutil/x86/*.o libavcodec/*.o libavcodec/x86/*.o libavformat/*.o libavfilter/*.o libavfilter/x86/*.o libswresample/*.o libswresample/x86/*.o libswscale/*.o libswscale/x86/*.o compat/*.o
        ;;
      mipsel-*)
        $CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS \
          libavutil/*.o libavutil/mips/*.o libavcodec/*.o libavcodec/mips/*.o libavformat/*.o libavfilter/*.o libswresample/*.o libswscale/*.o compat/*.o
        ;;
    esac

    cp $PREFIX/libffmpeg.so $PREFIX/libffmpeg-debug.so
    ${STRIP} --strip-unneeded $PREFIX/libffmpeg.so



    echo "----------------------$version -----------------------------"

  done
