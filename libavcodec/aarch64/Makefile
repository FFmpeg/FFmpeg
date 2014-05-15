OBJS-$(CONFIG_FFT)                      += aarch64/fft_init_aarch64.o
OBJS-$(CONFIG_H264CHROMA)               += aarch64/h264chroma_init_aarch64.o
OBJS-$(CONFIG_H264DSP)                  += aarch64/h264dsp_init_aarch64.o
OBJS-$(CONFIG_H264QPEL)                 += aarch64/h264qpel_init_aarch64.o
OBJS-$(CONFIG_HPELDSP)                  += aarch64/hpeldsp_init_aarch64.o
OBJS-$(CONFIG_MPEGAUDIODSP)             += aarch64/mpegaudiodsp_init.o
OBJS-$(CONFIG_NEON_CLOBBER_TEST)        += aarch64/neontest.o
OBJS-$(CONFIG_VIDEODSP)                 += aarch64/videodsp_init.o

OBJS-$(CONFIG_OPUS_DECODER)             += aarch64/opus_imdct_init.o
OBJS-$(CONFIG_RV40_DECODER)             += aarch64/rv40dsp_init_aarch64.o
OBJS-$(CONFIG_VC1_DECODER)              += aarch64/vc1dsp_init_aarch64.o
OBJS-$(CONFIG_VORBIS_DECODER)           += aarch64/vorbisdsp_init.o

ARMV8-OBJS-$(CONFIG_VIDEODSP)           += aarch64/videodsp.o

NEON-OBJS-$(CONFIG_FFT)                 += aarch64/fft_neon.o
NEON-OBJS-$(CONFIG_H264CHROMA)          += aarch64/h264cmc_neon.o
NEON-OBJS-$(CONFIG_H264DSP)             += aarch64/h264dsp_neon.o              \
                                           aarch64/h264idct_neon.o
NEON-OBJS-$(CONFIG_H264QPEL)            += aarch64/h264qpel_neon.o             \
                                           aarch64/hpeldsp_neon.o
NEON-OBJS-$(CONFIG_HPELDSP)             += aarch64/hpeldsp_neon.o
NEON-OBJS-$(CONFIG_MPEGAUDIODSP)        += aarch64/mpegaudiodsp_neon.o
NEON-OBJS-$(CONFIG_MDCT)                += aarch64/mdct_neon.o

NEON-OBJS-$(CONFIG_OPUS_DECODER)        += aarch64/opus_imdct_neon.o
NEON-OBJS-$(CONFIG_VORBIS_DECODER)      += aarch64/vorbisdsp_neon.o
