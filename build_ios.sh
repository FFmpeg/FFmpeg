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
RTMP=${SOURCE}/../rtmpdump/build/ios/built
RTMPINCLUDE=${RTMP}/release/universal/include
RTMPLIBS=${RTMP}/release/universal/lib

export DEVRootReal="${DEVELOPER}/Platforms/iPhoneOS.platform/Developer"
export SDKRootReal="${DEVRootReal}/SDKs/iPhoneOS${SDKVERSION}.sdk"
export DEVRootSimulator="${DEVELOPER}/Platforms/iPhoneSimulator.platform/Developer"
export SDKRootSimulator="${DEVRootSimulator}/SDKs/iPhoneSimulator${SDKVERSION}.sdk"
export PATH=$HOME/bin:$PATH
export CCACHE=; type ccache >/dev/null 2>&1 && export CCACHE=ccache
export PKG_CONFIG_LIBDIR=${SSLLIBS}/pkgconfig:${RTMPLIBS}/pkgconfig


function die()
{
	kill $$
	exit 1; exit 1; exit 1; exit 1;
}

function doConfigureSina()
{
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
		--enable-swscale  \
		--disable-avresample \
		--enable-network \
		\
		--disable-everything \
		--enable-filters \
		--enable-parsers \
		--enable-protocol=file \
		--enable-protocol=http \
		--enable-demuxer=hls \
		--enable-demuxer=mpegts \
		--enable-demuxer=mpegtsraw \
		--enable-demuxer=mpegvideo \
		--enable-demuxer=concat \
		--enable-demuxer=mov \
		--enable-demuxer=flv \
		--enable-demuxer=mp3 \
		--enable-decoder=mpeg4 \
		--enable-decoder=mpegvideo \
		--enable-decoder=mpeg1video \
		--enable-decoder=mpeg2video \
		--enable-decoder=h264 \
		--enable-decoder=h263 \
		--enable-decoder=flv \
		--enable-decoder=aac \
		--enable-decoder=ac3 \
		--enable-decoder=mp3 \
		\
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
		${DEBUGS}
}

function doConfigureAll()
{
	# *NEED* gas-preprocessor.pl file in $PATH , use for asm compile.
	# wget https://raw.github.com/libav/gas-preprocessor/master/gas-preprocessor.pl
	# wget https://raw.github.com/yuvi/gas-preprocessor/master/gas-preprocessor.pl
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
		--enable-bzlib \
		--enable-zlib \
		--enable-iconv \
		--disable-librtmp \
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
		${DEBUGS}
}

# $1	version
function doConfigure()
{
	ver="$1";
	if [[ $ver == "sina" ]]; then
		doConfigureSina
	else
		doConfigureAll
	fi
	ret=$?; cp -f ./config.log ./config.h ${DIST}/; [[ $ret != 0 ]] && die
}

function doMake()
{
	(make clean && make) || die
	## MMS stream failed by optimizations flag "-O1/-O2/-O3", so ...
	sed -e '/^CFLAGS=/s/ -O[123s]/ -O0/' config.mak > config.O0.mak
	mv -f config.O0.mak config.mak
	cd libavformat && rm -f mms.o mmst.o mmsh.o mmsu.o && cd ..
	make && make install || die
}


build_date=`date "+%Y%m%dT%H%M%S"`
build_versions="release debug sina"
build_archs="armv7 armv7s i386"
#build_versions="sina"
#build_date=built
#build_archs="armv7 armv7s i386"
path_old=$PATH

for iver in $build_versions; do
	case $iver in
		release|sina)	export DEBUGS="--disable-debug --disable-optimizations --optflags=-O3" ;; # -O3 failed to open mms stream!
		debug)			export DEBUGS="--enable-debug=3 --disable-optimizations" ;;
	esac

	lipo_archs=
	for iarch in $build_archs; do
		[[ $iver == "debug" && $iarch != "armv7" ]] && continue
		[[ $iver == "sina" && $iarch == "i386" ]] && continue
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
		doConfigure "$iver" 2>&1 | tee -a $confInfo
		objs=$(make -n -B | sed -n -e '/printf "AR.*; ar rc /p' | sed -e 's/^printf .* ar rc .*\.a//')
		doMake 2>&1 | tee -a $makeInfo
		ar rc $libdir/libffmpeg.a $objs
		lipo_archs="$lipo_archs $libdir/libffmpeg.a"
	done

	export PATH=${DEVRootReal}/usr/bin:$path_old
	univs=${DEST}/$build_date/$iver/universal
	univslib=$univs/lib && mkdir -p $univslib
	lipo $lipo_archs -create -output $univslib/libffmpeg_tmp.a
	#libtool -static -o $univslib/libffmpeg.a -L$univslib -L$RTMPLIBS -lffmpeg_tmp -lrtmp
	libtool -static -o $univslib/libffmpeg.a -L$univslib -lffmpeg_tmp
	ranlib $univslib/libffmpeg.a
	[[ $iver != "debug" ]] && strip -S $univslib/libffmpeg.a
done

[[ $build_date != built ]] && cd ${DEST} && rm -f built && ln -s $build_date built
printf "\nFFmpeg build successfully!!\n\n"

exit 0
