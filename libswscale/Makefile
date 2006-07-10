
include ../config.mak

NAME=swscale
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SWSVERSION)
LIBMAJOR=$(SWSMAJOR)
endif

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
       -D_GNU_SOURCE

OBJS= swscale.o rgb2rgb.o yuv2rgb.o
ifeq ($(TARGET_ALTIVEC),yes)
OBJS+=  yuv2rgb_altivec.o
endif

HEADERS = swscale.h rgb2rgb.h

include $(SRC_PATH)/common.mak
