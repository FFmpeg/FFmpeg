OBJS                                   += ppc/dsputil_ppc.o             \

FFT-OBJS-$(HAVE_GNU_AS)                += ppc/fft_altivec_s.o
ALTIVEC-OBJS-$(CONFIG_FFT)             += ppc/fft_altivec.o             \
                                          $(FFT-OBJS-yes)
ALTIVEC-OBJS-$(CONFIG_H264DSP)         += ppc/h264_altivec.o
ALTIVEC-OBJS-$(CONFIG_MPEGAUDIODSP)    += ppc/mpegaudiodec_altivec.o
ALTIVEC-OBJS-$(CONFIG_VC1_DECODER)     += ppc/vc1dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP3_DECODER)     += ppc/vp3dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP5_DECODER)     += ppc/vp3dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP6_DECODER)     += ppc/vp3dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP8_DECODER)     += ppc/vp8dsp_altivec.o

ALTIVEC-OBJS                           += ppc/dsputil_altivec.o         \
                                          ppc/fdct_altivec.o            \
                                          ppc/float_altivec.o           \
                                          ppc/fmtconvert_altivec.o      \
                                          ppc/gmc_altivec.o             \
                                          ppc/idct_altivec.o            \
                                          ppc/int_altivec.o             \
                                          ppc/mpegvideo_altivec.o       \
