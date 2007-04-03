#
# libavcodec Makefile
# (c) 2000-2005 Fabrice Bellard
#
include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libswscale $(AMR_CFLAGS)

OBJS= bitstream.o \
      utils.o \
      allcodecs.o \
      mpegvideo.o \
      jrevdct.o \
      jfdctfst.o \
      jfdctint.o\
      mjpeg.o \
      resample.o \
      resample2.o \
      dsputil.o \
      motion_est.o \
      imgconvert.o \
      mpeg12.o \
      mpegaudiodec.o \
      simple_idct.o \
      ratecontrol.o \
      eval.o \
      error_resilience.o \
      fft.o \
      mdct.o \
      raw.o \
      golomb.o \
      cabac.o\
      faandct.o \
      parser.o \
      vp3dsp.o \
      h264idct.o \
      rangecoder.o \
      pnm.o \
      h263.o \
      msmpeg4.o \
      h263dec.o \
      opt.o \
      bitstream_filter.o \
      audioconvert.o \


HEADERS = avcodec.h opt.h

OBJS-$(CONFIG_AASC_DECODER)            += aasc.o
OBJS-$(CONFIG_AC3_ENCODER)             += ac3enc.o ac3.o
OBJS-$(CONFIG_ALAC_DECODER)            += alac.o
OBJS-$(CONFIG_ASV1_DECODER)            += asv1.o
OBJS-$(CONFIG_ASV1_ENCODER)            += asv1.o
OBJS-$(CONFIG_ASV2_DECODER)            += asv1.o
OBJS-$(CONFIG_ASV2_ENCODER)            += asv1.o
OBJS-$(CONFIG_AVS_DECODER)             += avs.o
OBJS-$(CONFIG_BMP_DECODER)             += bmp.o
OBJS-$(CONFIG_BMP_ENCODER)             += bmpenc.o
OBJS-$(CONFIG_CAVS_DECODER)            += cavs.o cavsdsp.o
OBJS-$(CONFIG_CINEPAK_DECODER)         += cinepak.o
OBJS-$(CONFIG_CLJR_DECODER)            += cljr.o
OBJS-$(CONFIG_CLJR_ENCODER)            += cljr.o
OBJS-$(CONFIG_COOK_DECODER)            += cook.o
OBJS-$(CONFIG_CSCD_DECODER)            += cscd.o
OBJS-$(CONFIG_CYUV_DECODER)            += cyuv.o
OBJS-$(CONFIG_DCA_DECODER)             += dca.o
OBJS-$(CONFIG_DNXHD_DECODER)           += dnxhddec.o
OBJS-$(CONFIG_DSICINVIDEO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DSICINAUDIO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DVBSUB_DECODER)          += dvbsubdec.o
OBJS-$(CONFIG_DVBSUB_ENCODER)          += dvbsub.o
OBJS-$(CONFIG_DVDSUB_DECODER)          += dvdsubdec.o
OBJS-$(CONFIG_DVDSUB_ENCODER)          += dvdsubenc.o
OBJS-$(CONFIG_DVVIDEO_DECODER)         += dv.o
OBJS-$(CONFIG_DVVIDEO_ENCODER)         += dv.o
OBJS-$(CONFIG_DXA_DECODER)             += dxa.o
OBJS-$(CONFIG_EIGHTBPS_DECODER)        += 8bps.o
OBJS-$(CONFIG_FFV1_DECODER)            += ffv1.o
OBJS-$(CONFIG_FFV1_ENCODER)            += ffv1.o
OBJS-$(CONFIG_FFVHUFF_DECODER)         += huffyuv.o
OBJS-$(CONFIG_FFVHUFF_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_FLAC_DECODER)            += flac.o
OBJS-$(CONFIG_FLAC_ENCODER)            += flacenc.o
OBJS-$(CONFIG_FLASHSV_DECODER)         += flashsv.o
OBJS-$(CONFIG_FLASHSV_ENCODER)         += flashsvenc.o
OBJS-$(CONFIG_FLIC_DECODER)            += flicvideo.o
OBJS-$(CONFIG_FOURXM_DECODER)          += 4xm.o
OBJS-$(CONFIG_FRAPS_DECODER)           += fraps.o
OBJS-$(CONFIG_GIF_DECODER)             += gifdec.o lzw.o
OBJS-$(CONFIG_GIF_ENCODER)             += gif.o
OBJS-$(CONFIG_H261_DECODER)            += h261.o
OBJS-$(CONFIG_H261_ENCODER)            += h261.o
OBJS-$(CONFIG_H264_DECODER)            += h264.o
OBJS-$(CONFIG_H264_ENCODER)            += h264enc.o h264dsp.o
OBJS-$(CONFIG_HUFFYUV_DECODER)         += huffyuv.o
OBJS-$(CONFIG_HUFFYUV_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_IDCIN_DECODER)           += idcinvideo.o
OBJS-$(CONFIG_IMC_DECODER)             += imc.o
OBJS-$(CONFIG_INDEO2_DECODER)          += indeo2.o
OBJS-$(CONFIG_INDEO3_DECODER)          += indeo3.o
OBJS-$(CONFIG_INTERPLAY_VIDEO_DECODER) += interplayvideo.o
OBJS-$(CONFIG_INTERPLAY_DPCM_DECODER)  += dpcm.o
OBJS-$(CONFIG_KMVC_DECODER)            += kmvc.o
OBJS-$(CONFIG_LOCO_DECODER)            += loco.o
OBJS-$(CONFIG_MACE3_DECODER)           += mace.o
OBJS-$(CONFIG_MACE6_DECODER)           += mace.o
OBJS-$(CONFIG_MMVIDEO_DECODER)         += mmvideo.o
OBJS-$(CONFIG_MP2_ENCODER)             += mpegaudio.o
OBJS-$(CONFIG_MPC7_DECODER)            += mpc.o
OBJS-$(CONFIG_MSRLE_DECODER)           += msrle.o
OBJS-$(CONFIG_MSVIDEO1_DECODER)        += msvideo1.o
OBJS-$(CONFIG_MSZH_DECODER)            += lcl.o
OBJS-$(CONFIG_NUV_DECODER)             += nuv.o rtjpeg.o
OBJS-$(CONFIG_PNG_DECODER)             += png.o
OBJS-$(CONFIG_PNG_ENCODER)             += png.o
OBJS-$(CONFIG_QDM2_DECODER)            += qdm2.o
OBJS-$(CONFIG_QDRAW_DECODER)           += qdrw.o
OBJS-$(CONFIG_QPEG_DECODER)            += qpeg.o
OBJS-$(CONFIG_QTRLE_DECODER)           += qtrle.o
OBJS-$(CONFIG_RA_144_DECODER)          += ra144.o
OBJS-$(CONFIG_RA_288_DECODER)          += ra288.o
OBJS-$(CONFIG_ROQ_DECODER)             += roqvideo.o
OBJS-$(CONFIG_ROQ_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_RPZA_DECODER)            += rpza.o
OBJS-$(CONFIG_RV10_DECODER)            += rv10.o
OBJS-$(CONFIG_RV10_ENCODER)            += rv10.o
OBJS-$(CONFIG_RV20_DECODER)            += rv10.o
OBJS-$(CONFIG_RV20_ENCODER)            += rv10.o
OBJS-$(CONFIG_SHORTEN_DECODER)         += shorten.o
OBJS-$(CONFIG_SMACKAUD_DECODER)        += smacker.o
OBJS-$(CONFIG_SMACKER_DECODER)         += smacker.o
OBJS-$(CONFIG_SMC_DECODER)             += smc.o
OBJS-$(CONFIG_SNOW_DECODER)            += snow.o
OBJS-$(CONFIG_SNOW_ENCODER)            += snow.o
OBJS-$(CONFIG_SOL_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_SONIC_DECODER)           += sonic.o
OBJS-$(CONFIG_SONIC_ENCODER)           += sonic.o
OBJS-$(CONFIG_SONIC_LS_DECODER)        += sonic.o
OBJS-$(CONFIG_SVQ1_DECODER)            += svq1.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += svq1.o
OBJS-$(CONFIG_SVQ3_DECODER)            += h264.o
OBJS-$(CONFIG_TARGA_DECODER)           += targa.o
OBJS-$(CONFIG_TARGA_ENCODER)           += targaenc.o rle.o
OBJS-$(CONFIG_THEORA_DECODER)          += vp3.o xiph.o
OBJS-$(CONFIG_TIERTEXSEQVIDEO_DECODER) += tiertexseqv.o
OBJS-$(CONFIG_TIFF_DECODER)            += tiff.o lzw.o
OBJS-$(CONFIG_TIFF_ENCODER)            += tiffenc.o rle.o
OBJS-$(CONFIG_TRUEMOTION1_DECODER)     += truemotion1.o
OBJS-$(CONFIG_TRUEMOTION2_DECODER)     += truemotion2.o
OBJS-$(CONFIG_TRUESPEECH_DECODER)      += truespeech.o
OBJS-$(CONFIG_TSCC_DECODER)            += tscc.o
OBJS-$(CONFIG_TTA_DECODER)             += tta.o
OBJS-$(CONFIG_ULTI_DECODER)            += ulti.o
OBJS-$(CONFIG_VC1_DECODER)             += vc1.o vc1dsp.o
OBJS-$(CONFIG_VCR1_DECODER)            += vcr1.o
OBJS-$(CONFIG_VCR1_ENCODER)            += vcr1.o
OBJS-$(CONFIG_VMDAUDIO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMDVIDEO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMNC_DECODER)            += vmnc.o
OBJS-$(CONFIG_VORBIS_DECODER)          += vorbis.o vorbis_data.o xiph.o
OBJS-$(CONFIG_VORBIS_ENCODER)          += vorbis_enc.o vorbis.o vorbis_data.o
OBJS-$(CONFIG_VP3_DECODER)             += vp3.o
OBJS-$(CONFIG_VP5_DECODER)             += vp5.o vp56.o vp56data.o
OBJS-$(CONFIG_VP6_DECODER)             += vp6.o vp56.o vp56data.o
OBJS-$(CONFIG_VQA_DECODER)             += vqavideo.o
OBJS-$(CONFIG_WAVPACK_DECODER)         += wavpack.o
OBJS-$(CONFIG_WMAV1_DECODER)           += wmadec.o wma.o
OBJS-$(CONFIG_WMAV2_DECODER)           += wmadec.o wma.o
OBJS-$(CONFIG_WMAV1_ENCODER)           += wmaenc.o wma.o
OBJS-$(CONFIG_WMAV2_ENCODER)           += wmaenc.o wma.o
OBJS-$(CONFIG_WMV3_DECODER)            += vc1.o vc1dsp.o
OBJS-$(CONFIG_WNV1_DECODER)            += wnv1.o
OBJS-$(CONFIG_WS_SND1_DECODER)         += ws-snd1.o
OBJS-$(CONFIG_XAN_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_XAN_WC3_DECODER)         += xan.o
OBJS-$(CONFIG_XAN_WC4_DECODER)         += xan.o
OBJS-$(CONFIG_XL_DECODER)              += xl.o
OBJS-$(CONFIG_ZLIB_DECODER)            += lcl.o
OBJS-$(CONFIG_ZLIB_ENCODER)            += lcl.o
OBJS-$(CONFIG_ZMBV_DECODER)            += zmbv.o
OBJS-$(CONFIG_ZMBV_ENCODER)            += zmbvenc.o

