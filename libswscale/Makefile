include $(SUBDIR)../config.mak

NAME = swscale
FFLIBS = avutil

HEADERS = swscale.h

OBJS = rgb2rgb.o swscale.o swscale_avoption.o yuv2rgb.o

OBJS-$(ARCH_BFIN)          +=  internal_bfin.o swscale_bfin.o yuv2rgb_bfin.o
OBJS-$(CONFIG_MLIB)        +=  yuv2rgb_mlib.o
OBJS-$(HAVE_ALTIVEC)       +=  yuv2rgb_altivec.o
OBJS-$(HAVE_VIS)           +=  yuv2rgb_vis.o

TESTS = cs_test swscale-example

CLEANFILES = cs_test swscale-example

include $(SUBDIR)../subdir.mak

$(SUBDIR)cs_test: $(SUBDIR)cs_test.o $(SUBDIR)$(LIBNAME)

$(SUBDIR)swscale-example: $(SUBDIR)swscale-example.o $(SUBDIR)$(LIBNAME)
$(SUBDIR)swscale-example: EXTRALIBS += -lm
