# subsystems
OBJS-$(CONFIG_FFT)                      += aarch64/fft_init_aarch64.o
OBJS-$(CONFIG_FMTCONVERT)               += aarch64/fmtconvert_init.o
OBJS-$(CONFIG_H264CHROMA)               += aarch64/h264chroma_init_aarch64.o
OBJS-$(CONFIG_H264DSP)                  += aarch64/h264dsp_init_aarch64.o
OBJS-$(CONFIG_H264PRED)                 += aarch64/h264pred_init.o
OBJS-$(CONFIG_H264QPEL)                 += aarch64/h264qpel_init_aarch64.o
OBJS-$(CONFIG_HPELDSP)                  += aarch64/hpeldsp_init_aarch64.o
OBJS-$(CONFIG_IDCTDSP)                  += aarch64/idctdsp_init_aarch64.o
OBJS-$(CONFIG_ME_CMP)                   += aarch64/me_cmp_init_aarch64.o
OBJS-$(CONFIG_MPEGAUDIODSP)             += aarch64/mpegaudiodsp_init.o
OBJS-$(CONFIG_NEON_CLOBBER_TEST)        += aarch64/neontest.o
OBJS-$(CONFIG_PIXBLOCKDSP)              += aarch64/pixblockdsp_init_aarch64.o
OBJS-$(CONFIG_VIDEODSP)                 += aarch64/videodsp_init.o
OBJS-$(CONFIG_VP8DSP)                   += aarch64/vp8dsp_init_aarch64.o

# decoders/encoders
OBJS-$(CONFIG_AAC_DECODER)              += aarch64/aacpsdsp_init_aarch64.o \
                                           aarch64/sbrdsp_init_aarch64.o
OBJS-$(CONFIG_DCA_DECODER)              += aarch64/synth_filter_init.o
OBJS-$(CONFIG_OPUS_DECODER)             += aarch64/opusdsp_init.o
OBJS-$(CONFIG_RV40_DECODER)             += aarch64/rv40dsp_init_aarch64.o
OBJS-$(CONFIG_VC1DSP)                   += aarch64/vc1dsp_init_aarch64.o
OBJS-$(CONFIG_VORBIS_DECODER)           += aarch64/vorbisdsp_init.o
OBJS-$(CONFIG_VP9_DECODER)              += aarch64/vp9dsp_init_10bpp_aarch64.o \
                                           aarch64/vp9dsp_init_12bpp_aarch64.o \
                                           aarch64/vp9mc_aarch64.o             \
                                           aarch64/vp9dsp_init_aarch64.o

# ARMv8 optimizations

# subsystems
ARMV8-OBJS-$(CONFIG_VIDEODSP)           += aarch64/videodsp.o

# NEON optimizations

# subsystems
NEON-OBJS-$(CONFIG_AAC_DECODER)         += aarch64/sbrdsp_neon.o
NEON-OBJS-$(CONFIG_FFT)                 += aarch64/fft_neon.o
NEON-OBJS-$(CONFIG_FMTCONVERT)          += aarch64/fmtconvert_neon.o
NEON-OBJS-$(CONFIG_H264CHROMA)          += aarch64/h264cmc_neon.o
NEON-OBJS-$(CONFIG_H264DSP)             += aarch64/h264dsp_neon.o              \
                                           aarch64/h264idct_neon.o
NEON-OBJS-$(CONFIG_H264PRED)            += aarch64/h264pred_neon.o
NEON-OBJS-$(CONFIG_H264QPEL)            += aarch64/h264qpel_neon.o             \
                                           aarch64/hpeldsp_neon.o
NEON-OBJS-$(CONFIG_HPELDSP)             += aarch64/hpeldsp_neon.o
NEON-OBJS-$(CONFIG_IDCTDSP)             += aarch64/idctdsp_neon.o              \
                                           aarch64/simple_idct_neon.o
NEON-OBJS-$(CONFIG_MDCT)                += aarch64/mdct_neon.o
NEON-OBJS-$(CONFIG_ME_CMP)              += aarch64/me_cmp_neon.o
NEON-OBJS-$(CONFIG_MPEGAUDIODSP)        += aarch64/mpegaudiodsp_neon.o
NEON-OBJS-$(CONFIG_PIXBLOCKDSP)         += aarch64/pixblockdsp_neon.o
NEON-OBJS-$(CONFIG_VC1DSP)              += aarch64/vc1dsp_neon.o
NEON-OBJS-$(CONFIG_VP8DSP)              += aarch64/vp8dsp_neon.o

# decoders/encoders
NEON-OBJS-$(CONFIG_AAC_DECODER)         += aarch64/aacpsdsp_neon.o
NEON-OBJS-$(CONFIG_DCA_DECODER)         += aarch64/synth_filter_neon.o
NEON-OBJS-$(CONFIG_OPUS_DECODER)        += aarch64/opusdsp_neon.o
NEON-OBJS-$(CONFIG_VORBIS_DECODER)      += aarch64/vorbisdsp_neon.o
NEON-OBJS-$(CONFIG_VP9_DECODER)         += aarch64/vp9itxfm_16bpp_neon.o       \
                                           aarch64/vp9itxfm_neon.o             \
                                           aarch64/vp9lpf_16bpp_neon.o         \
                                           aarch64/vp9lpf_neon.o               \
                                           aarch64/vp9mc_16bpp_neon.o          \
                                           aarch64/vp9mc_neon.o
NEON-OBJS-$(CONFIG_HEVC_DECODER)        += aarch64/hevcdsp_idct_neon.o         \
                                           aarch64/hevcdsp_init_aarch64.o      \
                                           aarch64/hevcdsp_sao_neon.o
