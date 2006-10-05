
include ../config.mak

NAME=swscale
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SWSVERSION)
LIBMAJOR=$(SWSMAJOR)
endif

# NOTE: -I.. is needed to include config.h
CFLAGS=-I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil $(OPTFLAGS) \
       -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
       -D_ISOC9X_SOURCE

OBJS= swscale.o rgb2rgb.o yuv2rgb.o
ifeq ($(TARGET_ALTIVEC),yes)
OBJS+=  yuv2rgb_altivec.o
endif

HEADERS = swscale.h rgb2rgb.h

include $(SRC_PATH)/common.mak

cs_test: cs_test.c $(LIB)

swscale-example: swscale-example.o $(LIB)

clean::
	rm -f cs_test swscale-example
