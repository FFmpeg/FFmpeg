#!/bin/bash
#
# Script to build ffmpeg as needed

set -e

if [ -z "$PREFIX" ]; then
	echo 'Fatal: $PREFIX is not set'
	exit 1
fi

export MAKEFLAGS="-j$[$(nproc) + 1]"

./configure --prefix=$PREFIX \
	--extra-cflags=-I$PREFIX/include \
	--extra-ldflags=-L$PREFIX/lib \
	--bindir=$PREFIX/bin \
	--extra-libs=-ldl \
	--enable-version3 --enable-gpl --enable-nonfree \
	--enable-postproc --enable-avresample \
	--enable-libx264 --enable-libfdk-aac \
	--disable-ffplay --disable-ffprobe \
	--enable-small --disable-stripping --disable-debug

make

cd tools/
make qt-faststart
