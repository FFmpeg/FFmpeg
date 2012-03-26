include $(SUBDIR)../config.mak

NAME = swscale
FFLIBS = avutil

HEADERS = swscale.h

OBJS = input.o                                          \
       options.o                                        \
       output.o                                         \
       rgb2rgb.o                                        \
       swscale.o                                        \
       swscale_unscaled.o                               \
       utils.o                                          \
       yuv2rgb.o                                        \

OBJS-$(ARCH_BFIN)          +=  bfin/internal_bfin.o     \
                               bfin/swscale_bfin.o      \
                               bfin/yuv2rgb_bfin.o
OBJS-$(HAVE_ALTIVEC)       +=  ppc/swscale_altivec.o    \
                               ppc/yuv2rgb_altivec.o    \
                               ppc/yuv2yuv_altivec.o
OBJS-$(HAVE_MMX)           +=  x86/rgb2rgb.o            \
                               x86/swscale_mmx.o        \
                               x86/yuv2rgb_mmx.o
OBJS-$(HAVE_VIS)           +=  sparc/yuv2rgb_vis.o
MMX-OBJS-$(HAVE_YASM)      +=  x86/input.o              \
                               x86/output.o             \
                               x86/scale.o

$(SUBDIR)x86/swscale_mmx.o: CFLAGS += $(NOREDZONE_FLAGS)

OBJS-$(CONFIG_XMM_CLOBBER_TEST) += x86/w64xmmtest.o

TESTPROGS = colorspace swscale
