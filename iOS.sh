#!/bin/bash

[[ ! -e iOS_local.sh ]] && {
	echo "File 'iOS_local.sh' not found! Pls see 'iOS_local.sh.sample'!!" 2>&1
	exit 1;
}
source iOS_local.sh

DEST=`pwd`/build/ios
SOURCE=`pwd`
FFMPEG_DIR=${SOURCE}
SSL=${SOURCE}/../opensslmirror/build/ios
SSLINCLUDE=${SSL}/release/universal/include
SSLLIBS=${SSL}/release/universal/lib


function doConfigure()
{
	# *NEED* gas-preprocessor.pl file in PATH:
	# wget https://github.com/yuvi/gas-preprocessor/blob/master/gas-preprocessor.pl
	# wget https://github.com/wens/gas-preprocessor/blob/master/gas-preprocessor.pl
	./configure \
		--prefix=${DIST} \
		\
		--enable-version3 \
		\
		--disable-shared \
		--disable-small \
		--disable-runtime-cpudetect \
		\
		--disable-programs \
		--disable-doc \
		\
		--disable-avdevice \
		--disable-postproc \
		--enable-network \
		\
		--enable-vda \
		\
		--disable-muxers \
		--enable-muxer=mp4 \
		--enable-demuxers \
		--disable-demuxer=sbg \
		--disable-demuxer=dts \
		--disable-encoders \
		--enable-decoders \
		--disable-decoder=dca \
		--disable-decoder=svq3 \
		--disable-parser=dca \
		--disable-devices \
		\
		--disable-bzlib \
		--disable-zlib \
		--disable-iconv \
		--enable-openssl \
		\
		--enable-cross-compile \
		--sysroot=${SDKRoot} \
		--target-os=darwin \
		--as="gas-preprocessor.pl ${CC}" \
		--cc=${CC} \
		--extra-cflags="-DVPLAYER_IOS -arch ${ARCH} -I${SSLINCLUDE} ${EXCFLAGS}" \
		--extra-ldflags="-arch ${ARCH} -isysroot ${SDKRoot} -L${SSLLIBS} ${EXCLDFLAGS}" \
		${ADVANCED} \
		--enable-pic \
		--enable-thumb \
		--disable-symver \
		--enable-hardcoded-tables \
		--disable-memalign-hack \
		\
		--enable-asm \
		--disable-armv5te \
		--disable-armv6 \
		--disable-armv6t2 \
		--enable-neon \
		\
		${DEBUGS} \

		ret=$?; cp -f ./config.log ${DIST}/; [[ $ret != 0 ]] && kill $$
}


build_date=`date "+%Y%m%dT%H%M%S"`
build_versions="release debug"
#build_archs="armv7 armv7s"
build_archs="armv7"
path_old=$PATH

for iver in $build_versions; do
	case $iver in
		release)
			export DEBUGS="--disable-debug --enable-optimizations"
			export ADVANCED="--optflags=-Os"
			;;
		debug)
			export DEBUGS="--enable-debug --disable-optimizations"
			export ADVANCED=
			;;
	esac

	lipo_archs=
	for iarch in $build_archs; do
		export PATH=$DEVRoot/usr/bin:$HOME/bin:$path_old
		export ARCH=$iarch
		export DIST=${DEST}/$build_date/$iver/$iarch && mkdir -p ${DIST}
		confInfo=${DIST}/configure-info.out
		makeInfo=${DIST}/make-info.out

		case $iarch in
			armv7)
				export EXCFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp -mvectorize-with-neon-quad"
				export EXCLDFLAGS="-mfpu=neon -mfloat-abi=softfp -mvectorize-with-neon-quad"
				export ADVANCED="${ADVANCED}"" --arch=armv7-a --cpu=cortex-a8"
				;;
			armv7s)
				export EXCFLAGS=
				export EXCLDFLAGS=
				export ADVANCED="${ADVANCED}"" --arch=armv7-a --cpu=cortex-a9"
				;;
		esac

		cd $FFMPEG_DIR && cp -f ./iOS.sh $DIST
		doConfigure 2>&1 | tee -a $confInfo
		(make clean && make && make install) 2>&1 | tee -a $makeInfo
		lipo_archs="$lipo_archs"" $iarch"
	done

	univs=${DEST}/$build_date/$iver/universal && mkdir -p $univs/lib
	cd ${DEST}/$build_date/$iver/$iarch/lib
	for ilib in *.a; do
		lps=; for jarch in $lipo_archs; do lps="$lps ""-arch $jarch ${DEST}/$build_date/$iver/$jarch/lib/$ilib"; done
		lipo $lps -create -output $univs/lib/$ilib
	done
done

[[ $? == 0 ]] && cd ${DEST} && rm -f built && ln -s $build_date built


exit 0