OBJS-$(CONFIG_PCM_S32LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S32LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S32BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S32BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U32LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U32LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U32BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U32BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S24LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S24LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S24BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S24BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U24LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U24LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U24BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U24BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_DECODER)     += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_ENCODER)     += pcm.o
OBJS-$(CONFIG_PCM_S16LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S16LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S16BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_S16BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U16LE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U16LE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_U16BE_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_U16BE_ENCODER)       += pcm.o
OBJS-$(CONFIG_PCM_S8_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S8_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U8_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U8_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_ALAW_DECODER)        += pcm.o
OBJS-$(CONFIG_PCM_ALAW_ENCODER)        += pcm.o
OBJS-$(CONFIG_PCM_MULAW_DECODER)       += pcm.o
OBJS-$(CONFIG_PCM_MULAW_ENCODER)       += pcm.o

OBJS-$(CONFIG_ADPCM_4XM_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_4XM_ENCODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_ADX_DECODER)       += adx.o
OBJS-$(CONFIG_ADPCM_ADX_ENCODER)       += adx.o
OBJS-$(CONFIG_ADPCM_CT_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_CT_ENCODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_ENCODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_G726_DECODER)      += g726.o
OBJS-$(CONFIG_ADPCM_G726_ENCODER)      += g726.o
OBJS-$(CONFIG_ADPCM_IMA_DK3_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_DK3_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_DK4_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_DK4_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_QT_DECODER)    += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_QT_ENCODER)    += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_SMJPEG_DECODER) += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_SMJPEG_ENCODER) += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WS_DECODER)    += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WS_ENCODER)    += adpcm.o
OBJS-$(CONFIG_ADPCM_MS_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_MS_ENCODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_2_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_2_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_3_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_3_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_4_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_4_ENCODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_SWF_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_SWF_ENCODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_XA_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_XA_ENCODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_YAMAHA_DECODER)    += adpcm.o
OBJS-$(CONFIG_ADPCM_YAMAHA_ENCODER)    += adpcm.o

