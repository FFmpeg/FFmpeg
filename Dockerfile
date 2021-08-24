# execute through "docker build -t ffmpeg:<your-version-id> ."

FROM alpine:3.7 AS ffmpeg
MAINTAINER eric@turingvideo.net

# artifacts you may want to copy out from this docker container:
#
#  /root/dist/bin/*                        (this is where ffmpeg lives)
#  /root/dist/include/lib*/*.h             (headers for linking your custom binary to libraries)
#  /root/dist/lib/lib*.[a|so]              (link your custom binary to these libraries)
#  /root/dist/lib/pkgconfig/lib*.pc        (files that pkg-config uses)
#  /root/dist/share/ffmpeg/*.preset        (example settings to use as guides)
#  /root/dist/share/ffmpeg/examples/*.c    (example custom binaries to use as guides)

COPY . .

RUN apk add build-base nasm yasm openssl-dev && \
make distclean && \
./configure \
	--prefix=dist \
	--disable-debug \
	--disable-doc \
	--disable-ffplay \
	--disable-ffprobe \
	--enable-shared \
	--enable-pthreads \
	--enable-small \
	--enable-version3 \
	--extra-cflags= \
	--extra-ldflags= \
	--enable-gpl \
	--enable-nonfree \
	--enable-openssl \
	--disable-libx264 \
	--disable-libx265 \
	--disable-libopencore-amrnb \
	--disable-libopencore-amrwb \
	--disable-libfreetype \
	--disable-libvidstab \
	--disable-libmp3lame \
	--disable-libopenjpeg \
	--disable-libopus \
	--disable-libtheora \
	--disable-libvorbis \
	--disable-libvpx \
	--disable-libxvid \
	--disable-libfdk_aac \
	--disable-libkvazaar \
	--disable-libaom && \
make && \
make install && \
make clean


ENTRYPOINT ["/bin/bash", "/root/ffmpeg"]
