#!/bin/bash
#./configure --toolchain=msvc --target-os=win64 --enable-ffplay --enable-gpl --enable-videotoolbox --enable-audiotoolbox --enable-encoder=h264_videotoolbox --enable-encoder=aac_at   --enable-encoder=alac_at   --enable-static --enable-shared  --enable-debug  --disable-x86asm  --extra-cflags=-g --extra-ldflags=-g  --enable-nonfree --enable-libx264  --extra-cflags="-I../build/include"  --extra-ldflags="-LIBPATH:../build/lib"  --prefix=./build
./configure --toolchain=msvc \
	--target-os=win64 \
	--enable-ffplay \
	--enable-gpl \
	#--enable-videotoolbox \
	#--enable-audiotoolbox \
	
	#硬编码支持
	--enable-encoder=nvenc \
	--enable-nvenc \
	--enable-encoder=h264_videotoolbox \
	
	#--enable-encoder=aac_at   \
	#--enable-encoder=alac_at  \
	--enable-static \
	--enable-shared  \
	--enable-debug  \
	--disable-x86asm  \
	--extra-cflags=-g \
	--extra-ldflags=-g  \
	--enable-nonfree \
	--enable-libx264  \
	--extra-cflags="-I../build/include"  \
	--extra-ldflags="-LIBPATH:../build/lib"  \
	--prefix=./build

#./configure --toolchain=msvc --target-os=win64 \
#		--enable-ffplay \
#		--enable-gpl   \
#		--enable-static \
#		--enable-shared  \
#		--enable-debug  \
#		--disable-x86asm  \
#		--enable-libsrt \
#		--extra-cflags=-g \
#		--extra-ldflags=-g  \
#		--enable-nonfree \
#		--enable-libx264  \
#		--extra-cflags="-I../build/include"  \
#		--extra-ldflags="-LIBPATH:../build/lib"  \
#		--prefix=./build

#./configure --toolchain=msvc --target-os=win64 \
#    --arch=x86_64 \
#    --enable-shared \
#    --enable-static  \
#    #--enable-small \
#    --enable-debug   \
#    #--enable-version3 \
#    #--enable-gpl \
#    #--enable-nonfree \
#   # --disable-stripping \
#    #--disable-encoders \
#   # --disable-decoders \
#    #--enable-decoder=h264 \
#    #--enable-encoder=libx264 \
#    #--enable-encoder=mjpeg \
#    #--enable-encoder=mpeg4 \
#	--extra-cflags=-g \
#	--extra-ldflags=-g \
#	--disable-x86asm \
#    --prefix=./build \
#    --enable-libx264 \
#    --extra-cflags="-I../build/include" \
#    --extra-ldflags="-LIBPATH:../build/lib"