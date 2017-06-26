OBJS                                   += x86/constants.o               \

# subsystems
OBJS-$(CONFIG_AC3DSP)                  += x86/ac3dsp_init.o
OBJS-$(CONFIG_AUDIODSP)                += x86/audiodsp_init.o
OBJS-$(CONFIG_BLOCKDSP)                += x86/blockdsp_init.o
OBJS-$(CONFIG_BSWAPDSP)                += x86/bswapdsp_init.o
OBJS-$(CONFIG_DCT)                     += x86/dct_init.o
OBJS-$(CONFIG_DIRAC_DECODER)           += x86/diracdsp_init.o           \
                                          x86/dirac_dwt_init.o
OBJS-$(CONFIG_FDCTDSP)                 += x86/fdctdsp_init.o
OBJS-$(CONFIG_FFT)                     += x86/fft_init.o
OBJS-$(CONFIG_FLACDSP)                 += x86/flacdsp_init.o
OBJS-$(CONFIG_FMTCONVERT)              += x86/fmtconvert_init.o
OBJS-$(CONFIG_H263DSP)                 += x86/h263dsp_init.o
OBJS-$(CONFIG_H264CHROMA)              += x86/h264chroma_init.o
OBJS-$(CONFIG_H264DSP)                 += x86/h264dsp_init.o
OBJS-$(CONFIG_H264PRED)                += x86/h264_intrapred_init.o
OBJS-$(CONFIG_H264QPEL)                += x86/h264_qpel.o
OBJS-$(CONFIG_HPELDSP)                 += x86/hpeldsp_init.o
OBJS-$(CONFIG_LLAUDDSP)                += x86/lossless_audiodsp_init.o
OBJS-$(CONFIG_LLVIDDSP)                += x86/lossless_videodsp_init.o
OBJS-$(CONFIG_LLVIDENCDSP)             += x86/lossless_videoencdsp_init.o
OBJS-$(CONFIG_HUFFYUVDSP)              += x86/huffyuvdsp_init.o
OBJS-$(CONFIG_HUFFYUVENCDSP)           += x86/huffyuvencdsp_init.o
OBJS-$(CONFIG_IDCTDSP)                 += x86/idctdsp_init.o
OBJS-$(CONFIG_LPC)                     += x86/lpc.o
OBJS-$(CONFIG_MDCT15)                  += x86/mdct15_init.o
OBJS-$(CONFIG_ME_CMP)                  += x86/me_cmp_init.o
OBJS-$(CONFIG_MPEGAUDIODSP)            += x86/mpegaudiodsp.o
OBJS-$(CONFIG_MPEGVIDEO)               += x86/mpegvideo.o              \
                                          x86/mpegvideodsp.o
OBJS-$(CONFIG_MPEGVIDEOENC)            += x86/mpegvideoenc.o           \
                                          x86/mpegvideoencdsp_init.o
OBJS-$(CONFIG_PIXBLOCKDSP)             += x86/pixblockdsp_init.o
OBJS-$(CONFIG_QPELDSP)                 += x86/qpeldsp_init.o
OBJS-$(CONFIG_RV34DSP)                 += x86/rv34dsp_init.o
OBJS-$(CONFIG_VC1DSP)                  += x86/vc1dsp_init.o
OBJS-$(CONFIG_VIDEODSP)                += x86/videodsp_init.o
OBJS-$(CONFIG_VP3DSP)                  += x86/vp3dsp_init.o
OBJS-$(CONFIG_VP8DSP)                  += x86/vp8dsp_init.o
OBJS-$(CONFIG_XMM_CLOBBER_TEST)        += x86/w64xmmtest.o

# decoders/encoders
OBJS-$(CONFIG_AAC_DECODER)             += x86/aacpsdsp_init.o          \
                                          x86/sbrdsp_init.o
