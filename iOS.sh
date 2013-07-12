#!/bin/bash

[[ ! -e iOS_local.sh ]] && {
	echo "File 'iOS_local.sh' not found! Pls see 'iOS_local.sh.sample'!!" 2>&1
	exit 1;
}
source iOS_local.sh

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
		\
		--cc=${CC} \
		--as="gas-preprocessor.pl ${CC}" \
		--sysroot=${SDKRoot} \
		--enable-cross-compile \
		--target-os=darwin \
		--arch=arm \
		--cpu=cortex-a8 \
		--extra-cflags="-DVPLAYER_IOS -arch ${ARCH} -I${SSLINCLUDE}" \
		--extra-ldflags="-arch ${ARCH} -isysroot ${SDKRoot} -L${SSLLIBS}" \
		--optflags="-Os" \
		--enable-pic \
		--disable-symver \
		--enable-hardcoded-tables \
		--disable-safe-bitstream-reader \
		\
		--disable-shared \
		--disable-small \
		--disable-runtime-cpudetect \
		\
		--disable-avdevice \
		--disable-postproc \
		--enable-network \
		\
		--disable-muxers \
		--enable-muxer=mp4 \
		--enable-demuxers \
		--disable-demuxer=sbg \
		--disable-demuxer=dts \
		--disable-encoders \
		--disable-decoder=dca \
		--disable-decoder=svq3 \
		--disable-parser=dca \
		--disable-devices \
		\
		--enable-openssl \
		\
		--disable-doc \
		--disable-programs \
		--enable-version3 \
		\
		--disable-asm \
		\
		--disable-debug \

		[[ $? != 0 ]] && kill $$
}
		#--enable-lto \


for iarch in armv7; do
#for iarch in armv7 armv7s; do
	export PATH=$DEVRoot/usr/bin:$PATH
	export ARCH=$iarch
	export DIST=${DEST}/build-dist-$iarch-`date "+%Y%m%d-%H%M%S"`
	confInfo=${DIST}/configure-info.out
	makeInfo=${DIST}/make-info.out

	cd $FFMPEG_DIR
	rm -rf $DIST && mkdir -p $DIST
	cp -f ./iOS.sh $DIST

	doConfigure 2>&1 | tee $confInfo

	(make clean && make && make install) 2>&1 | tee $makeInfo
done

