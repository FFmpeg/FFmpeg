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
MIPSDSP-OBJS-$(CONFIG_MPEGAUDIODSP)       += mips/mpegaudiodsp_mips_fixed.o
MIPSFPU-OBJS-$(CONFIG_FFT)                += mips/fft_mips.o
MIPSFPU-OBJS-$(CONFIG_FMTCONVERT)         += mips/fmtconvert_mips.o
OBJS-$(CONFIG_AC3DSP)                     += mips/ac3dsp_mips.o
OBJS-$(CONFIG_AAC_DECODER)                += mips/aacdec_mips.o            \
                                             mips/aacsbr_mips.o            \
                                             mips/sbrdsp_mips.o            \
                                             mips/aacpsdsp_mips.o
MIPSDSP-OBJS-$(CONFIG_AAC_ENCODER)        += mips/aaccoder_mips.o
MIPSFPU-OBJS-$(CONFIG_AAC_ENCODER)        += mips/iirfilter_mips.o
OBJS-$(CONFIG_HEVC_DECODER)               += mips/hevcdsp_init_mips.o      \
                                             mips/hevcpred_init_mips.o
OBJS-$(CONFIG_VP9_DECODER)                += mips/vp9dsp_init_mips.o
OBJS-$(CONFIG_VP8_DECODER)                += mips/vp8dsp_init_mips.o
OBJS-$(CONFIG_VP3DSP)                     += mips/vp3dsp_init_mips.o
OBJS-$(CONFIG_H264DSP)                    += mips/h264dsp_init_mips.o
OBJS-$(CONFIG_H264QPEL)                   += mips/h264qpel_init_mips.o
OBJS-$(CONFIG_H264CHROMA)                 += mips/h264chroma_init_mips.o
OBJS-$(CONFIG_H264PRED)                   += mips/h264pred_init_mips.o
OBJS-$(CONFIG_H263DSP)                    += mips/h263dsp_init_mips.o
OBJS-$(CONFIG_QPELDSP)                    += mips/qpeldsp_init_mips.o
OBJS-$(CONFIG_HPELDSP)                    += mips/hpeldsp_init_mips.o
OBJS-$(CONFIG_BLOCKDSP)                   += mips/blockdsp_init_mips.o
OBJS-$(CONFIG_PIXBLOCKDSP)                += mips/pixblockdsp_init_mips.o
OBJS-$(CONFIG_IDCTDSP)                    += mips/idctdsp_init_mips.o
OBJS-$(CONFIG_MPEGVIDEO)                  += mips/mpegvideo_init_mips.o
OBJS-$(CONFIG_MPEGVIDEOENC)               += mips/mpegvideoencdsp_init_mips.o
OBJS-$(CONFIG_ME_CMP)                     += mips/me_cmp_init_mips.o
OBJS-$(CONFIG_MPEG4_DECODER)              += mips/xvididct_init_mips.o
OBJS-$(CONFIG_VC1DSP)                     += mips/vc1dsp_init_mips.o
OBJS-$(CONFIG_WMV2DSP)                    += mips/wmv2dsp_init_mips.o
OBJS-$(CONFIG_VIDEODSP)                   += mips/videodsp_init.o
MSA-OBJS-$(CONFIG_HEVC_DECODER)           += mips/hevcdsp_msa.o            \
                                             mips/hevc_mc_uni_msa.o        \
                                             mips/hevc_mc_uniw_msa.o       \
                                             mips/hevc_mc_bi_msa.o         \
                                             mips/hevc_mc_biw_msa.o        \
                                             mips/hevc_idct_msa.o          \
                                             mips/hevc_lpf_sao_msa.o       \
                                             mips/hevcpred_msa.o
MSA-OBJS-$(CONFIG_VP9_DECODER)            += mips/vp9_mc_msa.o             \
                                             mips/vp9_lpf_msa.o            \
                                             mips/vp9_idct_msa.o           \
                                             mips/vp9_intra_msa.o
MSA-OBJS-$(CONFIG_VP8_DECODER)            += mips/vp8_mc_msa.o             \
                                             mips/vp8_idct_msa.o           \
                                             mips/vp8_lpf_msa.o
MSA-OBJS-$(CONFIG_VP3DSP)                 += mips/vp3dsp_idct_msa.o
MSA-OBJS-$(CONFIG_H264DSP)                += mips/h264dsp_msa.o            \
                                             mips/h264idct_msa.o
MSA-OBJS-$(CONFIG_H264QPEL)               += mips/h264qpel_msa.o
MSA-OBJS-$(CONFIG_H264CHROMA)             += mips/h264chroma_msa.o
MSA-OBJS-$(CONFIG_H264PRED)               += mips/h264pred_msa.o
MSA-OBJS-$(CONFIG_H263DSP)                += mips/h263dsp_msa.o
MSA-OBJS-$(CONFIG_QPELDSP)                += mips/qpeldsp_msa.o
MSA-OBJS-$(CONFIG_HPELDSP)                += mips/hpeldsp_msa.o
MSA-OBJS-$(CONFIG_BLOCKDSP)               += mips/blockdsp_msa.o
MSA-OBJS-$(CONFIG_PIXBLOCKDSP)            += mips/pixblockdsp_msa.o
MSA-OBJS-$(CONFIG_IDCTDSP)                += mips/idctdsp_msa.o           \
                                             mips/simple_idct_msa.o
MSA-OBJS-$(CONFIG_MPEGVIDEO)              += mips/mpegvideo_msa.o
MSA-OBJS-$(CONFIG_MPEGVIDEOENC)           += mips/mpegvideoencdsp_msa.o
MSA-OBJS-$(CONFIG_ME_CMP)                 += mips/me_cmp_msa.o
MSA-OBJS-$(CONFIG_VC1_DECODER)            += mips/vc1dsp_msa.o

MMI-OBJS                                  += mips/constants.o
MMI-OBJS-$(CONFIG_H264DSP)                += mips/h264dsp_mmi.o
MMI-OBJS-$(CONFIG_H264CHROMA)             += mips/h264chroma_mmi.o
MMI-OBJS-$(CONFIG_H264PRED)               += mips/h264pred_mmi.o
MMI-OBJS-$(CONFIG_MPEGVIDEO)              += mips/mpegvideo_mmi.o
MMI-OBJS-$(CONFIG_IDCTDSP)                += mips/idctdsp_mmi.o           \
                                             mips/simple_idct_mmi.o
MMI-OBJS-$(CONFIG_MPEG4_DECODER)          += mips/xvid_idct_mmi.o
MMI-OBJS-$(CONFIG_BLOCKDSP)               += mips/blockdsp_mmi.o
MMI-OBJS-$(CONFIG_PIXBLOCKDSP)            += mips/pixblockdsp_mmi.o
MMI-OBJS-$(CONFIG_H264QPEL)               += mips/h264qpel_mmi.o
MMI-OBJS-$(CONFIG_VP8_DECODER)            += mips/vp8dsp_mmi.o
MMI-OBJS-$(CONFIG_HPELDSP)                += mips/hpeldsp_mmi.o
MMI-OBJS-$(CONFIG_VC1_DECODER)            += mips/vc1dsp_mmi.o
MMI-OBJS-$(CONFIG_WMV2DSP)                += mips/wmv2dsp_mmi.o
MMI-OBJS-$(CONFIG_HEVC_DECODER)           += mips/hevcdsp_mmi.o
MMI-OBJS-$(CONFIG_VP3DSP)                 += mips/vp3dsp_idct_mmi.o
MMI-OBJS-$(CONFIG_VP9_DECODER)            += mips/vp9_mc_mmi.o
