OBJS-$(CONFIG_MLP_DECODER)             += x86/mlpdsp.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += x86/mlpdsp.o
OBJS-$(CONFIG_XMM_CLOBBER_TEST)        += x86/w64xmmtest.o

MMX-OBJS                               += x86/dsputil_mmx.o             \
                                          x86/fdct_mmx.o                \
                                          x86/fmtconvert_mmx.o          \
                                          x86/idct_mmx_xvid.o           \
                                          x86/idct_sse2_xvid.o          \
                                          x86/motion_est_mmx.o          \
                                          x86/mpegvideo_mmx.o           \
                                          x86/simple_idct_mmx.o         \

MMX-OBJS-$(CONFIG_AAC_DECODER)         += x86/sbrdsp_init.o
MMX-OBJS-$(CONFIG_AC3DSP)              += x86/ac3dsp_mmx.o
MMX-OBJS-$(CONFIG_CAVS_DECODER)        += x86/cavsdsp_mmx.o
MMX-OBJS-$(CONFIG_DNXHD_ENCODER)       += x86/dnxhd_mmx.o
MMX-OBJS-$(CONFIG_DWT)                 += x86/snowdsp_mmx.o \
                                          x86/dwt.o
MMX-OBJS-$(CONFIG_ENCODERS)            += x86/dsputilenc_mmx.o
MMX-OBJS-$(CONFIG_FFT)                 += x86/fft.o
MMX-OBJS-$(CONFIG_GPL)                 += x86/idct_mmx.o
MMX-OBJS-$(CONFIG_H264DSP)             += x86/h264dsp_mmx.o
MMX-OBJS-$(CONFIG_H264PRED)            += x86/h264_intrapred_init.o
MMX-OBJS-$(CONFIG_LPC)                 += x86/lpc_mmx.o
MMX-OBJS-$(CONFIG_MPEGAUDIODSP)        += x86/mpegaudiodec_mmx.o
MMX-OBJS-$(CONFIG_PNG_DECODER)         += x86/pngdsp-init.o
MMX-OBJS-$(CONFIG_PRORES_DECODER)      += x86/proresdsp-init.o
MMX-OBJS-$(CONFIG_PRORES_LGPL_DECODER) += x86/proresdsp-init.o
MMX-OBJS-$(CONFIG_RV30_DECODER)        += x86/rv34dsp_init.o
MMX-OBJS-$(CONFIG_RV40_DECODER)        += x86/rv34dsp_init.o            \
                                          x86/rv40dsp_init.o
MMX-OBJS-$(CONFIG_V210_DECODER)        += x86/v210-init.o
MMX-OBJS-$(CONFIG_VC1_DECODER)         += x86/vc1dsp_mmx.o
MMX-OBJS-$(CONFIG_VP5_DECODER)         += x86/vp56dsp_init.o
MMX-OBJS-$(CONFIG_VP6_DECODER)         += x86/vp56dsp_init.o
MMX-OBJS-$(CONFIG_VP8_DECODER)         += x86/vp8dsp-init.o

YASM-OBJS-$(CONFIG_AAC_DECODER)        += x86/sbrdsp.o
YASM-OBJS-$(CONFIG_AC3DSP)             += x86/ac3dsp.o
YASM-OBJS-$(CONFIG_DCT)                += x86/dct32_sse.o
YASM-OBJS-$(CONFIG_DIRAC_DECODER)      += x86/diracdsp_mmx.o x86/diracdsp_yasm.o
YASM-OBJS-$(CONFIG_ENCODERS)           += x86/dsputilenc_yasm.o
YASM-OBJS-FFT-$(HAVE_AMD3DNOW)         += x86/fft_3dn.o
YASM-OBJS-FFT-$(HAVE_AMD3DNOWEXT)      += x86/fft_3dn2.o
YASM-OBJS-FFT-$(HAVE_SSE)              += x86/fft_sse.o
YASM-OBJS-$(CONFIG_FFT)                += x86/fft_mmx.o                 \
                                          $(YASM-OBJS-FFT-yes)

YASM-OBJS-$(CONFIG_DWT)                += x86/dwt_yasm.o
YASM-OBJS-$(CONFIG_H264CHROMA)         += x86/h264_chromamc.o           \
                                          x86/h264_chromamc_10bit.o
YASM-OBJS-$(CONFIG_H264DSP)            += x86/h264_deblock.o            \
                                          x86/h264_deblock_10bit.o      \
                                          x86/h264_idct.o               \
                                          x86/h264_idct_10bit.o         \
                                          x86/h264_weight.o             \
                                          x86/h264_weight_10bit.o
YASM-OBJS-$(CONFIG_H264PRED)           += x86/h264_intrapred.o          \
                                          x86/h264_intrapred_10bit.o
YASM-OBJS-$(CONFIG_H264QPEL)           += x86/h264_qpel_10bit.o
YASM-OBJS-$(CONFIG_MPEGAUDIODSP)       += x86/imdct36_sse.o
YASM-OBJS-$(CONFIG_PNG_DECODER)        += x86/pngdsp.o
YASM-OBJS-$(CONFIG_PRORES_DECODER)     += x86/proresdsp.o
YASM-OBJS-$(CONFIG_PRORES_LGPL_DECODER) += x86/proresdsp.o
YASM-OBJS-$(CONFIG_RV30_DECODER)       += x86/rv34dsp.o
YASM-OBJS-$(CONFIG_RV40_DECODER)       += x86/rv34dsp.o                 \
                                          x86/rv40dsp.o
YASM-OBJS-$(CONFIG_V210_DECODER)       += x86/v210.o
YASM-OBJS-$(CONFIG_VC1_DECODER)        += x86/vc1dsp_yasm.o
YASM-OBJS-$(CONFIG_VP3_DECODER)        += x86/vp3dsp.o
YASM-OBJS-$(CONFIG_VP5_DECODER)        += x86/vp3dsp.o
YASM-OBJS-$(CONFIG_VP6_DECODER)        += x86/vp3dsp.o                  \
                                          x86/vp56dsp.o
YASM-OBJS-$(CONFIG_VP8_DECODER)        += x86/vp8dsp.o

YASM-OBJS                              += x86/dsputil_yasm.o            \
                                          x86/deinterlace.o             \
                                          x86/fmtconvert.o              \