OBJS-$(CONFIG_AAC_ENCODER)             += x86/aacencdsp_init.o
OBJS-$(CONFIG_ADPCM_G722_DECODER)      += x86/g722dsp_init.o
OBJS-$(CONFIG_ADPCM_G722_ENCODER)      += x86/g722dsp_init.o
OBJS-$(CONFIG_ALAC_DECODER)            += x86/alacdsp_init.o
OBJS-$(CONFIG_APNG_DECODER)            += x86/pngdsp_init.o
OBJS-$(CONFIG_CAVS_DECODER)            += x86/cavsdsp.o
OBJS-$(CONFIG_DCA_DECODER)             += x86/dcadsp_init.o x86/synth_filter_init.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += x86/dnxhdenc_init.o
OBJS-$(CONFIG_HEVC_DECODER)            += x86/hevcdsp_init.o
OBJS-$(CONFIG_JPEG2000_DECODER)        += x86/jpeg2000dsp_init.o
OBJS-$(CONFIG_MLP_DECODER)             += x86/mlpdsp_init.o
OBJS-$(CONFIG_MPEG4_DECODER)           += x86/xvididct_init.o
OBJS-$(CONFIG_PNG_DECODER)             += x86/pngdsp_init.o
OBJS-$(CONFIG_PRORES_DECODER)          += x86/proresdsp_init.o
OBJS-$(CONFIG_PRORES_LGPL_DECODER)     += x86/proresdsp_init.o
OBJS-$(CONFIG_RV40_DECODER)            += x86/rv40dsp_init.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += x86/svq1enc_init.o
OBJS-$(CONFIG_TAK_DECODER)             += x86/takdsp_init.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += x86/mlpdsp_init.o
OBJS-$(CONFIG_TTA_DECODER)             += x86/ttadsp_init.o
OBJS-$(CONFIG_TTA_ENCODER)             += x86/ttaencdsp_init.o
OBJS-$(CONFIG_UTVIDEO_DECODER)         += x86/utvideodsp_init.o
OBJS-$(CONFIG_V210_DECODER)            += x86/v210-init.o
OBJS-$(CONFIG_V210_ENCODER)            += x86/v210enc_init.o
OBJS-$(CONFIG_VORBIS_DECODER)          += x86/vorbisdsp_init.o
OBJS-$(CONFIG_VP3_DECODER)             += x86/hpeldsp_vp3_init.o
OBJS-$(CONFIG_VP6_DECODER)             += x86/vp6dsp_init.o
OBJS-$(CONFIG_VP9_DECODER)             += x86/vp9dsp_init.o            \
                                          x86/vp9dsp_init_10bpp.o      \
                                          x86/vp9dsp_init_12bpp.o      \
                                          x86/vp9dsp_init_16bpp.o
OBJS-$(CONFIG_WEBP_DECODER)            += x86/vp8dsp_init.o


# GCC inline assembly optimizations
# subsystems
MMX-OBJS-$(CONFIG_FDCTDSP)             += x86/fdct.o
MMX-OBJS-$(CONFIG_VC1DSP)              += x86/vc1dsp_mmx.o

# decoders/encoders
MMX-OBJS-$(CONFIG_SNOW_DECODER)        += x86/snowdsp.o
MMX-OBJS-$(CONFIG_SNOW_ENCODER)        += x86/snowdsp.o

# subsystems
X86ASM-OBJS-$(CONFIG_AC3DSP)           += x86/ac3dsp.o                  \
                                          x86/ac3dsp_downmix.o
X86ASM-OBJS-$(CONFIG_AUDIODSP)         += x86/audiodsp.o
X86ASM-OBJS-$(CONFIG_BLOCKDSP)         += x86/blockdsp.o
X86ASM-OBJS-$(CONFIG_BSWAPDSP)         += x86/bswapdsp.o
X86ASM-OBJS-$(CONFIG_DCT)              += x86/dct32.o
X86ASM-OBJS-$(CONFIG_FFT)              += x86/fft.o
X86ASM-OBJS-$(CONFIG_FMTCONVERT)       += x86/fmtconvert.o
X86ASM-OBJS-$(CONFIG_H263DSP)          += x86/h263_loopfilter.o
X86ASM-OBJS-$(CONFIG_H264CHROMA)       += x86/h264_chromamc.o           \
                                          x86/h264_chromamc_10bit.o
