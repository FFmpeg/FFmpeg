include $(SUBDIR)../config.mak

NAME = swscale
FFLIBS = avutil

HEADERS = swscale.h

OBJS = rgb2rgb.o swscale.o swscale_avoption.o yuv2rgb.o

OBJS-$(ARCH_BFIN)          +=  internal_bfin.o swscale_bfin.o bfin/yuv2rgb_bfin.o
OBJS-$(CONFIG_MLIB)        +=  mlib/yuv2rgb_mlib.o
OBJS-$(HAVE_ALTIVEC)       +=  ppc/yuv2rgb_altivec.o
OBJS-$(HAVE_VIS)           +=  sparc/yuv2rgb_vis.o

MMX-OBJS-$(CONFIG_GPL)     +=  x86/yuv2rgb_mmx.o        \

OBJS-$(HAVE_MMX)           +=  $(MMX-OBJS-yes)

EXAMPLES  = swscale-example
TESTPROGS = colorspace

DIRS = bfin mlib ppc sparc x86

include $(SUBDIR)../subdir.mak

$(SUBDIR)colorspace-test: $(SUBDIR)colorspace-test.o $(SUBDIR)$(LIBNAME)

$(SUBDIR)swscale-example: $(SUBDIR)swscale-example.o $(SUBDIR)$(LIBNAME)
$(SUBDIR)swscale-example: EXTRALIBS += -lm
