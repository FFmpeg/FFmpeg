include $(SUBDIR)../config.mak

NAME = swscale
FFLIBS = avutil

OBJS = rgb2rgb.o swscale.o

OBJS-$(ARCH_BFIN)          +=  swscale_bfin.o yuv2rgb_bfin.o
OBJS-$(CONFIG_GPL)         +=  yuv2rgb.o
OBJS-$(CONFIG_MLIB)        +=  yuv2rgb_mlib.c
OBJS-$(HAVE_ALTIVEC)       +=  yuv2rgb_altivec.o
OBJS-$(HAVE_VIS)           +=  yuv2rgb_vis.c

ASM_OBJS-$(ARCH_BFIN)      +=  internal_bfin.o

HEADERS = swscale.h rgb2rgb.h

CLEANFILES = cs_test swscale-example

include $(SUBDIR)../subdir.mak

$(SUBDIR)cs_test: $(SUBDIR)cs_test.o $(SUBDIR)$(LIBNAME)

$(SUBDIR)swscale-example: $(SUBDIR)swscale-example.o $(SUBDIR)$(LIBNAME)
$(SUBDIR)swscale-example: EXTRALIBS += -lm
