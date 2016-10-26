FROM centos:7

ENV LANG en_US.utf8
ENV LC_ALL en_US.utf8
env HOME /home/builder

RUN yum -y update && yum install -y \
	autoconf \
	automake \
	bzip2 \
	cmake \
	ethtool \
	gcc \
	gcc-c++ \
	git \
	libtool \
	make \
	net-tools \
	openssl-devel \
	perl \
	tar \
	which \
	zlib-devel

ENV FDKAAC_VERSION=0.1.4 \
	 YASM_VERSION=1.3.0 \
	 NASM_VERSION=2.13.02
ENV PREFIX=/usr/local
ENV PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig

COPY build-ffmpeg-dependencies.sh /tmp/build-ffmpeg-dependencies.sh
RUN /tmp/build-ffmpeg-dependencies.sh

WORKDIR /home/builder/src
VOLUME /home/builder/src
