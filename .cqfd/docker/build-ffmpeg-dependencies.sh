#!/bin/bash
#
# Compile & Install FFMPEG libraries

set -euo pipefail

echo "/usr/local/lib" >/etc/ld.so.conf.d/libc.conf

if [ -z "$PREFIX" ]; then
	echo 'PREFIX was not set in Dockerfile. Aborting.'
	exit 1
fi

export MAKEFLAGS="-j$[$(nproc) + 1]"

# yasm
DIR=$(mktemp -d)
cd $DIR/
curl -s http://www.tortall.net/projects/yasm/releases/yasm-$YASM_VERSION.tar.gz |
	tar zxvf - -C .
cd $DIR/yasm-$YASM_VERSION/
./configure --prefix="$PREFIX" --bindir="$PREFIX/bin" --docdir=$DIR -mandir=$DIR
make
make install
make distclean
rm -rf $DIR

# nasm
cd /tmp
nasm_rpm=nasm-$NASM_VERSION-0.fc24.x86_64.rpm
curl -O https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VERSION/linux/$nasm_rpm
rpm -i $nasm_rpm
rm -f $nasm_rpm

# x264
DIR=$(mktemp -d)
cd $DIR/
git clone -b stable  --single-branch http://git.videolan.org/git/x264.git
cd x264/
./configure --prefix="$PREFIX" --bindir="$PREFIX/bin" --enable-static
make
make install
make distclean
rm -rf $DIR

# fdk-aac
DIR=$(mktemp -d)
cd $DIR/
curl -s https://codeload.github.com/mstorsjo/fdk-aac/tar.gz/v$FDKAAC_VERSION |
	tar zxvf - -C .
cd fdk-aac-$FDKAAC_VERSION/
libtoolize
autoreconf -fiv
./configure --prefix="$PREFIX" --disable-shared
make
make install
make distclean
rm -rf $DIR