# external codec libraries
OBJS-$(CONFIG_LIBA52)                  += a52dec.o
OBJS-$(CONFIG_LIBDTS)                  += dtsdec.o
OBJS-$(CONFIG_LIBFAAC)                 += faac.o
OBJS-$(CONFIG_LIBFAAD)                 += faad.o
OBJS-$(CONFIG_LIBGSM)                  += libgsm.o
OBJS-$(CONFIG_LIBMP3LAME)              += mp3lameaudio.o
OBJS-$(CONFIG_LIBTHEORA)               += libtheoraenc.o
OBJS-$(CONFIG_LIBVORBIS)               += oggvorbis.o
OBJS-$(CONFIG_X264)                    += x264.o
OBJS-$(CONFIG_XVID)                    += xvidff.o xvid_rc.o

OBJS-$(CONFIG_AMR)                     += amr.o
OBJS-$(CONFIG_AMR_NB)                  += amr_float/sp_dec.o     \
                                          amr_float/sp_enc.o     \
                                          amr_float/interf_dec.o \
                                          amr_float/interf_enc.o

ifeq ($(CONFIG_AMR_NB_FIXED),yes)
EXTRAOBJS += amr/*.o
EXTRADEPS=amrlibs
endif

OBJS-$(CONFIG_AMR_WB)                  += amrwb_float/dec_acelp.o \
                                          amrwb_float/dec_dtx.o   \
                                          amrwb_float/dec_gain.o  \
                                          amrwb_float/dec_if.o    \
                                          amrwb_float/dec_lpc.o   \
                                          amrwb_float/dec_main.o  \
                                          amrwb_float/dec_rom.o   \
                                          amrwb_float/dec_util.o  \
                                          amrwb_float/enc_acelp.o \
                                          amrwb_float/enc_dtx.o   \
                                          amrwb_float/enc_gain.o  \
                                          amrwb_float/enc_if.o    \
                                          amrwb_float/enc_lpc.o   \
                                          amrwb_float/enc_main.o  \
                                          amrwb_float/enc_rom.o   \
                                          amrwb_float/enc_util.o  \
                                          amrwb_float/if_rom.o

OBJS-$(CONFIG_AAC_PARSER)              += parser.o
OBJS-$(CONFIG_AC3_PARSER)              += parser.o ac3.o
OBJS-$(CONFIG_CAVSVIDEO_PARSER)        += cavs.o parser.o
OBJS-$(CONFIG_DVBSUB_PARSER)           += dvbsubdec.o
OBJS-$(CONFIG_DVDSUB_PARSER)           += dvdsubdec.o
OBJS-$(CONFIG_H261_PARSER)             += h261.o
OBJS-$(CONFIG_H263_PARSER)             += h263dec.o
OBJS-$(CONFIG_H264_PARSER)             += h264.o
OBJS-$(CONFIG_MJPEG_PARSER)            += mjpeg.o
OBJS-$(CONFIG_MPEG4VIDEO_PARSER)       += parser.o
OBJS-$(CONFIG_MPEGAUDIO_PARSER)        += parser.o
OBJS-$(CONFIG_MPEGVIDEO_PARSER)        += parser.o
OBJS-$(CONFIG_PNM_PARSER)              += pnm.o

OBJS-$(HAVE_PTHREADS)                  += pthread.o
OBJS-$(HAVE_W32THREADS)                += w32thread.o
OBJS-$(HAVE_OS2THREADS)                += os2thread.o
OBJS-$(HAVE_BEOSTHREADS)               += beosthread.o

OBJS-$(HAVE_XVMC_ACCEL)                += xvmcvideo.o

ifneq ($(CONFIG_SWSCALER),yes)
OBJS += imgresample.o
endif

# processor-specific code
ifeq ($(TARGET_MMX),yes)
OBJS += i386/fdct_mmx.o \
        i386/cputest.o \
        i386/dsputil_mmx.o \
        i386/mpegvideo_mmx.o \
        i386/motion_est_mmx.o \
        i386/simple_idct_mmx.o \
        i386/idct_mmx_xvid.o \
        i386/fft_sse.o \
        i386/vp3dsp_mmx.o \
        i386/vp3dsp_sse2.o \
        i386/fft_3dn.o \
        i386/fft_3dn2.o \

OBJS-$(CONFIG_GPL)                     += i386/idct_mmx.o
OBJS-$(CONFIG_CAVS_DECODER)            += i386/cavsdsp_mmx.o
OBJS-$(CONFIG_SNOW_DECODER)            += i386/snowdsp_mmx.o
endif

ASM_OBJS-$(TARGET_ARCH_ARMV4L)         += armv4l/jrevdct_arm.o     \
                                          armv4l/simple_idct_arm.o \
                                          armv4l/dsputil_arm_s.o   \

OBJS-$(TARGET_ARCH_ARMV4L)             += armv4l/dsputil_arm.o   \
                                          armv4l/mpegvideo_arm.o \

OBJS-$(TARGET_IWMMXT)                  += armv4l/dsputil_iwmmxt.o   \
                                          armv4l/mpegvideo_iwmmxt.o \

ASM_OBJS-$(TARGET_ARMV5TE)             += armv4l/simple_idct_armv5te.o \
                                          armv4l/mpegvideo_armv5te.o \

ASM_OBJS-$(HAVE_ARMV6)                 += armv4l/simple_idct_armv6.o

OBJS-$(TARGET_ARCH_SPARC)              += sparc/dsputil_vis.o \

sparc/dsputil_vis.o: CFLAGS += -mcpu=ultrasparc -mtune=ultrasparc

OBJS-$(HAVE_MLIB)                      += mlib/dsputil_mlib.o \

OBJS-$(TARGET_ARCH_ALPHA)              += alpha/dsputil_alpha.o     \
                                          alpha/mpegvideo_alpha.o   \
                                          alpha/simple_idct_alpha.o \
                                          alpha/motion_est_alpha.o  \

ASM_OBJS-$(TARGET_ARCH_ALPHA)          += alpha/dsputil_alpha_asm.o  \
                                          alpha/motion_est_mvi_asm.o \

OBJS-$(TARGET_ARCH_POWERPC)            += ppc/dsputil_ppc.o   \
                                          ppc/mpegvideo_ppc.o \

OBJS-$(TARGET_MMI)                     += ps2/dsputil_mmi.o   \
                                          ps2/idct_mmi.o      \
                                          ps2/mpegvideo_mmi.o \

OBJS-$(TARGET_ARCH_SH4)                += sh4/idct_sh4.o      \
                                          sh4/dsputil_sh4.o   \
                                          sh4/dsputil_align.o \

OBJS-$(TARGET_ALTIVEC)                 += ppc/dsputil_altivec.o      \
                                          ppc/mpegvideo_altivec.o    \
                                          ppc/idct_altivec.o         \
                                          ppc/fft_altivec.o          \
                                          ppc/gmc_altivec.o          \
                                          ppc/fdct_altivec.o         \
                                          ppc/float_altivec.o        \

ifeq ($(TARGET_ALTIVEC),yes)
OBJS-$(CONFIG_H264_DECODER)            += ppc/h264_altivec.o
OBJS-$(CONFIG_SNOW_DECODER)            += ppc/snow_altivec.o
OBJS-$(CONFIG_VC1_DECODER)             += ppc/vc1dsp_altivec.o
OBJS-$(CONFIG_WMV3_DECODER)            += ppc/vc1dsp_altivec.o
endif

OBJS-$(TARGET_ARCH_BFIN)               += bfin/dsputil_bfin.o \

ASM_OBJS-$(TARGET_ARCH_BFIN)           += bfin/pixels_bfin.o \
                                          bfin/idct_bfin.o   \
                                          bfin/fdct_bfin.o   \

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=avcodec
LIBVERSION=$(LAVCVERSION)
LIBMAJOR=$(LAVCMAJOR)

TESTS= imgresample-test fft-test dct-test
ifeq ($(TARGET_ARCH_X86),yes)
TESTS+= cpuid_test motion-test
endif

include ../common.mak

amrlibs:
	$(MAKE) -C amr spclib fipoplib

tests: apiexample $(TESTS)

clean::
	rm -f \
	   i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
	   alpha/*.o alpha/*~ \
	   ppc/*.o ppc/*~ \
	   ps2/*.o ps2/*~ \
	   sh4/*.o sh4/*~ \
	   sparc/*.o sparc/*~ \
	   amr_float/*.o \
	   apiexample $(TESTS)
	-$(MAKE) -C amr clean
	-$(MAKE) -C amrwb_float -f makefile.gcc clean

apiexample: apiexample.o $(LIB)

cpuid_test: i386/cputest.c
	$(CC) $(CFLAGS) -D__TEST__ -o $@ $<

imgresample-test: imgresample.c $(LIB)
	$(CC) $(CFLAGS) -DTEST -o $@ $^ $(EXTRALIBS)

dct-test: dct-test.o fdctref.o $(LIB)

motion-test: motion_test.o $(LIB)

fft-test: fft-test.o $(LIB)

.PHONY: amrlibs tests
