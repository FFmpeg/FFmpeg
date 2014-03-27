OBJS                                   += x86/constants.o               \
                                          x86/fmtconvert_init.o         \

OBJS-$(CONFIG_AC3DSP)                  += x86/ac3dsp_init.o
OBJS-$(CONFIG_DCT)                     += x86/dct_init.o
OBJS-$(CONFIG_DSPUTIL)                 += x86/dsputil_init.o            \
                                          x86/dsputil_x86.o
OBJS-$(CONFIG_ENCODERS)                += x86/dsputilenc_mmx.o          \
                                          x86/fdct.o                    \
                                          x86/motion_est.o
OBJS-$(CONFIG_FFT)                     += x86/fft_init.o
OBJS-$(CONFIG_FLAC_DECODER)            += x86/flacdsp_init.o
OBJS-$(CONFIG_FLAC_ENCODER)            += x86/flacdsp_init.o
OBJS-$(CONFIG_H263DSP)                 += x86/h263dsp_init.o
OBJS-$(CONFIG_H264CHROMA)              += x86/h264chroma_init.o
OBJS-$(CONFIG_H264DSP)                 += x86/h264dsp_init.o
OBJS-$(CONFIG_H264PRED)                += x86/h264_intrapred_init.o
OBJS-$(CONFIG_H264QPEL)                += x86/h264_qpel.o
OBJS-$(CONFIG_HPELDSP)                 += x86/hpeldsp_init.o
OBJS-$(CONFIG_LLVIDDSP)                += x86/lossless_videodsp_init.o
OBJS-$(CONFIG_LPC)                     += x86/lpc.o
OBJS-$(CONFIG_MPEGAUDIODSP)            += x86/mpegaudiodsp.o
OBJS-$(CONFIG_MPEGVIDEO)               += x86/mpegvideo.o
OBJS-$(CONFIG_MPEGVIDEOENC)            += x86/mpegvideoenc.o
OBJS-$(CONFIG_VIDEODSP)                += x86/videodsp_init.o
OBJS-$(CONFIG_VP3DSP)                  += x86/vp3dsp_init.o
OBJS-$(CONFIG_XMM_CLOBBER_TEST)        += x86/w64xmmtest.o

OBJS-$(CONFIG_AAC_DECODER)             += x86/sbrdsp_init.o
OBJS-$(CONFIG_CAVS_DECODER)            += x86/cavsdsp.o
OBJS-$(CONFIG_DCA_DECODER)             += x86/dcadsp_init.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += x86/dnxhdenc_init.o
OBJS-$(CONFIG_MLP_DECODER)             += x86/mlpdsp.o
OBJS-$(CONFIG_PNG_DECODER)             += x86/pngdsp_init.o
OBJS-$(CONFIG_PRORES_DECODER)          += x86/proresdsp_init.o
OBJS-$(CONFIG_PRORES_LGPL_DECODER)     += x86/proresdsp_init.o
OBJS-$(CONFIG_RV30_DECODER)            += x86/rv34dsp_init.o
OBJS-$(CONFIG_RV40_DECODER)            += x86/rv34dsp_init.o            \
                                          x86/rv40dsp_init.o
OBJS-$(CONFIG_V210_DECODER)            += x86/v210-init.o
OBJS-$(CONFIG_TTA_DECODER)             += x86/ttadsp_init.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += x86/mlpdsp.o
OBJS-$(CONFIG_VC1_DECODER)             += x86/vc1dsp_init.o
OBJS-$(CONFIG_VORBIS_DECODER)          += x86/vorbisdsp_init.o
OBJS-$(CONFIG_VP6_DECODER)             += x86/vp6dsp_init.o
OBJS-$(CONFIG_VP7_DECODER)             += x86/vp8dsp_init.o
OBJS-$(CONFIG_VP8_DECODER)             += x86/vp8dsp_init.o
OBJS-$(CONFIG_VP9_DECODER)             += x86/vp9dsp_init.o
OBJS-$(CONFIG_WEBP_DECODER)            += x86/vp8dsp_init.o

MMX-OBJS-$(CONFIG_DSPUTIL)             += x86/dsputil_mmx.o             \
                                          x86/fpel_mmx.o                \
                                          x86/idct_mmx_xvid.o           \
                                          x86/idct_sse2_xvid.o          \
                                          x86/simple_idct.o
