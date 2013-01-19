OBJS                                   += ppc/dsputil_ppc.o             \
                                          ppc/videodsp_ppc.o            \

OBJS-$(CONFIG_H264CHROMA)              += ppc/h264chroma_init.o
OBJS-$(CONFIG_H264QPEL)                += ppc/h264_qpel.o
OBJS-$(CONFIG_VORBIS_DECODER)          += ppc/vorbisdsp_altivec.o
OBJS-$(CONFIG_VP3DSP)                  += ppc/vp3dsp_altivec.o

FFT-OBJS-$(HAVE_GNU_AS)                += ppc/fft_altivec_s.o
ALTIVEC-OBJS-$(CONFIG_FFT)             += ppc/fft_altivec.o             \
                                          $(FFT-OBJS-yes)
ALTIVEC-OBJS-$(CONFIG_H264DSP)         += ppc/h264_altivec.o
ALTIVEC-OBJS-$(CONFIG_MPEGAUDIODSP)    += ppc/mpegaudiodec_altivec.o
ALTIVEC-OBJS-$(CONFIG_MPEGVIDEO)       += ppc/mpegvideo_altivec.o
ALTIVEC-OBJS-$(CONFIG_VC1_DECODER)     += ppc/vc1dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_VP8_DECODER)     += ppc/vp8dsp_altivec.o

ALTIVEC-OBJS                           += ppc/dsputil_altivec.o         \
                                          ppc/fdct_altivec.o            \
                                          ppc/fmtconvert_altivec.o      \
                                          ppc/gmc_altivec.o             \
                                          ppc/idct_altivec.o            \
                                          ppc/int_altivec.o             \