X86ASM-OBJS-$(CONFIG_H264DSP)          += x86/h264_deblock.o            \
                                          x86/h264_deblock_10bit.o      \
                                          x86/h264_idct.o               \
                                          x86/h264_idct_10bit.o         \
                                          x86/h264_weight.o             \
                                          x86/h264_weight_10bit.o
X86ASM-OBJS-$(CONFIG_H264PRED)         += x86/h264_intrapred.o          \
                                          x86/h264_intrapred_10bit.o
X86ASM-OBJS-$(CONFIG_H264QPEL)         += x86/h264_qpel_8bit.o          \
                                          x86/h264_qpel_10bit.o         \
                                          x86/fpel.o                    \
                                          x86/qpel.o
X86ASM-OBJS-$(CONFIG_HPELDSP)          += x86/fpel.o                    \
                                          x86/hpeldsp.o
X86ASM-OBJS-$(CONFIG_HUFFYUVDSP)       += x86/huffyuvdsp.o
X86ASM-OBJS-$(CONFIG_HUFFYUVENCDSP)    += x86/huffyuvencdsp.o
X86ASM-OBJS-$(CONFIG_IDCTDSP)          += x86/idctdsp.o
X86ASM-OBJS-$(CONFIG_LLAUDDSP)         += x86/lossless_audiodsp.o
X86ASM-OBJS-$(CONFIG_LLVIDDSP)         += x86/lossless_videodsp.o
X86ASM-OBJS-$(CONFIG_LLVIDENCDSP)      += x86/lossless_videoencdsp.o
X86ASM-OBJS-$(CONFIG_MDCT15)           += x86/mdct15.o
X86ASM-OBJS-$(CONFIG_ME_CMP)           += x86/me_cmp.o
X86ASM-OBJS-$(CONFIG_MPEGAUDIODSP)     += x86/imdct36.o
X86ASM-OBJS-$(CONFIG_MPEGVIDEOENC)     += x86/mpegvideoencdsp.o
X86ASM-OBJS-$(CONFIG_PIXBLOCKDSP)      += x86/pixblockdsp.o
X86ASM-OBJS-$(CONFIG_QPELDSP)          += x86/qpeldsp.o                 \
                                          x86/fpel.o                    \
                                          x86/qpel.o
X86ASM-OBJS-$(CONFIG_RV34DSP)          += x86/rv34dsp.o
X86ASM-OBJS-$(CONFIG_VC1DSP)           += x86/vc1dsp_loopfilter.o       \
                                          x86/vc1dsp_mc.o
X86ASM-OBJS-$(CONFIG_IDCTDSP)          += x86/simple_idct10.o           \
                                          x86/simple_idct.o
X86ASM-OBJS-$(CONFIG_VIDEODSP)         += x86/videodsp.o
X86ASM-OBJS-$(CONFIG_VP3DSP)           += x86/vp3dsp.o
X86ASM-OBJS-$(CONFIG_VP8DSP)           += x86/vp8dsp.o                  \
                                          x86/vp8dsp_loopfilter.o

# decoders/encoders
X86ASM-OBJS-$(CONFIG_AAC_DECODER)      += x86/aacpsdsp.o                \
                                          x86/sbrdsp.o
