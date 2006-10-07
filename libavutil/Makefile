#
# libavutil Makefile
#
include ../config.mak

CFLAGS=-DHAVE_AV_CONFIG_H -DBUILD_AVUTIL -I$(BUILD_ROOT) $(OPTFLAGS) \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_ISOC9X_SOURCE

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

HEADERS = avutil.h common.h mathematics.h integer.h rational.h \
          intfloat_readwrite.h md5.h adler32.h log.h fifo.h

NAME=avutil
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(LAVUVERSION)
LIBMAJOR=$(LAVUMAJOR)
endif

include $(SRC_PATH)/common.mak
