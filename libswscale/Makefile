
include ../config.mak

NAME=swscale
LIBVERSION=$(SWSVERSION)
LIBMAJOR=$(SWSMAJOR)

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

OBJS= swscale.o rgb2rgb.o

OBJS-$(HAVE_ALTIVEC)       +=  yuv2rgb_altivec.o
OBJS-$(CONFIG_GPL)         +=  yuv2rgb.o

OBJS-$(ARCH_BFIN)          +=  yuv2rgb_bfin.o \
                               swscale_bfin.o \

ASM_OBJS-$(ARCH_BFIN)      += internal_bfin.o

HEADERS = swscale.h rgb2rgb.h

include ../common.mak

cs_test: cs_test.o $(LIB)

swscale-example: swscale-example.o $(LIB)
swscale-example: EXTRALIBS += -lm

clean::
	rm -f cs_test swscale-example
