include $(SUBDIR)../config.mak

NAME = swscale

HEADERS = swscale.h                                                     \
          version.h                                                     \

OBJS = hscale_fast_bilinear.o                           \
       input.o                                          \
       options.o                                        \
       output.o                                         \
       rgb2rgb.o                                        \
       swscale.o                                        \
       swscale_unscaled.o                               \
       utils.o                                          \
       yuv2rgb.o                                        \

OBJS-$(CONFIG_SHARED)        += log2_tab.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += swscaleres.o

TESTPROGS = colorspace                                                  \
            swscale                                                     \
