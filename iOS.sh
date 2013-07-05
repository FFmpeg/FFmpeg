#!/bin/bash

[[ ! -e iOS_local.sh ]] && {
	echo "File 'iOS_local.sh' not found! Pls see 'iOS_local.sh.sample'!!" 2>&1
	exit 1;
}
source iOS_local.sh


export CC=$DEVRoot/usr/bin/llvm-gcc
export LD=$DEVRoot/usr/bin/ld
export STRIP=$DEVRoot/usr/bin/strip

DEST=`pwd`/build/ios
SOURCE=`pwd`
SSL=${SOURCE}/../ios-openssl
SSLINCLUDE=${SSL}/include
SSLLIBS=${SSL}/lib

export FFMPEG_DIR=${SOURCE}


# export LIPO=/Users/nuoerlz/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/lipo
# $LIPO -arch armv7 lib.a -arch armv7s r/lib.a -create -output libffmpeg.a


function doConfigure()
{
	#  *NEED* gas-preprocessor.pl file in PATH:
	# wget https://github.com/jacobson/gas-preprocessor/blob/master/gas-preprocessor.pl
	./configure \
		--prefix=${DIST} \
		--cc=${CC} \
		--as="gas-preprocessor.pl ${CC}" \
		--sysroot=${SDKRoot} \
		--enable-cross-compile \
		--target-os=darwin \
		--arch=arm \
		--cpu=cortex-a8 \
		--extra-cflags="-DVPLAYER_IOS -arch ${ARCH} -I${SSLINCLUDE}" \
		--extra-ldflags="-arch ${ARCH} -isysroot ${SDKRoot} -L${SSLLIBS}" \
		--enable-pic \
		--disable-runtime-cpudetect \
		--disable-symver \
		--disable-doc \
		--disable-ffplay \
		--disable-ffmpeg \
		--disable-ffprobe \
		--disable-ffserver \
		--disable-avdevice \
		--disable-postproc \
		--disable-encoders \
		--disable-devices \
		--disable-muxers \
		--enable-muxer=mp4 \
		--enable-demuxers \
		--disable-demuxer=sbg \
		--disable-demuxer=dts \
		--disable-parser=dca \
		--disable-decoder=dca \
		--disable-decoder=svq3 \
		--enable-network \
		--enable-openssl \
		--enable-version3 \
		\
		--disable-asm \
		--disable-shared

	[[ $? != 0 ]] && kill $$
}


for iarch in armv7; do
#for iarch in armv7 armv7s; do
	export ARCH=$iarch
	export DIST=${DEST}/build-dist-$iarch
	confInfo=${DIST}/configure-info.out
	makeInfo=${DIST}/make-info.out

	rm -rf $DIST
	mkdir -p $DIST
	cd $FFMPEG_DIR

	doConfigure 2>&1 | tee $confInfo

	(make clean && make && make install) 2>&1 | tee $makeInfo
done

