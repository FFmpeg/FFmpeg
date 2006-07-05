#
# libavutil Makefile
#
include ../config.mak

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -DHAVE_AV_CONFIG_H -DBUILD_AVUTIL -I.. \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= mathematics.o \
      rational.o \
      intfloat_readwrite.o \
      crc.o \
      md5.o \

HEADERS = avutil.h common.h mathematics.h integer.h rational.h \
          intfloat_readwrite.h md5.h

NAME=avutil
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(LAVUVERSION)
LIBMAJOR=$(LAVUMAJOR)
endif

include $(SRC_PATH)/common.mak
