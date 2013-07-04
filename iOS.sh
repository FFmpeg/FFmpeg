DEST=`pwd`/build/ios
SOURCE=`pwd`
SSL=${SOURCE}/../ios-openssl
SSLINCLUDE=${SSL}/include
SSLLIBS=${SSL}/lib

export FFMPEG_DIR=${SOURCE}

# export LIPO=/Users/nuoerlz/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/lipo
# $LIPO -arch armv7 lib.a -arch armv7s r/lib.a -create -output libffmpeg.a

### For iOS 5.1
#export DEVRoot=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer
#export SDKRoot=$DEVRoot/SDKs/iPhoneOS5.1.sdk
#export CC=$DEVRoot/usr/bin/llvm-gcc
#export LD=$DEVRoot/usr/bin/ld
#export ARCH=armv7

## For iOS 6.x.x
export DEVRoot=/Users/nuoerlz/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer
export SDKRoot=$DEVRoot/SDKs/iPhoneOS6.1.sdk
export CC=$DEVRoot/usr/bin/llvm-gcc
export LD=$DEVRoot/usr/bin/ld


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
		--disable-muxers \
		--disable-devices \
		--disable-demuxer=sbg \
		--disable-demuxer=dts \
		--disable-parser=dca \
		--disable-decoder=dca \
		--disable-decoder=svq3 \
		--enable-muxer=mp4 \
		--enable-protocols  \
		--enable-parsers \
		--enable-demuxers \
		--enable-decoders \
		--enable-network \
		--enable-openssl \
		--enable-swscale  \
		--enable-version3 \
		\
		--enable-avdevice \
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

