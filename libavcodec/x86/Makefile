OBJS                                   += x86/constants.o               \

# subsystems
OBJS-$(CONFIG_AC3DSP)                  += x86/ac3dsp_init.o
OBJS-$(CONFIG_AUDIODSP)                += x86/audiodsp_init.o
OBJS-$(CONFIG_BLOCKDSP)                += x86/blockdsp_init.o
OBJS-$(CONFIG_BSWAPDSP)                += x86/bswapdsp_init.o
OBJS-$(CONFIG_DCT)                     += x86/dct_init.o
OBJS-$(CONFIG_FDCTDSP)                 += x86/fdctdsp_init.o
OBJS-$(CONFIG_FFT)                     += x86/fft_init.o
OBJS-$(CONFIG_FLAC_DECODER)            += x86/flacdsp_init.o
OBJS-$(CONFIG_FLAC_ENCODER)            += x86/flacdsp_init.o
OBJS-$(CONFIG_FMTCONVERT)              += x86/fmtconvert_init.o
OBJS-$(CONFIG_H263DSP)                 += x86/h263dsp_init.o
OBJS-$(CONFIG_H264CHROMA)              += x86/h264chroma_init.o
OBJS-$(CONFIG_H264DSP)                 += x86/h264dsp_init.o
OBJS-$(CONFIG_H264PRED)                += x86/h264_intrapred_init.o
OBJS-$(CONFIG_H264QPEL)                += x86/h264_qpel.o
OBJS-$(CONFIG_HPELDSP)                 += x86/hpeldsp_init.o
OBJS-$(CONFIG_LLAUDDSP)                += x86/lossless_audiodsp_init.o
OBJS-$(CONFIG_LLVIDDSP)                += x86/lossless_videodsp_init.o
OBJS-$(CONFIG_HUFFYUVDSP)              += x86/huffyuvdsp_init.o
OBJS-$(CONFIG_HUFFYUVENCDSP)           += x86/huffyuvencdsp_mmx.o
OBJS-$(CONFIG_IDCTDSP)                 += x86/idctdsp_init.o
OBJS-$(CONFIG_LPC)                     += x86/lpc.o
OBJS-$(CONFIG_ME_CMP)                  += x86/me_cmp_init.o
OBJS-$(CONFIG_MPEGAUDIODSP)            += x86/mpegaudiodsp.o
OBJS-$(CONFIG_MPEGVIDEO)               += x86/mpegvideo.o              \
                                          x86/mpegvideodsp.o
OBJS-$(CONFIG_MPEGVIDEOENC)            += x86/mpegvideoenc.o           \
                                          x86/mpegvideoencdsp_init.o
OBJS-$(CONFIG_PIXBLOCKDSP)             += x86/pixblockdsp_init.o
OBJS-$(CONFIG_QPELDSP)                 += x86/qpeldsp_init.o
OBJS-$(CONFIG_VIDEODSP)                += x86/videodsp_init.o
OBJS-$(CONFIG_VP3DSP)                  += x86/vp3dsp_init.o
OBJS-$(CONFIG_XMM_CLOBBER_TEST)        += x86/w64xmmtest.o

# decoders/encoders
OBJS-$(CONFIG_AAC_DECODER)             += x86/sbrdsp_init.o
OBJS-$(CONFIG_ADPCM_G722_DECODER)      += x86/g722dsp_init.o
OBJS-$(CONFIG_ADPCM_G722_ENCODER)      += x86/g722dsp_init.o
OBJS-$(CONFIG_APNG_DECODER)            += x86/pngdsp_init.o
OBJS-$(CONFIG_CAVS_DECODER)            += x86/cavsdsp.o
OBJS-$(CONFIG_DCA_DECODER)             += x86/dcadsp_init.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += x86/dnxhdenc_init.o
OBJS-$(CONFIG_HEVC_DECODER)            += x86/hevcdsp_init.o
OBJS-$(CONFIG_MLP_DECODER)             += x86/mlpdsp_init.o
OBJS-$(CONFIG_MPEG4_DECODER)           += x86/xvididct_init.o
OBJS-$(CONFIG_PNG_DECODER)             += x86/pngdsp_init.o
OBJS-$(CONFIG_PRORES_DECODER)          += x86/proresdsp_init.o
OBJS-$(CONFIG_PRORES_LGPL_DECODER)     += x86/proresdsp_init.o
OBJS-$(CONFIG_RV30_DECODER)            += x86/rv34dsp_init.o
OBJS-$(CONFIG_RV40_DECODER)            += x86/rv34dsp_init.o            \
                                          x86/rv40dsp_init.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += x86/svq1enc_init.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += x86/mlpdsp_init.o
OBJS-$(CONFIG_TTA_DECODER)             += x86/ttadsp_init.o
OBJS-$(CONFIG_V210_DECODER)            += x86/v210-init.o
OBJS-$(CONFIG_V210_ENCODER)            += x86/v210enc_init.o
OBJS-$(CONFIG_VC1_DECODER)             += x86/vc1dsp_init.o
OBJS-$(CONFIG_VORBIS_DECODER)          += x86/vorbisdsp_init.o
OBJS-$(CONFIG_VP6_DECODER)             += x86/vp6dsp_init.o
OBJS-$(CONFIG_VP7_DECODER)             += x86/vp8dsp_init.o
OBJS-$(CONFIG_VP8_DECODER)             += x86/vp8dsp_init.o
OBJS-$(CONFIG_VP9_DECODER)             += x86/vp9dsp_init.o
OBJS-$(CONFIG_WEBP_DECODER)            += x86/vp8dsp_init.o


