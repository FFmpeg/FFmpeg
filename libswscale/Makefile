
include ../config.mak

NAME=swscale
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SWSVERSION)
LIBMAJOR=$(SWSMAJOR)
endif

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

OBJS= swscale.o rgb2rgb.o yuv2rgb.o
ifeq ($(TARGET_ALTIVEC),yes)
OBJS+=  yuv2rgb_altivec.o
endif

HEADERS = swscale.h rgb2rgb.h

include ../common.mak

cs_test: cs_test.c $(LIB)

swscale-example: swscale-example.o $(LIB)

clean::
	rm -f cs_test swscale-example
