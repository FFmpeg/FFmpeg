OBJS-$(CONFIG_H264CHROMA)               += aarch64/h264chroma_init_aarch64.o
OBJS-$(CONFIG_H264DSP)                  += aarch64/h264dsp_init_aarch64.o
OBJS-$(CONFIG_H264QPEL)                 += aarch64/h264qpel_init_aarch64.o
OBJS-$(CONFIG_HPELDSP)                  += aarch64/hpeldsp_init_aarch64.o
OBJS-$(CONFIG_NEON_CLOBBER_TEST)        += aarch64/neontest.o
OBJS-$(CONFIG_RV40_DECODER)             += aarch64/rv40dsp_init_aarch64.o
OBJS-$(CONFIG_VC1_DECODER)              += aarch64/vc1dsp_init_aarch64.o

NEON-OBJS-$(CONFIG_H264CHROMA)          += aarch64/h264cmc_neon.o
NEON-OBJS-$(CONFIG_H264DSP)             += aarch64/h264dsp_neon.o              \
                                           aarch64/h264idct_neon.o
NEON-OBJS-$(CONFIG_H264QPEL)            += aarch64/h264qpel_neon.o             \
                                           aarch64/hpeldsp_neon.o
NEON-OBJS-$(CONFIG_HPELDSP)             += aarch64/hpeldsp_neon.o
