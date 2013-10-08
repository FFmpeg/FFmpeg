#!/bin/bash

# export ANDROID_NDK=
# Detect ANDROID_NDK
if [ -z "$ANDROID_NDK" ]; then
	echo "You must define ANDROID_NDK before starting."
	echo "They must point to your NDK directories.\n"
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

function arm_toolchain()
{
	export CROSS_PREFIX=arm-linux-androideabi-
	$ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=${CROSS_PREFIX}4.7 \
		--system=$HOST_SYSTEM --platform=android-14 --install-dir=$TOOLCHAIN
}

function x86_toolchain()
{
	export CROSS_PREFIX=i686-linux-android-
	$ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=x86-4.7 \
		--system=$HOST_SYSTEM --platform=android-14 --install-dir=$TOOLCHAIN
}


SOURCE=`pwd`
DEST=$SOURCE/build/android_static

TOOLCHAIN=/tmp/vplayer
SYSROOT=$TOOLCHAIN/sysroot/

if [ "$platform" = "x86" ];then
	echo "Build Android x86 ffmpeg\n"
	x86_toolchain
	TARGET="x86"
else
	echo "Build Android arm ffmpeg\n"
	arm_toolchain
	TARGET="armv7"
fi


X264=../x264
AAC=../fdk-aac

export PATH=$TOOLCHAIN/bin:$PATH
export CC="$CCACHE ${CROSS_PREFIX}gcc"
export LD=${CROSS_PREFIX}ld
export AR=${CROSS_PREFIX}ar
export STRIP=${CROSS_PREFIX}strip

CFLAGS="-std=c99 -O3 -Wall -pipe -fpic -fasm \
	-finline-limit=300 -ffast-math \
	-fstrict-aliasing -Werror=strict-aliasing \
	-fmodulo-sched -fmodulo-sched-allow-regmoves \
	-fgraphite -fgraphite-identity -floop-block -floop-flatten \
	-floop-interchange -floop-strip-mine -floop-parallelize-all -ftree-loop-linear \
	-Wno-psabi -Wa,--noexecstack \
	-DANDROID -DNDEBUG \
  -I$X264/build/android/include \
  -I$AAC/build/android/include"

LDFLAGS="-lm -lz -Wl,--no-undefined -Wl,-z,noexecstack"

case $CROSS_PREFIX in
	arm-*)
		CFLAGS="-mthumb $CFLAGS \
			-D__ARM_ARCH_5__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__"
		;;
	x86-*)
		;;
esac

FFMPEG_FLAGS_COMMON="--target-os=linux \
  --enable-cross-compile \
  --cross-prefix=$CROSS_PREFIX \
  --enable-version3 \
  --enable-optimizations \
  --enable-static \
  --enable-muxers \
  --disable-shared \
  --disable-symver \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-avdevice \
  --disable-devices \
  --disable-parser=dca \
  --enable-network \
  --enable-libx264 \
  --enable-libfdk_aac \
  --enable-nonfree \
  --enable-gpl \
  --enable-asm"

for version in $TARGET; do

	cd $SOURCE

	FFMPEG_FLAGS="$FFMPEG_FLAGS_COMMON"

	case $version in
		armv7)
			FFMPEG_FLAGS="--arch=armv7-a \
				--cpu=cortex-a8 \
				--disable-runtime-cpudetect \
				$FFMPEG_FLAGS"
			EXTRA_CFLAGS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp"
			EXTRA_LDFLAGS="-Wl,--fix-cortex-a8"
			EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$X264/build/android/lib -L$AAC/build/android/lib -lfdk-aac"
			;;
		armv6)
			FFMPEG_FLAGS="--arch=arm \
				--disable-runtime-cpudetect \
				$FFMPEG_FLAGS"
			EXTRA_CFLAGS="-march=armv6 -msoft-float"
			;;
		x86)
			FFMPEG_FLAGS="--arch=x86 \
				--cpu=i686 \
				--enable-runtime-cpudetect
				--enable-yasm \
				--disable-amd3dnow \
				--disable-amd3dnowext \
				$FFMPEG_FLAGS"
			EXTRA_CFLAGS="-march=atom -msse3 -ffast-math -mfpmath=sse"
			;;
		*)
			FFMPEG_FLAGS=""
			EXTRA_CFLAGS=""
			EXTRA_LDFLAGS=""
			;;
	esac

	PREFIX="$DEST/$version" && rm -rf $PREFIX && mkdir -p $PREFIX
	FFMPEG_FLAGS="$FFMPEG_FLAGS --prefix=$PREFIX"

	./configure $FFMPEG_FLAGS --extra-cflags="$CFLAGS $EXTRA_CFLAGS" --extra-ldflags="$LDFLAGS $EXTRA_LDFLAGS" | tee $PREFIX/configuration.txt
	cp config.* $PREFIX
	[ $PIPESTATUS == 0 ] || exit 1

	make clean
	find . -name "*.o" -type f -delete
	make -j4 install || exit 1

	echo "  _    __   _    __                           _             "
	echo " | |  / /  (_)  / /_   ____ _   ____ ___     (_)  ___       "
	echo " | | / /  / /  / __/  / __ \/  / __ __  \   / /  / __ \     "
	echo " | |/ /  / /  / /_   / /_/ /  / / / / / /  / /  / /_/ /     "
	echo " |___/  /_/   \__/   \__,_/  /_/ /_/ /_/  /_/   \____/      "

	echo "----------------------$version -----------------------------"

done