MMX-OBJS-$(CONFIG_DIRAC_DECODER)       += x86/dirac_dwt.o
MMX-OBJS-$(CONFIG_HPELDSP)             += x86/fpel_mmx.o                \
                                          x86/hpeldsp_mmx.o

MMX-OBJS-$(CONFIG_SNOW_DECODER)        += x86/snowdsp.o
MMX-OBJS-$(CONFIG_SNOW_ENCODER)        += x86/snowdsp.o
MMX-OBJS-$(CONFIG_VC1_DECODER)         += x86/vc1dsp_mmx.o

YASM-OBJS                              += x86/deinterlace.o             \
                                          x86/fmtconvert.o              \

YASM-OBJS-$(CONFIG_AC3DSP)             += x86/ac3dsp.o
YASM-OBJS-$(CONFIG_DCT)                += x86/dct32.o
YASM-OBJS-$(CONFIG_DIRAC_DECODER)      += x86/diracdsp_mmx.o x86/diracdsp_yasm.o\
                                          x86/dwt_yasm.o
YASM-OBJS-$(CONFIG_DNXHD_ENCODER)      += x86/dnxhdenc.o
YASM-OBJS-$(CONFIG_DSPUTIL)            += x86/dsputil.o                 \
                                          x86/fpel.o                    \
                                          x86/mpeg4qpel.o               \
                                          x86/qpel.o
YASM-OBJS-$(CONFIG_ENCODERS)           += x86/dsputilenc.o
YASM-OBJS-$(CONFIG_FFT)                += x86/fft.o
YASM-OBJS-$(CONFIG_FLAC_DECODER)       += x86/flacdsp.o
YASM-OBJS-$(CONFIG_H263DSP)            += x86/h263_loopfilter.o
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
YASM-OBJS-$(CONFIG_H264QPEL)           += x86/h264_qpel_8bit.o          \
                                          x86/h264_qpel_10bit.o         \
                                          x86/fpel.o                    \
                                          x86/qpel.o
YASM-OBJS-$(CONFIG_HPELDSP)            += x86/fpel.o                    \
                                          x86/hpeldsp.o
YASM-OBJS-$(CONFIG_LLVIDDSP)           += x86/lossless_videodsp.o
YASM-OBJS-$(CONFIG_MPEGAUDIODSP)       += x86/imdct36.o
YASM-OBJS-$(CONFIG_VIDEODSP)           += x86/videodsp.o
YASM-OBJS-$(CONFIG_VP3DSP)             += x86/vp3dsp.o

YASM-OBJS-$(CONFIG_AAC_DECODER)        += x86/sbrdsp.o
YASM-OBJS-$(CONFIG_DCA_DECODER)        += x86/dcadsp.o
YASM-OBJS-$(CONFIG_PNG_DECODER)        += x86/pngdsp.o
YASM-OBJS-$(CONFIG_PRORES_DECODER)     += x86/proresdsp.o
YASM-OBJS-$(CONFIG_PRORES_LGPL_DECODER) += x86/proresdsp.o
YASM-OBJS-$(CONFIG_RV30_DECODER)       += x86/rv34dsp.o
YASM-OBJS-$(CONFIG_RV40_DECODER)       += x86/rv34dsp.o                 \
                                          x86/rv40dsp.o
YASM-OBJS-$(CONFIG_TTA_DECODER)        += x86/ttadsp.o
YASM-OBJS-$(CONFIG_V210_DECODER)       += x86/v210.o
YASM-OBJS-$(CONFIG_VC1_DECODER)        += x86/vc1dsp.o
YASM-OBJS-$(CONFIG_VORBIS_DECODER)     += x86/vorbisdsp.o
YASM-OBJS-$(CONFIG_VP6_DECODER)        += x86/vp6dsp.o
YASM-OBJS-$(CONFIG_VP7_DECODER)        += x86/vp8dsp.o                  \
                                          x86/vp8dsp_loopfilter.o
YASM-OBJS-$(CONFIG_VP8_DECODER)        += x86/vp8dsp.o                  \
                                          x86/vp8dsp_loopfilter.o
YASM-OBJS-$(CONFIG_VP9_DECODER)        += x86/vp9intrapred.o            \
                                          x86/vp9itxfm.o                \
                                          x86/vp9lpf.o                  \
                                          x86/vp9mc.o
YASM-OBJS-$(CONFIG_WEBP_DECODER)       += x86/vp8dsp.o
