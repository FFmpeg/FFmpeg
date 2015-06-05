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
MIPSFPU-OBJS-$(CONFIG_FFT)                += mips/fft_mips.o
MIPSFPU-OBJS-$(CONFIG_FMTCONVERT)         += mips/fmtconvert_mips.o
OBJS-$(CONFIG_AC3DSP)                     += mips/ac3dsp_mips.o
OBJS-$(CONFIG_AAC_DECODER)                += mips/aacdec_mips.o            \
                                             mips/aacsbr_mips.o            \
                                             mips/sbrdsp_mips.o            \
                                             mips/aacpsdsp_mips.o
MIPSDSPR1-OBJS-$(CONFIG_AAC_ENCODER)      += mips/aaccoder_mips.o
MIPSFPU-OBJS-$(CONFIG_AAC_ENCODER)        += mips/iirfilter_mips.o
OBJS-$(CONFIG_HEVC_DECODER)               += mips/hevcdsp_init_mips.o
OBJS-$(CONFIG_H264DSP)                    += mips/h264dsp_init_mips.o
OBJS-$(CONFIG_H264CHROMA)                 += mips/h264chroma_init_mips.o
MSA-OBJS-$(CONFIG_HEVC_DECODER)           += mips/hevcdsp_msa.o            \
                                             mips/hevc_mc_uni_msa.o        \
                                             mips/hevc_mc_uniw_msa.o       \
                                             mips/hevc_mc_bi_msa.o         \
                                             mips/hevc_mc_biw_msa.o        \
                                             mips/hevc_idct_msa.o
MSA-OBJS-$(CONFIG_H264DSP)                += mips/h264dsp_msa.o
LOONGSON3-OBJS-$(CONFIG_H264DSP)          += mips/h264dsp_mmi.o
LOONGSON3-OBJS-$(CONFIG_H264CHROMA)       += mips/h264chroma_mmi.o
