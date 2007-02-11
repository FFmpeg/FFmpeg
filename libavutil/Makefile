#
# libavutil Makefile
#
include ../config.mak

CFLAGS+=-DBUILD_AVUTIL

OBJS= mathematics.o \
      rational.o \
      intfloat_readwrite.o \
      crc.o \
      md5.o \
      lls.o \
      adler32.o \
      log.o \
      mem.o \
      fifo.o \
      tree.o \
      lzo.o \
      random.o \
      aes.o \

HEADERS = avutil.h common.h mathematics.h integer.h rational.h \
          intfloat_readwrite.h md5.h adler32.h log.h fifo.h lzo.h \
          random.h

NAME=avutil
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(LAVUVERSION)
LIBMAJOR=$(LAVUMAJOR)
endif

include ../common.mak
