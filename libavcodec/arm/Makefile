OBJS-$(CONFIG_DCA_DECODER)             += arm/dcadsp_init_arm.o         \

OBJS-$(CONFIG_H264DSP)                 += arm/h264dsp_init_arm.o        \
                                          arm/h264pred_init_arm.o       \

OBJS                                   += arm/dsputil_init_arm.o        \
                                          arm/dsputil_arm.o             \
                                          arm/fft_init_arm.o            \
                                          arm/jrevdct_arm.o             \
                                          arm/mpegvideo_arm.o           \
                                          arm/simple_idct_arm.o         \

OBJS-$(HAVE_ARMV5TE)                   += arm/dsputil_init_armv5te.o    \
                                          arm/mpegvideo_armv5te.o       \
                                          arm/mpegvideo_armv5te_s.o     \
                                          arm/simple_idct_armv5te.o     \

OBJS-$(HAVE_ARMV6)                     += arm/dsputil_init_armv6.o      \
                                          arm/dsputil_armv6.o           \
                                          arm/simple_idct_armv6.o       \

OBJS-$(HAVE_ARMVFP)                    += arm/dsputil_vfp.o             \
                                          arm/dsputil_init_vfp.o        \

OBJS-$(HAVE_IWMMXT)                    += arm/dsputil_iwmmxt.o          \
                                          arm/mpegvideo_iwmmxt.o        \

NEON-OBJS-$(CONFIG_FFT)                += arm/fft_neon.o                \

NEON-OBJS-$(CONFIG_MDCT)               += arm/mdct_neon.o               \

NEON-OBJS-$(CONFIG_RDFT)               += arm/rdft_neon.o               \

NEON-OBJS-$(CONFIG_H264DSP)            += arm/h264dsp_neon.o            \
                                          arm/h264idct_neon.o           \
                                          arm/h264pred_neon.o           \

NEON-OBJS-$(CONFIG_DCA_DECODER)        += arm/dcadsp_neon.o             \
                                          arm/synth_filter_neon.o       \

NEON-OBJS-$(CONFIG_VP3_DECODER)        += arm/vp3dsp_neon.o

OBJS-$(HAVE_NEON)                      += arm/dsputil_init_neon.o       \
                                          arm/dsputil_neon.o            \
                                          arm/int_neon.o                \
                                          arm/simple_idct_neon.o        \
                                          $(NEON-OBJS-yes)
