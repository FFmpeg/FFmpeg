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
ARCH=`uname -m`
if [ $OS == 'Linux' ]; then
	export HOST_SYSTEM=linux-$ARCH
	export CCACHE=ccache
elif [ $OS == 'Darwin' ]; then
	export HOST_SYSTEM=darwin-$ARCH
	export CCACHE=
fi

SOURCE=`pwd`
DEST=$SOURCE/build/android
SSL=$SOURCE/../openssl

TOOLCHAIN=/tmp/vplayer
SYSROOT=$TOOLCHAIN/sysroot/
$ANDROID_NDK/build/tools/make-standalone-toolchain.sh --toolchain=x86-4.7 --arch=x86 \
	--system=$HOST_SYSTEM --platform=android-14 --install-dir=$TOOLCHAIN

export PATH=$TOOLCHAIN/bin:$PATH
export CC="$CCACHE i686-linux-android-gcc-4.7"
export LD=i686-linux-android-ld
export AR=i686-linux-android-ar

CFLAGS="-std=c99 -O3 -Wall -pipe -fpic -fasm \
	-finline-limit=300 -ffast-math \
	-fstrict-aliasing -Werror=strict-aliasing \
	-fmodulo-sched -fmodulo-sched-allow-regmoves \
	-fgraphite -fgraphite-identity -floop-block -floop-flatten \
	-floop-interchange -floop-strip-mine -floop-parallelize-all -ftree-loop-linear \
	-Wno-psabi -Wa,--noexecstack \
	-DANDROID -DNDEBUG \
	-I$SSL/include "

LDFLAGS="-lm -lz -Wl,--no-undefined -Wl,-z,noexecstack"

if [ $1 ]; then
	FFMPEG_FLAGS_COMMON="--target-os=linux \
		--arch=x86
		--cpu=i686
		--cross-prefix=i686-linux-android- \
		--enable-runtime-cpudetect
		--enable-shared \
		--disable-symver \
		--disable-doc \
		--disable-programs \
		--disable-avdevice \
		--disable-encoders  \
		--disable-muxers \
		--disable-devices \
		--disable-everything \
		--disable-protocols  \
		--disable-demuxers \
		--disable-decoders \
		--disable-bsfs \
		--enable-filters \
		--enable-parsers \
		--enable-swscale  \
		--enable-network \
		--enable-openssl \
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
		--disable-amd3dnow \
		--disable-amd3dnowext \
		--enable-asm \
		--enable-yasm \
		--enable-pic "


else
	FFMPEG_FLAGS_COMMON="--target-os=linux \
		--arch=x86
		--cpu=i686
		--cross-prefix=i686-linux-android- \
		--enable-cross-compile \
		--enable-shared \
		--disable-static \
		--disable-symver \
		--disable-doc \
		--disable-programs \
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
		--enable-version3 \
		--disable-amd3dnow \
		--disable-amd3dnowext \
		--enable-asm \
		--enable-yasm \
		--enable-pic "

fi

for version in x86; do

	cd $SOURCE

	FFMPEG_FLAGS="$FFMPEG_FLAGS_COMMON"

	case $version in
		x86)
			EXTRA_CFLAGS="-march=atom -msse3 -ffast-math -mfpmath=sse"
			EXTRA_LDFLAGS="-L$SSL/libs/x86"
			SSL_OBJS=`find $SSL/obj/local/x86/objs/ssl $SSL/obj/local/x86/objs/crypto -type f -name "*.o"`
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
	$CC -o $PREFIX/libffmpeg.so -shared $LDFLAGS $EXTRA_LDFLAGS $SSL_OBJS \
		libavutil/*.o libavutil/x86/*.o libavcodec/*.o libavcodec/x86/*.o libavformat/*.o libavfilter/*.o libavfilter/x86/*.o libswresample/*.o libswresample/x86/*.o libswscale/*.o libswscale/x86/*.o compat/*.o

	cp $PREFIX/libffmpeg.so $PREFIX/libffmpeg-debug.so
	i686-linux-android-strip --strip-unneeded $PREFIX/libffmpeg.so


	echo "  _    __   _    __                           _             "
	echo " | |  / /  (_)  / /_   ____ _   ____ ___     (_)  ___       "
	echo " | | / /  / /  / __/  / __ \/  / __ __  \   / /  / __ \     "
	echo " | |/ /  / /  / /_   / /_/ /  / / / / / /  / /  / /_/ /     "
	echo " |___/  /_/   \__/   \__,_/  /_/ /_/ /_/  /_/   \____/      "

	echo "----------------------$version -----------------------------"

done