# GCC inline assembly optimizations
# subsystems
MMX-OBJS-$(CONFIG_DIRAC_DECODER)       += x86/dirac_dwt.o
MMX-OBJS-$(CONFIG_FDCTDSP)             += x86/fdct.o
MMX-OBJS-$(CONFIG_IDCTDSP)             += x86/simple_idct.o

# decoders/encoders
MMX-OBJS-$(CONFIG_SNOW_DECODER)        += x86/snowdsp.o
MMX-OBJS-$(CONFIG_SNOW_ENCODER)        += x86/snowdsp.o
MMX-OBJS-$(CONFIG_VC1_DECODER)         += x86/vc1dsp_mmx.o


# YASM optimizations
YASM-OBJS                              += x86/deinterlace.o             \

# subsystems
YASM-OBJS-$(CONFIG_AC3DSP)             += x86/ac3dsp.o
YASM-OBJS-$(CONFIG_AUDIODSP)           += x86/audiodsp.o
YASM-OBJS-$(CONFIG_BLOCKDSP)           += x86/blockdsp.o
YASM-OBJS-$(CONFIG_BSWAPDSP)           += x86/bswapdsp.o
YASM-OBJS-$(CONFIG_DCT)                += x86/dct32.o
YASM-OBJS-$(CONFIG_DIRAC_DECODER)      += x86/diracdsp_mmx.o x86/diracdsp_yasm.o\
                                          x86/dwt_yasm.o
YASM-OBJS-$(CONFIG_DNXHD_ENCODER)      += x86/dnxhdenc.o
YASM-OBJS-$(CONFIG_FFT)                += x86/fft.o
YASM-OBJS-$(CONFIG_FLAC_DECODER)       += x86/flacdsp.o
ifdef CONFIG_GPL
YASM-OBJS-$(CONFIG_FLAC_ENCODER)       += x86/flac_dsp_gpl.o
endif
YASM-OBJS-$(CONFIG_FMTCONVERT)         += x86/fmtconvert.o
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
YASM-OBJS-$(CONFIG_HUFFYUVDSP)         += x86/huffyuvdsp.o
YASM-OBJS-$(CONFIG_IDCTDSP)            += x86/idctdsp.o
YASM-OBJS-$(CONFIG_LLAUDDSP)           += x86/lossless_audiodsp.o
YASM-OBJS-$(CONFIG_LLVIDDSP)           += x86/lossless_videodsp.o
YASM-OBJS-$(CONFIG_ME_CMP)             += x86/me_cmp.o
YASM-OBJS-$(CONFIG_MPEGAUDIODSP)       += x86/imdct36.o
YASM-OBJS-$(CONFIG_MPEGVIDEOENC)       += x86/mpegvideoencdsp.o
YASM-OBJS-$(CONFIG_PIXBLOCKDSP)        += x86/pixblockdsp.o
YASM-OBJS-$(CONFIG_QPELDSP)            += x86/qpeldsp.o                 \
                                          x86/fpel.o                    \
                                          x86/qpel.o
YASM-OBJS-$(CONFIG_VIDEODSP)           += x86/videodsp.o
YASM-OBJS-$(CONFIG_VP3DSP)             += x86/vp3dsp.o

# decoders/encoders
YASM-OBJS-$(CONFIG_AAC_DECODER)        += x86/sbrdsp.o
YASM-OBJS-$(CONFIG_ADPCM_G722_DECODER) += x86/g722dsp.o
YASM-OBJS-$(CONFIG_ADPCM_G722_ENCODER) += x86/g722dsp.o
YASM-OBJS-$(CONFIG_APNG_DECODER)       += x86/pngdsp.o
YASM-OBJS-$(CONFIG_DCA_DECODER)        += x86/dcadsp.o
YASM-OBJS-$(CONFIG_HEVC_DECODER)       += x86/hevc_mc.o                 \
                                          x86/hevc_deblock.o            \
                                          x86/hevc_idct.o               \
                                          x86/hevc_res_add.o            \
                                          x86/hevc_sao.o
YASM-OBJS-$(CONFIG_MLP_DECODER)        += x86/mlpdsp.o
YASM-OBJS-$(CONFIG_MPEG4_DECODER)      += x86/xvididct.o
YASM-OBJS-$(CONFIG_PNG_DECODER)        += x86/pngdsp.o
YASM-OBJS-$(CONFIG_PRORES_DECODER)     += x86/proresdsp.o
YASM-OBJS-$(CONFIG_PRORES_LGPL_DECODER) += x86/proresdsp.o
YASM-OBJS-$(CONFIG_RV30_DECODER)       += x86/rv34dsp.o
YASM-OBJS-$(CONFIG_RV40_DECODER)       += x86/rv34dsp.o                 \
                                          x86/rv40dsp.o
YASM-OBJS-$(CONFIG_SVQ1_ENCODER)       += x86/svq1enc.o
YASM-OBJS-$(CONFIG_TRUEHD_DECODER)     += x86/mlpdsp.o
YASM-OBJS-$(CONFIG_TTA_DECODER)        += x86/ttadsp.o
YASM-OBJS-$(CONFIG_V210_ENCODER)       += x86/v210enc.o
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
