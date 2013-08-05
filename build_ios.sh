#!/bin/bash

[[ ! -e $HOME/build_ios_local.sh ]] && {
	echo "File \"$HOME/build_ios_local.sh\" not found! Pls see 'build_ios_local.sh.sample'!!" 2>&1
	exit 1;
}
source $HOME/build_ios_local.sh

pwd | grep -q '[[:blank:]]' && {
	echo "Source path: $(pwd)"
	echo "Out of tree builds are impossible with whitespace in source path."
	exit 1;
}


selfname=$0
DEST=`pwd`/build/ios
SOURCE=`pwd`
SSL=${SOURCE}/../opensslmirror/build/ios
SSLINCLUDE=${SSL}/release/universal/include
SSLLIBS=${SSL}/release/universal/lib

export DEVRootReal="${DEVELOPER}/Platforms/iPhoneOS.platform/Developer"
export SDKRootReal="${DEVRootReal}/SDKs/iPhoneOS${SDKVERSION}.sdk"
export DEVRootSimulator="${DEVELOPER}/Platforms/iPhoneSimulator.platform/Developer"
export SDKRootSimulator="${DEVRootSimulator}/SDKs/iPhoneSimulator${SDKVERSION}.sdk"
export PATH=$HOME/bin:$PATH
export CCACHE=; type ccache >/dev/null 2>&1 && export CCACHE=ccache


function doConfigure()
{
	# *NEED* gas-preprocessor.pl file in $PATH , use for asm compile.
	# wget https://raw.github.com/yuvi/gas-preprocessor/master/gas-preprocessor.pl
	# wget https://raw.github.com/wens/gas-preprocessor/master/gas-preprocessor.pl
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
		--cc="${CC}" \
		--extra-cflags="-DVPLAYER_IOS -arch ${ARCH} -pipe -I${SSLINCLUDE} ${EXCFLAGS}" \
		--extra-ldflags="-arch ${ARCH} -isysroot ${SDKRoot} -L${SSLLIBS} ${EXCLDFLAGS}" \
		${ADVANCED} \
		--enable-pic \
		--enable-thumb \
		--disable-symver \
		--enable-hardcoded-tables \
		--disable-memalign-hack \
		\
		${OPTZFLAGS} \
		\
		${DEBUGS} \

		ret=$?; cp -f ./config.log ${DIST}/; [[ $ret != 0 ]] && kill $$
}


build_date=`date "+%Y%m%dT%H%M%S"`
#build_date=20130801T182243
build_versions="release debug"
#build_versions="debug"
build_archs="armv7 armv7s i386"
#build_archs="i386"
path_old=$PATH

for iver in $build_versions; do
	case $iver in
		release)	export DEBUGS="--disable-debug --enable-optimizations --optflags=-O3" ;;
		debug)		export DEBUGS="--enable-debug=3 --disable-optimizations" ;;
	esac

	lipo_archs=
	for iarch in $build_archs; do
		export ARCH=$iarch
		export DIST=${DEST}/$build_date/$iver/$iarch && mkdir -p ${DIST}
		libdir=${DIST}/lib && mkdir -p $libdir
		confInfo=${DIST}/configure_out.log
		makeInfo=${DIST}/make_out.log

		case $iarch in
			arm*)
				export PATH=${DEVRootReal}/usr/bin:$path_old
				export CC="$CCACHE ${DEVRootReal}/usr/bin/gcc"
				export SDKRoot="$SDKRootReal"
				export OPTZFLAGS="--enable-asm --disable-armv5te --disable-armv6 --disable-armv6t2"
				case $iarch in
					armv7)
						export EXCFLAGS="-mfpu=neon -mfloat-abi=hard" # "-mcpu= or -march=" set by "--cpu="
						export ADVANCED="--arch=arm --cpu=cortex-a8"
						;;
					armv7s)
						export EXCFLAGS="-mfpu=neon-fp16 -mfloat-abi=hard"
						export ADVANCED="--arch=arm --cpu=cortex-a9"
						;;
				esac
				;;
			i386)
				export PATH=${DEVRootSimulator}/usr/bin:$path_old
				export CC="$CCACHE ${DEVRootSimulator}/usr/bin/gcc"
				export SDKRoot="$SDKRootSimulator"
				export OPTZFLAGS="--disable-asm"
				export EXCFLAGS=
				export ADVANCED="--arch=i386 --cpu=i386"
				;;
		esac

		cd $SOURCE && cp -f $selfname $DIST
		doConfigure 2>&1 | tee -a $confInfo
		objs=$(make -n -B | sed -n -e '/printf "AR.*; ar rc /p' | sed -e 's/^printf .* ar rc .*\.a//')
		(make clean && make && make install) 2>&1 | tee -a $makeInfo
		ar rc $libdir/libffmpeg.a $objs
		lipo_archs="$lipo_archs $libdir/libffmpeg.a"
	done

	export PATH=${DEVRootReal}/usr/bin:$path_old
	univs=${DEST}/$build_date/$iver/universal && mkdir -p $univs/lib
	lipo $lipo_archs -create -output $univs/lib/libffmpeg.a
	ranlib $univs/lib/libffmpeg.a
done

[[ $? == 0 ]] && {
	cd ${DEST} && rm -f built && ln -s $build_date built
	printf "\nFFmpeg build successful!!\n"
}

exit 0
