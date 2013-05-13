$(SUBDIR)x86/swscale_mmx.o: CFLAGS += $(NOREDZONE_FLAGS)

OBJS-$(CONFIG_XMM_CLOBBER_TEST) += x86/w64xmmtest.o

MMX-OBJS                        += x86/rgb2rgb.o                        \
                                   x86/swscale.o                        \
                                   x86/yuv2rgb.o                        \

YASM-OBJS                       += x86/input.o                          \
                                   x86/output.o                         \
                                   x86/scale.o                          \
