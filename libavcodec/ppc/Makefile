OBJS                                   += ppc/dsputil_ppc.o             \

ALTIVEC-OBJS-$(CONFIG_H264DSP)         += ppc/h264_altivec.o
ALTIVEC-OBJS-$(CONFIG_VC1_DECODER)     += ppc/vc1dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP3_DECODER)     += ppc/vp3dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP5_DECODER)     += ppc/vp3dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP6_DECODER)     += ppc/vp3dsp_altivec.o

OBJS-$(HAVE_ALTIVEC)                   += ppc/check_altivec.o           \
                                          ppc/dsputil_altivec.o         \
                                          ppc/fdct_altivec.o            \
                                          ppc/fft_altivec.o             \
                                          ppc/float_altivec.o           \
                                          ppc/gmc_altivec.o             \
                                          ppc/idct_altivec.o            \
                                          ppc/int_altivec.o             \
                                          ppc/mpegvideo_altivec.o       \
                                          $(ALTIVEC-OBJS-yes)