X86ASM-OBJS-$(CONFIG_AAC_ENCODER)      += x86/aacencdsp.o
X86ASM-OBJS-$(CONFIG_ADPCM_G722_DECODER) += x86/g722dsp.o
X86ASM-OBJS-$(CONFIG_ADPCM_G722_ENCODER) += x86/g722dsp.o
X86ASM-OBJS-$(CONFIG_ALAC_DECODER)     += x86/alacdsp.o
X86ASM-OBJS-$(CONFIG_APNG_DECODER)     += x86/pngdsp.o
X86ASM-OBJS-$(CONFIG_CAVS_DECODER)     += x86/cavsidct.o
X86ASM-OBJS-$(CONFIG_DCA_DECODER)      += x86/dcadsp.o x86/synth_filter.o
X86ASM-OBJS-$(CONFIG_DIRAC_DECODER)    += x86/diracdsp.o                \
                                          x86/dirac_dwt.o
X86ASM-OBJS-$(CONFIG_DNXHD_ENCODER)    += x86/dnxhdenc.o
X86ASM-OBJS-$(CONFIG_FLAC_DECODER)     += x86/flacdsp.o
ifdef CONFIG_GPL
X86ASM-OBJS-$(CONFIG_FLAC_ENCODER)     += x86/flac_dsp_gpl.o
endif
X86ASM-OBJS-$(CONFIG_HEVC_DECODER)     += x86/hevc_add_res.o            \
                                          x86/hevc_deblock.o            \
                                          x86/hevc_idct.o               \
                                          x86/hevc_mc.o                 \
                                          x86/hevc_sao.o                \
                                          x86/hevc_sao_10bit.o
X86ASM-OBJS-$(CONFIG_JPEG2000_DECODER) += x86/jpeg2000dsp.o
X86ASM-OBJS-$(CONFIG_MLP_DECODER)      += x86/mlpdsp.o
X86ASM-OBJS-$(CONFIG_MPEG4_DECODER)    += x86/xvididct.o
X86ASM-OBJS-$(CONFIG_PNG_DECODER)      += x86/pngdsp.o
X86ASM-OBJS-$(CONFIG_PRORES_DECODER)   += x86/proresdsp.o
X86ASM-OBJS-$(CONFIG_PRORES_LGPL_DECODER) += x86/proresdsp.o
X86ASM-OBJS-$(CONFIG_RV40_DECODER)     += x86/rv40dsp.o
X86ASM-OBJS-$(CONFIG_SVQ1_ENCODER)     += x86/svq1enc.o
X86ASM-OBJS-$(CONFIG_TAK_DECODER)      += x86/takdsp.o
X86ASM-OBJS-$(CONFIG_TRUEHD_DECODER)   += x86/mlpdsp.o
X86ASM-OBJS-$(CONFIG_TTA_DECODER)      += x86/ttadsp.o
X86ASM-OBJS-$(CONFIG_TTA_ENCODER)      += x86/ttaencdsp.o
X86ASM-OBJS-$(CONFIG_UTVIDEO_DECODER)  += x86/utvideodsp.o
X86ASM-OBJS-$(CONFIG_V210_ENCODER)     += x86/v210enc.o
X86ASM-OBJS-$(CONFIG_V210_DECODER)     += x86/v210.o
X86ASM-OBJS-$(CONFIG_VORBIS_DECODER)   += x86/vorbisdsp.o
X86ASM-OBJS-$(CONFIG_VP3_DECODER)      += x86/hpeldsp_vp3.o
X86ASM-OBJS-$(CONFIG_VP6_DECODER)      += x86/vp6dsp.o
X86ASM-OBJS-$(CONFIG_VP9_DECODER)      += x86/vp9intrapred.o            \
                                          x86/vp9intrapred_16bpp.o      \
                                          x86/vp9itxfm.o                \
                                          x86/vp9itxfm_16bpp.o          \
                                          x86/vp9lpf.o                  \
                                          x86/vp9lpf_16bpp.o            \
                                          x86/vp9mc.o                   \
                                          x86/vp9mc_16bpp.o
X86ASM-OBJS-$(CONFIG_WEBP_DECODER)     += x86/vp8dsp.o
