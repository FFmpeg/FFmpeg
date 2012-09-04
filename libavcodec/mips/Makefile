MMI-OBJS += mips/dsputil_mmi.o                                          \
            mips/idct_mmi.o                                             \
            mips/mpegvideo_mmi.o

MIPSFPU-OBJS-$(CONFIG_AMRNB_DECODER)      += mips/acelp_filters_mips.o     \
                                             mips/celp_filters_mips.o      \
                                             mips/celp_math_mips.o         \
                                             mips/acelp_vectors_mips.o
MIPSFPU-OBJS-$(CONFIG_AMRWB_DECODER)      += mips/acelp_filters_mips.o     \
                                             mips/celp_filters_mips.o      \
                                             mips/amrwbdec_mips.o          \
                                             mips/celp_math_mips.o         \
                                             mips/acelp_vectors_mips.o
MIPSFPU-OBJS-$(CONFIG_MPEGAUDIODSP)       += mips/mpegaudiodsp_mips_float.o
MIPSDSPR1-OBJS-$(CONFIG_MPEGAUDIODSP)     += mips/mpegaudiodsp_mips_fixed.o
OBJS-$(CONFIG_FFT)                        += mips/fft_init_table.o
MIPSFPU-OBJS-$(CONFIG_FFT)                += mips/fft_mips.o
MIPSFPU-OBJS-$(HAVE_INLINE_ASM)           += mips/fmtconvert_mips.o
MIPSFPU-OBJS-$(HAVE_INLINE_ASM)           += mips/dsputil_mips.o
