OBJS-$(CONFIG_MLP_DECODER)             += x86/mlpdsp.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += x86/mlpdsp.o

YASM-OBJS-FFT-$(HAVE_AMD3DNOW)         += x86/fft_3dn.o
YASM-OBJS-FFT-$(HAVE_AMD3DNOWEXT)      += x86/fft_3dn2.o
YASM-OBJS-FFT-$(HAVE_SSE)              += x86/fft_sse.o
YASM-OBJS-$(CONFIG_FFT)                += x86/fft_mmx.o                 \
                                          $(YASM-OBJS-FFT-yes)
YASM-OBJS-$(CONFIG_GPL)                += x86/h264_deblock_sse2.o       \
                                          x86/h264_idct_sse2.o          \

MMX-OBJS-$(CONFIG_CAVS_DECODER)        += x86/cavsdsp_mmx.o
MMX-OBJS-$(CONFIG_ENCODERS)            += x86/dsputilenc_mmx.o
MMX-OBJS-$(CONFIG_GPL)                 += x86/idct_mmx.o
MMX-OBJS-$(CONFIG_LPC)                 += x86/lpc_mmx.o
MMX-OBJS-$(CONFIG_DWT)                 += x86/snowdsp_mmx.o
MMX-OBJS-$(CONFIG_VC1_DECODER)         += x86/vc1dsp_mmx.o
MMX-OBJS-$(CONFIG_VP3_DECODER)         += x86/vp3dsp_mmx.o              \
                                          x86/vp3dsp_sse2.o
MMX-OBJS-$(CONFIG_VP5_DECODER)         += x86/vp3dsp_mmx.o              \
                                          x86/vp3dsp_sse2.o
MMX-OBJS-$(CONFIG_VP6_DECODER)         += x86/vp3dsp_mmx.o              \
                                          x86/vp3dsp_sse2.o             \
                                          x86/vp6dsp_mmx.o              \
                                          x86/vp6dsp_sse2.o
MMX-OBJS-$(HAVE_YASM)                  += x86/dsputil_yasm.o            \
                                          $(YASM-OBJS-yes)

MMX-OBJS-$(CONFIG_FFT)                 += x86/fft.o

OBJS-$(HAVE_MMX)                       += x86/cpuid.o                   \
                                          x86/dnxhd_mmx.o               \
                                          x86/dsputil_mmx.o             \
                                          x86/fdct_mmx.o                \
                                          x86/idct_mmx_xvid.o           \
                                          x86/idct_sse2_xvid.o          \
                                          x86/motion_est_mmx.o          \
                                          x86/mpegvideo_mmx.o           \
                                          x86/simple_idct_mmx.o         \
