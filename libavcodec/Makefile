include $(SUBDIR)../config.mak

NAME = avcodec
FFLIBS = avutil

HEADERS = avcodec.h                                                     \
          avfft.h                                                       \
          dxva2.h                                                       \
          vaapi.h                                                       \
          vda.h                                                         \
          vdpau.h                                                       \
          version.h                                                     \
          xvmc.h                                                        \

OBJS = allcodecs.o                                                      \
       audioconvert.o                                                   \
       avpacket.o                                                       \
       bitstream.o                                                      \
       bitstream_filter.o                                               \
       dsputil.o                                                        \
       faanidct.o                                                       \
       fmtconvert.o                                                     \
       imgconvert.o                                                     \
       jrevdct.o                                                        \
       options.o                                                        \
       parser.o                                                         \
       raw.o                                                            \
       rawdec.o                                                         \
       resample.o                                                       \
       resample2.o                                                      \
       simple_idct.o                                                    \
       utils.o                                                          \

# parts needed for many different codecs
OBJS-$(CONFIG_AANDCT)                  += aandcttab.o
OBJS-$(CONFIG_AC3DSP)                  += ac3dsp.o
OBJS-$(CONFIG_CRYSTALHD)               += crystalhd.o
OBJS-$(CONFIG_ENCODERS)                += faandct.o jfdctfst.o jfdctint.o
OBJS-$(CONFIG_DCT)                     += dct.o dct32_fixed.o dct32_float.o
OBJS-$(CONFIG_DWT)                     += dwt.o
OBJS-$(CONFIG_DXVA2)                   += dxva2.o
FFT-OBJS-$(CONFIG_HARDCODED_TABLES)    += cos_tables.o cos_fixed_tables.o
OBJS-$(CONFIG_FFT)                     += avfft.o fft_fixed.o fft_float.o \
                                          $(FFT-OBJS-yes)
OBJS-$(CONFIG_GOLOMB)                  += golomb.o
OBJS-$(CONFIG_H264DSP)                 += h264dsp.o h264idct.o
OBJS-$(CONFIG_H264PRED)                += h264pred.o
OBJS-$(CONFIG_HUFFMAN)                 += huffman.o
OBJS-$(CONFIG_LIBXVID)                 += libxvid_rc.o
OBJS-$(CONFIG_LPC)                     += lpc.o
OBJS-$(CONFIG_LSP)                     += lsp.o
OBJS-$(CONFIG_MDCT)                    += mdct_fixed.o mdct_float.o
OBJS-$(CONFIG_MPEGAUDIODSP)            += mpegaudiodsp.o                \
                                          mpegaudiodsp_fixed.o          \
                                          mpegaudiodsp_float.o
RDFT-OBJS-$(CONFIG_HARDCODED_TABLES)   += sin_tables.o
OBJS-$(CONFIG_RDFT)                    += rdft.o $(RDFT-OBJS-yes)
OBJS-$(CONFIG_SINEWIN)                 += sinewin.o
OBJS-$(CONFIG_VAAPI)                   += vaapi.o
OBJS-$(CONFIG_VDA)                     += vda.o
OBJS-$(CONFIG_VDPAU)                   += vdpau.o

# decoders/encoders/hardware accelerators
OBJS-$(CONFIG_A64MULTI_ENCODER)        += a64multienc.o elbg.o
OBJS-$(CONFIG_A64MULTI5_ENCODER)       += a64multienc.o elbg.o
OBJS-$(CONFIG_AAC_DECODER)             += aacdec.o aactab.o aacsbr.o aacps.o \
                                          aacadtsdec.o mpeg4audio.o kbdwin.o \
                                          sbrdsp.o aacpsdsp.o
OBJS-$(CONFIG_AAC_ENCODER)             += aacenc.o aaccoder.o    \
                                          aacpsy.o aactab.o      \
                                          psymodel.o iirfilter.o \
                                          mpeg4audio.o kbdwin.o  \
                                          audio_frame_queue.o
OBJS-$(CONFIG_AASC_DECODER)            += aasc.o msrledec.o
OBJS-$(CONFIG_AC3_DECODER)             += ac3dec.o ac3dec_data.o ac3.o kbdwin.o
OBJS-$(CONFIG_AC3_ENCODER)             += ac3enc_float.o ac3enc.o ac3tab.o \
                                          ac3.o kbdwin.o
OBJS-$(CONFIG_AC3_FIXED_ENCODER)       += ac3enc_fixed.o ac3enc.o ac3tab.o ac3.o
OBJS-$(CONFIG_ALAC_DECODER)            += alac.o
OBJS-$(CONFIG_ALAC_ENCODER)            += alacenc.o
OBJS-$(CONFIG_ALS_DECODER)             += alsdec.o bgmc.o mpeg4audio.o
OBJS-$(CONFIG_AMRNB_DECODER)           += amrnbdec.o celp_filters.o   \
                                          celp_math.o acelp_filters.o \
                                          acelp_vectors.o             \
                                          acelp_pitch_delay.o
OBJS-$(CONFIG_AMRWB_DECODER)           += amrwbdec.o celp_filters.o   \
                                          celp_math.o acelp_filters.o \
                                          acelp_vectors.o             \
                                          acelp_pitch_delay.o lsp.o
OBJS-$(CONFIG_AMV_DECODER)             += sp5xdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_ANM_DECODER)             += anm.o
OBJS-$(CONFIG_ANSI_DECODER)            += ansi.o cga_data.o
OBJS-$(CONFIG_APE_DECODER)             += apedec.o
OBJS-$(CONFIG_ASS_DECODER)             += assdec.o ass.o ass_split.o
OBJS-$(CONFIG_ASS_ENCODER)             += assenc.o ass.o
OBJS-$(CONFIG_ASV1_DECODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV1_ENCODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV2_DECODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV2_ENCODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ATRAC1_DECODER)          += atrac1.o atrac.o
OBJS-$(CONFIG_ATRAC3_DECODER)          += atrac3.o atrac.o
OBJS-$(CONFIG_AURA_DECODER)            += cyuv.o
OBJS-$(CONFIG_AURA2_DECODER)           += aura.o
OBJS-$(CONFIG_AVRP_DECODER)            += r210dec.o
OBJS-$(CONFIG_AVRP_ENCODER)            += r210enc.o
OBJS-$(CONFIG_AVS_DECODER)             += avs.o
OBJS-$(CONFIG_AVUI_DECODER)            += avuidec.o
OBJS-$(CONFIG_AYUV_DECODER)            += v408dec.o
OBJS-$(CONFIG_AYUV_ENCODER)            += v408enc.o
OBJS-$(CONFIG_BETHSOFTVID_DECODER)     += bethsoftvideo.o
OBJS-$(CONFIG_BFI_DECODER)             += bfi.o
OBJS-$(CONFIG_BINK_DECODER)            += bink.o binkdsp.o
OBJS-$(CONFIG_BINKAUDIO_DCT_DECODER)   += binkaudio.o wma.o wma_common.o
OBJS-$(CONFIG_BINKAUDIO_RDFT_DECODER)  += binkaudio.o wma.o wma_common.o
OBJS-$(CONFIG_BINTEXT_DECODER)         += bintext.o cga_data.o
OBJS-$(CONFIG_BMP_DECODER)             += bmp.o msrledec.o
OBJS-$(CONFIG_BMP_ENCODER)             += bmpenc.o
OBJS-$(CONFIG_BMV_VIDEO_DECODER)       += bmv.o
OBJS-$(CONFIG_BMV_AUDIO_DECODER)       += bmv.o
OBJS-$(CONFIG_C93_DECODER)             += c93.o
OBJS-$(CONFIG_CAVS_DECODER)            += cavs.o cavsdec.o cavsdsp.o \
                                          mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_CDGRAPHICS_DECODER)      += cdgraphics.o
OBJS-$(CONFIG_CDXL_DECODER)            += cdxl.o
OBJS-$(CONFIG_CINEPAK_DECODER)         += cinepak.o
OBJS-$(CONFIG_CLJR_DECODER)            += cljr.o
OBJS-$(CONFIG_CLJR_ENCODER)            += cljr.o
OBJS-$(CONFIG_COOK_DECODER)            += cook.o
OBJS-$(CONFIG_CSCD_DECODER)            += cscd.o
OBJS-$(CONFIG_CYUV_DECODER)            += cyuv.o
OBJS-$(CONFIG_DCA_DECODER)             += dca.o synth_filter.o dcadsp.o \
                                          dca_parser.o
OBJS-$(CONFIG_DCA_ENCODER)             += dcaenc.o
OBJS-$(CONFIG_DIRAC_DECODER)           += diracdec.o dirac.o diracdsp.o \
                                          dirac_arith.o mpeg12data.o dwt.o
OBJS-$(CONFIG_DFA_DECODER)             += dfa.o
OBJS-$(CONFIG_DNXHD_DECODER)           += dnxhddec.o dnxhddata.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += dnxhdenc.o dnxhddata.o       \
                                          mpegvideo_enc.o motion_est.o \
                                          ratecontrol.o mpeg12data.o   \
                                          mpegvideo.o
OBJS-$(CONFIG_DPX_DECODER)             += dpx.o
OBJS-$(CONFIG_DPX_ENCODER)             += dpxenc.o
OBJS-$(CONFIG_DSICINAUDIO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DSICINVIDEO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DVBSUB_DECODER)          += dvbsubdec.o
OBJS-$(CONFIG_DVBSUB_ENCODER)          += dvbsub.o
OBJS-$(CONFIG_DVDSUB_DECODER)          += dvdsubdec.o
OBJS-$(CONFIG_DVDSUB_ENCODER)          += dvdsubenc.o
OBJS-$(CONFIG_DVVIDEO_DECODER)         += dvdec.o dv.o dvdata.o dv_profile.o
OBJS-$(CONFIG_DVVIDEO_ENCODER)         += dv.o dvdata.o dv_profile.o
OBJS-$(CONFIG_DXA_DECODER)             += dxa.o
OBJS-$(CONFIG_DXTORY_DECODER)          += dxtory.o
OBJS-$(CONFIG_EAC3_DECODER)            += eac3dec.o eac3_data.o
OBJS-$(CONFIG_EAC3_ENCODER)            += eac3enc.o ac3enc.o ac3enc_float.o \
                                          ac3tab.o ac3.o kbdwin.o eac3_data.o
OBJS-$(CONFIG_EACMV_DECODER)           += eacmv.o
OBJS-$(CONFIG_EAMAD_DECODER)           += eamad.o eaidct.o mpeg12.o \
                                          mpeg12data.o mpegvideo.o  \
                                          error_resilience.o
OBJS-$(CONFIG_EATGQ_DECODER)           += eatgq.o eaidct.o
OBJS-$(CONFIG_EATGV_DECODER)           += eatgv.o
OBJS-$(CONFIG_EATQI_DECODER)           += eatqi.o eaidct.o mpeg12.o \
                                          mpeg12data.o mpegvideo.o  \
                                          error_resilience.o
OBJS-$(CONFIG_EIGHTBPS_DECODER)        += 8bps.o
OBJS-$(CONFIG_EIGHTSVX_EXP_DECODER)    += 8svx.o
OBJS-$(CONFIG_EIGHTSVX_FIB_DECODER)    += 8svx.o
OBJS-$(CONFIG_EIGHTSVX_RAW_DECODER)    += 8svx.o
OBJS-$(CONFIG_ESCAPE124_DECODER)       += escape124.o
OBJS-$(CONFIG_ESCAPE130_DECODER)       += escape130.o
OBJS-$(CONFIG_EXR_DECODER)             += exr.o
OBJS-$(CONFIG_FFV1_DECODER)            += ffv1.o rangecoder.o
OBJS-$(CONFIG_FFV1_ENCODER)            += ffv1.o rangecoder.o
OBJS-$(CONFIG_FFVHUFF_DECODER)         += huffyuv.o
OBJS-$(CONFIG_FFVHUFF_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_FFWAVESYNTH_DECODER)     += ffwavesynth.o
OBJS-$(CONFIG_FLAC_DECODER)            += flacdec.o flacdata.o flac.o
OBJS-$(CONFIG_FLAC_ENCODER)            += flacenc.o flacdata.o flac.o vorbis_data.o
OBJS-$(CONFIG_FLASHSV_DECODER)         += flashsv.o
OBJS-$(CONFIG_FLASHSV_ENCODER)         += flashsvenc.o
OBJS-$(CONFIG_FLASHSV2_ENCODER)        += flashsv2enc.o
OBJS-$(CONFIG_FLASHSV2_DECODER)        += flashsv.o
OBJS-$(CONFIG_FLIC_DECODER)            += flicvideo.o
OBJS-$(CONFIG_FOURXM_DECODER)          += 4xm.o
OBJS-$(CONFIG_FRAPS_DECODER)           += fraps.o
OBJS-$(CONFIG_FRWU_DECODER)            += frwu.o
OBJS-$(CONFIG_G723_1_DECODER)          += g723_1.o acelp_vectors.o \
                                          celp_filters.o celp_math.o
OBJS-$(CONFIG_G723_1_ENCODER)          += g723_1.o
OBJS-$(CONFIG_G729_DECODER)            += g729dec.o lsp.o celp_math.o acelp_filters.o acelp_pitch_delay.o acelp_vectors.o g729postfilter.o
OBJS-$(CONFIG_GIF_DECODER)             += gifdec.o lzw.o
OBJS-$(CONFIG_GIF_ENCODER)             += gif.o lzwenc.o
OBJS-$(CONFIG_GSM_DECODER)             += gsmdec.o gsmdec_data.o msgsmdec.o
OBJS-$(CONFIG_GSM_MS_DECODER)          += gsmdec.o gsmdec_data.o msgsmdec.o
OBJS-$(CONFIG_H261_DECODER)            += h261dec.o h261.o h261data.o  \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H261_ENCODER)            += h261enc.o h261.o h261data.o  \
                                          mpegvideo_enc.o motion_est.o \
                                          ratecontrol.o mpeg12data.o   \
                                          mpegvideo.o
OBJS-$(CONFIG_H263_DECODER)            += h263dec.o h263.o ituh263dec.o        \
                                          mpeg4video.o mpeg4videodec.o flvdec.o\
                                          intelh263dec.o mpegvideo.o           \
                                          error_resilience.o
OBJS-$(CONFIG_H263_VAAPI_HWACCEL)      += vaapi_mpeg4.o
OBJS-$(CONFIG_H263_ENCODER)            += mpegvideo_enc.o mpeg4video.o      \
                                          mpeg4videoenc.o motion_est.o      \
                                          ratecontrol.o h263.o ituh263enc.o \
                                          flvenc.o mpeg12data.o             \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H264_DECODER)            += h264.o                               \
                                          h264_loopfilter.o h264_direct.o      \
                                          cabac.o h264_sei.o h264_ps.o         \
                                          h264_refs.o h264_cavlc.o h264_cabac.o\
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H264_DXVA2_HWACCEL)      += dxva2_h264.o
OBJS-$(CONFIG_H264_VAAPI_HWACCEL)      += vaapi_h264.o
OBJS-$(CONFIG_H264_VDA_HWACCEL)        += vda_h264.o
OBJS-$(CONFIG_HUFFYUV_DECODER)         += huffyuv.o
OBJS-$(CONFIG_HUFFYUV_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_IDCIN_DECODER)           += idcinvideo.o
OBJS-$(CONFIG_IDF_DECODER)             += bintext.o cga_data.o
OBJS-$(CONFIG_IFF_BYTERUN1_DECODER)    += iff.o
OBJS-$(CONFIG_IFF_ILBM_DECODER)        += iff.o
OBJS-$(CONFIG_IMC_DECODER)             += imc.o
OBJS-$(CONFIG_INDEO2_DECODER)          += indeo2.o
OBJS-$(CONFIG_INDEO3_DECODER)          += indeo3.o
OBJS-$(CONFIG_INDEO4_DECODER)          += indeo4.o ivi_common.o ivi_dsp.o
OBJS-$(CONFIG_INDEO5_DECODER)          += indeo5.o ivi_common.o ivi_dsp.o
OBJS-$(CONFIG_INTERPLAY_DPCM_DECODER)  += dpcm.o
OBJS-$(CONFIG_INTERPLAY_VIDEO_DECODER) += interplayvideo.o
OBJS-$(CONFIG_JACOSUB_DECODER)         += jacosubdec.o ass.o
OBJS-$(CONFIG_JPEG2000_DECODER)        += j2kdec.o mqcdec.o mqc.o j2k.o j2k_dwt.o
OBJS-$(CONFIG_JPEG2000_ENCODER)        += j2kenc.o mqcenc.o mqc.o j2k.o j2k_dwt.o
OBJS-$(CONFIG_JPEGLS_DECODER)          += jpeglsdec.o jpegls.o \
                                          mjpegdec.o mjpeg.o
OBJS-$(CONFIG_JPEGLS_ENCODER)          += jpeglsenc.o jpegls.o
OBJS-$(CONFIG_JV_DECODER)              += jvdec.o
OBJS-$(CONFIG_KGV1_DECODER)            += kgv1dec.o
OBJS-$(CONFIG_KMVC_DECODER)            += kmvc.o
OBJS-$(CONFIG_LAGARITH_DECODER)        += lagarith.o lagarithrac.o
OBJS-$(CONFIG_LJPEG_ENCODER)           += ljpegenc.o mjpegenc.o mjpeg.o \
                                          mpegvideo_enc.o motion_est.o  \
                                          ratecontrol.o mpeg12data.o    \
                                          mpegvideo.o
OBJS-$(CONFIG_LOCO_DECODER)            += loco.o
OBJS-$(CONFIG_MACE3_DECODER)           += mace.o
OBJS-$(CONFIG_MACE6_DECODER)           += mace.o
OBJS-$(CONFIG_MDEC_DECODER)            += mdec.o mpeg12.o mpeg12data.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MICRODVD_DECODER)        += microdvddec.o ass.o
OBJS-$(CONFIG_MIMIC_DECODER)           += mimic.o
OBJS-$(CONFIG_MJPEG_DECODER)           += mjpegdec.o mjpeg.o
OBJS-$(CONFIG_MJPEG_ENCODER)           += mjpegenc.o mjpeg.o           \
                                          mpegvideo_enc.o motion_est.o \
                                          ratecontrol.o mpeg12data.o   \
                                          mpegvideo.o
OBJS-$(CONFIG_MJPEGB_DECODER)          += mjpegbdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_MLP_DECODER)             += mlpdec.o mlpdsp.o
OBJS-$(CONFIG_MMVIDEO_DECODER)         += mmvideo.o
OBJS-$(CONFIG_MOTIONPIXELS_DECODER)    += motionpixels.o
OBJS-$(CONFIG_MP1_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP1FLOAT_DECODER)        += mpegaudiodec_float.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP2_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP2_ENCODER)             += mpegaudioenc.o mpegaudio.o \
                                          mpegaudiodata.o
OBJS-$(CONFIG_MP2FLOAT_DECODER)        += mpegaudiodec_float.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3ADU_DECODER)          += mpegaudiodec.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3ADUFLOAT_DECODER)     += mpegaudiodec_float.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3ON4_DECODER)          += mpegaudiodec.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o         \
                                          mpeg4audio.o
OBJS-$(CONFIG_MP3ON4FLOAT_DECODER)     += mpegaudiodec_float.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o         \
                                          mpeg4audio.o
OBJS-$(CONFIG_MP3_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3FLOAT_DECODER)        += mpegaudiodec_float.o mpegaudiodecheader.o \
                                          mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MPC7_DECODER)            += mpc7.o mpc.o mpegaudiodec.o      \
                                          mpegaudiodecheader.o mpegaudio.o \
                                          mpegaudiodata.o
OBJS-$(CONFIG_MPC8_DECODER)            += mpc8.o mpc.o mpegaudiodec.o      \
                                          mpegaudiodecheader.o mpegaudio.o \
                                          mpegaudiodata.o
OBJS-$(CONFIG_MPEGVIDEO_DECODER)       += mpeg12.o mpeg12data.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG_XVMC_DECODER)       += mpegvideo_xvmc.o
OBJS-$(CONFIG_MPEG1VIDEO_DECODER)      += mpeg12.o mpeg12data.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG1VIDEO_ENCODER)      += mpeg12enc.o mpegvideo_enc.o \
                                          timecode.o                  \
                                          motion_est.o ratecontrol.o  \
                                          mpeg12.o mpeg12data.o       \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG2_DXVA2_HWACCEL)     += dxva2_mpeg2.o
OBJS-$(CONFIG_MPEG2_VAAPI_HWACCEL)     += vaapi_mpeg2.o
OBJS-$(CONFIG_MPEG2VIDEO_DECODER)      += mpeg12.o mpeg12data.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG2VIDEO_ENCODER)      += mpeg12enc.o mpegvideo_enc.o \
                                          timecode.o                  \
                                          motion_est.o ratecontrol.o  \
                                          mpeg12.o mpeg12data.o       \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG4_VAAPI_HWACCEL)     += vaapi_mpeg4.o
OBJS-$(CONFIG_MSMPEG4V1_DECODER)       += msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSMPEG4V2_DECODER)       += msmpeg4.o msmpeg4data.o h263dec.o \
                                          h263.o ituh263dec.o mpeg4videodec.o
OBJS-$(CONFIG_MSMPEG4V2_ENCODER)       += msmpeg4.o msmpeg4enc.o msmpeg4data.o \
                                          h263dec.o h263.o ituh263dec.o        \
                                          mpeg4videodec.o
OBJS-$(CONFIG_MSMPEG4V3_DECODER)       += msmpeg4.o msmpeg4data.o h263dec.o \
                                          h263.o ituh263dec.o mpeg4videodec.o
OBJS-$(CONFIG_MSMPEG4V3_ENCODER)       += msmpeg4.o msmpeg4enc.o msmpeg4data.o \
                                          h263dec.o h263.o ituh263dec.o        \
                                          mpeg4videodec.o
OBJS-$(CONFIG_MSRLE_DECODER)           += msrle.o msrledec.o
OBJS-$(CONFIG_MSVIDEO1_DECODER)        += msvideo1.o
OBJS-$(CONFIG_MSVIDEO1_ENCODER)        += msvideo1enc.o elbg.o
OBJS-$(CONFIG_MSZH_DECODER)            += lcldec.o
OBJS-$(CONFIG_MXPEG_DECODER)           += mxpegdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_NELLYMOSER_DECODER)      += nellymoserdec.o nellymoser.o
OBJS-$(CONFIG_NELLYMOSER_ENCODER)      += nellymoserenc.o nellymoser.o \
                                          audio_frame_queue.o
OBJS-$(CONFIG_NUV_DECODER)             += nuv.o rtjpeg.o
OBJS-$(CONFIG_PAM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PAM_ENCODER)             += pamenc.o pnm.o
OBJS-$(CONFIG_PBM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PBM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PCX_DECODER)             += pcx.o
OBJS-$(CONFIG_PCX_ENCODER)             += pcxenc.o
OBJS-$(CONFIG_PGM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PGM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PGMYUV_DECODER)          += pnmdec.o pnm.o
OBJS-$(CONFIG_PGMYUV_ENCODER)          += pnmenc.o pnm.o
OBJS-$(CONFIG_PGSSUB_DECODER)          += pgssubdec.o
OBJS-$(CONFIG_PICTOR_DECODER)          += pictordec.o cga_data.o
OBJS-$(CONFIG_PNG_DECODER)             += png.o pngdec.o pngdsp.o
OBJS-$(CONFIG_PNG_ENCODER)             += png.o pngenc.o
OBJS-$(CONFIG_PPM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PPM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PRORES_DECODER)          += proresdec2.o
OBJS-$(CONFIG_PRORES_LGPL_DECODER)     += proresdec_lgpl.o proresdsp.o proresdata.o
OBJS-$(CONFIG_PRORES_ENCODER)          += proresenc_anatoliy.o
OBJS-$(CONFIG_PRORES_ANATOLIY_ENCODER) += proresenc_anatoliy.o
OBJS-$(CONFIG_PRORES_KOSTYA_ENCODER)   += proresenc_kostya.o proresdata.o proresdsp.o
OBJS-$(CONFIG_PTX_DECODER)             += ptx.o
OBJS-$(CONFIG_QCELP_DECODER)           += qcelpdec.o celp_math.o         \
                                          celp_filters.o acelp_vectors.o \
                                          acelp_filters.o
OBJS-$(CONFIG_QDM2_DECODER)            += qdm2.o mpegaudiodec.o            \
                                          mpegaudiodecheader.o mpegaudio.o \
                                          mpegaudiodata.o
OBJS-$(CONFIG_QDRAW_DECODER)           += qdrw.o
OBJS-$(CONFIG_QPEG_DECODER)            += qpeg.o
OBJS-$(CONFIG_QTRLE_DECODER)           += qtrle.o
OBJS-$(CONFIG_QTRLE_ENCODER)           += qtrleenc.o
OBJS-$(CONFIG_R10K_DECODER)            += r210dec.o
OBJS-$(CONFIG_R10K_ENCODER)            += r210enc.o
OBJS-$(CONFIG_R210_DECODER)            += r210dec.o
OBJS-$(CONFIG_R210_ENCODER)            += r210enc.o
OBJS-$(CONFIG_RA_144_DECODER)          += ra144dec.o ra144.o celp_filters.o
OBJS-$(CONFIG_RA_144_ENCODER)          += ra144enc.o ra144.o celp_filters.o \
                                          audio_frame_queue.o
OBJS-$(CONFIG_RA_288_DECODER)          += ra288.o celp_math.o celp_filters.o
OBJS-$(CONFIG_RALF_DECODER)            += ralf.o
OBJS-$(CONFIG_RAWVIDEO_DECODER)        += rawdec.o
OBJS-$(CONFIG_RAWVIDEO_ENCODER)        += rawenc.o
OBJS-$(CONFIG_RL2_DECODER)             += rl2.o
OBJS-$(CONFIG_ROQ_DECODER)             += roqvideodec.o roqvideo.o
OBJS-$(CONFIG_ROQ_ENCODER)             += roqvideoenc.o roqvideo.o elbg.o
OBJS-$(CONFIG_ROQ_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_ROQ_DPCM_ENCODER)        += roqaudioenc.o
OBJS-$(CONFIG_RPZA_DECODER)            += rpza.o
OBJS-$(CONFIG_RV10_DECODER)            += rv10.o
OBJS-$(CONFIG_RV10_ENCODER)            += rv10enc.o
OBJS-$(CONFIG_RV20_DECODER)            += rv10.o
OBJS-$(CONFIG_RV20_ENCODER)            += rv20enc.o
OBJS-$(CONFIG_RV30_DECODER)            += rv30.o rv34.o rv30dsp.o rv34dsp.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV40_DECODER)            += rv40.o rv34.o rv34dsp.o rv40dsp.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_S302M_DECODER)           += s302m.o
OBJS-$(CONFIG_SGI_DECODER)             += sgidec.o
OBJS-$(CONFIG_SGI_ENCODER)             += sgienc.o rle.o
OBJS-$(CONFIG_SHORTEN_DECODER)         += shorten.o
OBJS-$(CONFIG_SIPR_DECODER)            += sipr.o acelp_pitch_delay.o \
                                          celp_math.o acelp_vectors.o \
                                          acelp_filters.o celp_filters.o \
                                          sipr16k.o
OBJS-$(CONFIG_SMACKAUD_DECODER)        += smacker.o
OBJS-$(CONFIG_SMACKER_DECODER)         += smacker.o
OBJS-$(CONFIG_SMC_DECODER)             += smc.o
OBJS-$(CONFIG_SNOW_DECODER)            += snowdec.o snow.o rangecoder.o
OBJS-$(CONFIG_SNOW_ENCODER)            += snowenc.o snow.o rangecoder.o    \
                                          motion_est.o ratecontrol.o       \
                                          h263.o mpegvideo.o               \
                                          error_resilience.o ituh263enc.o  \
                                          mpegvideo_enc.o mpeg12data.o
OBJS-$(CONFIG_SOL_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_SONIC_DECODER)           += sonic.o
OBJS-$(CONFIG_SONIC_ENCODER)           += sonic.o
OBJS-$(CONFIG_SONIC_LS_ENCODER)        += sonic.o
OBJS-$(CONFIG_SP5X_DECODER)            += sp5xdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_SRT_DECODER)             += srtdec.o ass.o
OBJS-$(CONFIG_SRT_ENCODER)             += srtenc.o ass_split.o
OBJS-$(CONFIG_SUNRAST_DECODER)         += sunrast.o
OBJS-$(CONFIG_SUNRAST_ENCODER)         += sunrastenc.o
OBJS-$(CONFIG_SVQ1_DECODER)            += svq1dec.o svq1.o h263.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += svq1enc.o svq1.o    \
                                          motion_est.o h263.o \
                                          mpegvideo.o error_resilience.o \
                                          ituh263enc.o mpegvideo_enc.o   \
                                          ratecontrol.o mpeg12data.o
OBJS-$(CONFIG_SVQ3_DECODER)            += h264.o svq3.o                       \
                                          h264_loopfilter.o h264_direct.o     \
                                          h264_sei.o h264_ps.o h264_refs.o    \
                                          h264_cavlc.o h264_cabac.o cabac.o   \
                                          mpegvideo.o error_resilience.o      \
                                          svq1dec.o svq1.o h263.o
OBJS-$(CONFIG_TARGA_DECODER)           += targa.o
OBJS-$(CONFIG_TARGA_ENCODER)           += targaenc.o rle.o
OBJS-$(CONFIG_THEORA_DECODER)          += xiph.o
OBJS-$(CONFIG_THP_DECODER)             += mjpegdec.o mjpeg.o
OBJS-$(CONFIG_TIERTEXSEQVIDEO_DECODER) += tiertexseqv.o
OBJS-$(CONFIG_TIFF_DECODER)            += tiff.o lzw.o faxcompr.o
OBJS-$(CONFIG_TIFF_ENCODER)            += tiffenc.o rle.o lzwenc.o
OBJS-$(CONFIG_TMV_DECODER)             += tmv.o cga_data.o
OBJS-$(CONFIG_TRUEMOTION1_DECODER)     += truemotion1.o
OBJS-$(CONFIG_TRUEMOTION2_DECODER)     += truemotion2.o
OBJS-$(CONFIG_TRUESPEECH_DECODER)      += truespeech.o
OBJS-$(CONFIG_TSCC_DECODER)            += tscc.o msrledec.o
OBJS-$(CONFIG_TTA_DECODER)             += tta.o
OBJS-$(CONFIG_TWINVQ_DECODER)          += twinvq.o celp_math.o
OBJS-$(CONFIG_TXD_DECODER)             += txd.o s3tc.o
OBJS-$(CONFIG_ULTI_DECODER)            += ulti.o
OBJS-$(CONFIG_UTVIDEO_DECODER)         += utvideo.o
OBJS-$(CONFIG_V210_DECODER)            += v210dec.o
OBJS-$(CONFIG_V210_ENCODER)            += v210enc.o
OBJS-$(CONFIG_V308_DECODER)            += v308dec.o
OBJS-$(CONFIG_V308_ENCODER)            += v308enc.o
OBJS-$(CONFIG_V408_DECODER)            += v408dec.o
OBJS-$(CONFIG_V408_ENCODER)            += v408enc.o
OBJS-$(CONFIG_V410_DECODER)            += v410dec.o
OBJS-$(CONFIG_V410_ENCODER)            += v410enc.o
OBJS-$(CONFIG_V210X_DECODER)           += v210x.o
OBJS-$(CONFIG_VB_DECODER)              += vb.o
OBJS-$(CONFIG_VBLE_DECODER)            += vble.o
OBJS-$(CONFIG_VC1_DECODER)             += vc1dec.o vc1.o vc1data.o vc1dsp.o \
                                          msmpeg4.o msmpeg4data.o           \
                                          intrax8.o intrax8dsp.o
OBJS-$(CONFIG_VC1_DXVA2_HWACCEL)       += dxva2_vc1.o
OBJS-$(CONFIG_VC1_VAAPI_HWACCEL)       += vaapi_vc1.o
OBJS-$(CONFIG_VCR1_DECODER)            += vcr1.o
OBJS-$(CONFIG_VCR1_ENCODER)            += vcr1.o
OBJS-$(CONFIG_VMDAUDIO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMDVIDEO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMNC_DECODER)            += vmnc.o
OBJS-$(CONFIG_VORBIS_DECODER)          += vorbisdec.o vorbis.o \
                                          vorbis_data.o xiph.o
OBJS-$(CONFIG_VORBIS_ENCODER)          += vorbisenc.o vorbis.o \
                                          vorbis_data.o
OBJS-$(CONFIG_VP3_DECODER)             += vp3.o vp3dsp.o
OBJS-$(CONFIG_VP5_DECODER)             += vp5.o vp56.o vp56data.o vp56dsp.o \
                                          vp3dsp.o vp56rac.o
OBJS-$(CONFIG_VP6_DECODER)             += vp6.o vp56.o vp56data.o vp56dsp.o \
                                          vp3dsp.o vp6dsp.o vp56rac.o
OBJS-$(CONFIG_VP8_DECODER)             += vp8.o vp8dsp.o vp56rac.o
OBJS-$(CONFIG_VQA_DECODER)             += vqavideo.o
OBJS-$(CONFIG_WAVPACK_DECODER)         += wavpack.o
OBJS-$(CONFIG_WMALOSSLESS_DECODER)     += wmalosslessdec.o wma_common.o
OBJS-$(CONFIG_WMAPRO_DECODER)          += wmaprodec.o wma.o wma_common.o
OBJS-$(CONFIG_WMAV1_DECODER)           += wmadec.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV1_ENCODER)           += wmaenc.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV2_DECODER)           += wmadec.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV2_ENCODER)           += wmaenc.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAVOICE_DECODER)        += wmavoice.o \
                                          celp_math.o celp_filters.o \
                                          acelp_vectors.o acelp_filters.o
OBJS-$(CONFIG_WMV1_DECODER)            += msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_WMV2_DECODER)            += wmv2dec.o wmv2.o        \
                                          msmpeg4.o msmpeg4data.o \
                                          intrax8.o intrax8dsp.o
OBJS-$(CONFIG_WMV2_ENCODER)            += wmv2enc.o wmv2.o \
                                          msmpeg4.o msmpeg4enc.o msmpeg4data.o \
                                          mpeg4videodec.o ituh263dec.o h263dec.o
OBJS-$(CONFIG_WNV1_DECODER)            += wnv1.o
OBJS-$(CONFIG_WS_SND1_DECODER)         += ws-snd1.o
OBJS-$(CONFIG_XAN_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_XAN_WC3_DECODER)         += xan.o
OBJS-$(CONFIG_XAN_WC4_DECODER)         += xxan.o
OBJS-$(CONFIG_XBIN_DECODER)            += bintext.o cga_data.o
OBJS-$(CONFIG_XBM_DECODER)             += xbmdec.o
OBJS-$(CONFIG_XBM_ENCODER)             += xbmenc.o
OBJS-$(CONFIG_XL_DECODER)              += xl.o
OBJS-$(CONFIG_XSUB_DECODER)            += xsubdec.o
OBJS-$(CONFIG_XSUB_ENCODER)            += xsubenc.o
OBJS-$(CONFIG_XWD_DECODER)             += xwddec.o
OBJS-$(CONFIG_XWD_ENCODER)             += xwdenc.o
OBJS-$(CONFIG_Y41P_DECODER)            += y41pdec.o
OBJS-$(CONFIG_Y41P_ENCODER)            += y41penc.o
OBJS-$(CONFIG_YOP_DECODER)             += yop.o
OBJS-$(CONFIG_YUV4_DECODER)            += yuv4dec.o
OBJS-$(CONFIG_YUV4_ENCODER)            += yuv4enc.o
OBJS-$(CONFIG_ZEROCODEC_DECODER)       += zerocodec.o
OBJS-$(CONFIG_ZLIB_DECODER)            += lcldec.o
OBJS-$(CONFIG_ZLIB_ENCODER)            += lclenc.o
OBJS-$(CONFIG_ZMBV_DECODER)            += zmbv.o
OBJS-$(CONFIG_ZMBV_ENCODER)            += zmbvenc.o

# (AD)PCM decoders/encoders
OBJS-$(CONFIG_PCM_ALAW_DECODER)           += pcm.o
OBJS-$(CONFIG_PCM_ALAW_ENCODER)           += pcm.o
OBJS-$(CONFIG_PCM_BLURAY_DECODER)         += pcm-mpeg.o
OBJS-$(CONFIG_PCM_DVD_DECODER)            += pcm.o
OBJS-$(CONFIG_PCM_DVD_ENCODER)            += pcm.o
OBJS-$(CONFIG_PCM_F32BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_F32BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_F32LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_F32LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_F64BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_F64BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_F64LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_F64LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_LXF_DECODER)            += pcm.o
OBJS-$(CONFIG_PCM_MULAW_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_MULAW_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S8_DECODER)             += pcm.o
OBJS-$(CONFIG_PCM_S8_ENCODER)             += pcm.o
OBJS-$(CONFIG_PCM_S8_PLANAR_DECODER)      += 8svx.o
OBJS-$(CONFIG_PCM_S16BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16LE_PLANAR_DECODER)   += pcm.o
OBJS-$(CONFIG_PCM_S24BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_DECODER)        += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_ENCODER)        += pcm.o
OBJS-$(CONFIG_PCM_S24LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U8_DECODER)             += pcm.o
OBJS-$(CONFIG_PCM_U8_ENCODER)             += pcm.o
OBJS-$(CONFIG_PCM_U16BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U16BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U16LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U16LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U24BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U24BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U24LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U24LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U32BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U32BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_U32LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_U32LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_ZORK_DECODER)           += pcm.o

OBJS-$(CONFIG_ADPCM_4XM_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_ADX_DECODER)          += adxdec.o adx.o
OBJS-$(CONFIG_ADPCM_ADX_ENCODER)          += adxenc.o adx.o
OBJS-$(CONFIG_ADPCM_CT_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_MAXIS_XA_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R1_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R2_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R3_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_XAS_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_G722_DECODER)         += g722.o g722dec.o
OBJS-$(CONFIG_ADPCM_G722_ENCODER)         += g722.o g722enc.o
OBJS-$(CONFIG_ADPCM_G726_DECODER)         += g726.o
OBJS-$(CONFIG_ADPCM_G726_ENCODER)         += g726.o
OBJS-$(CONFIG_ADPCM_IMA_AMV_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_APC_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_DK3_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_DK4_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_EA_EACS_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_EA_SEAD_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_ISS_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_QT_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_QT_ENCODER)       += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_SMJPEG_DECODER)   += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_ENCODER)      += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WS_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_MS_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_MS_ENCODER)           += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_2_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_3_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_4_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SWF_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SWF_ENCODER)          += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_THP_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_XA_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_YAMAHA_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_YAMAHA_ENCODER)       += adpcmenc.o adpcm_data.o

# libavformat dependencies
OBJS-$(CONFIG_ADTS_MUXER)              += mpeg4audio.o
OBJS-$(CONFIG_ADX_DEMUXER)             += adx.o
OBJS-$(CONFIG_CAF_DEMUXER)             += mpeg4audio.o mpegaudiodata.o
OBJS-$(CONFIG_DV_DEMUXER)              += dv_profile.o
OBJS-$(CONFIG_DV_MUXER)                += dv_profile.o timecode.o
OBJS-$(CONFIG_FLAC_DEMUXER)            += flacdec.o flacdata.o flac.o vorbis_data.o
OBJS-$(CONFIG_FLAC_MUXER)              += flacdec.o flacdata.o flac.o vorbis_data.o
OBJS-$(CONFIG_FLV_DEMUXER)             += mpeg4audio.o
OBJS-$(CONFIG_GXF_DEMUXER)             += mpeg12data.o
OBJS-$(CONFIG_IFF_DEMUXER)             += iff.o
OBJS-$(CONFIG_LATM_MUXER)              += mpeg4audio.o
OBJS-$(CONFIG_MATROSKA_AUDIO_MUXER)    += xiph.o mpeg4audio.o vorbis_data.o \
                                          flacdec.o flacdata.o flac.o
OBJS-$(CONFIG_MATROSKA_DEMUXER)        += mpeg4audio.o mpegaudiodata.o
OBJS-$(CONFIG_MATROSKA_MUXER)          += xiph.o mpeg4audio.o \
                                          flacdec.o flacdata.o flac.o \
                                          mpegaudiodata.o vorbis_data.o
OBJS-$(CONFIG_MP3_MUXER)               += mpegaudiodata.o mpegaudiodecheader.o
OBJS-$(CONFIG_MOV_DEMUXER)             += mpeg4audio.o mpegaudiodata.o ac3tab.o timecode.o
OBJS-$(CONFIG_MOV_MUXER)               += mpeg4audio.o mpegaudiodata.o
OBJS-$(CONFIG_MPEGTS_MUXER)            += mpegvideo.o mpeg4audio.o
OBJS-$(CONFIG_MPEGTS_DEMUXER)          += mpeg4audio.o mpegaudiodata.o
OBJS-$(CONFIG_MXF_MUXER)               += timecode.o
OBJS-$(CONFIG_NUT_MUXER)               += mpegaudiodata.o
OBJS-$(CONFIG_OGG_DEMUXER)             += flacdec.o flacdata.o flac.o \
                                          dirac.o mpeg12data.o vorbis_parser.o vorbis_data.o
OBJS-$(CONFIG_OGG_MUXER)               += xiph.o flacdec.o flacdata.o flac.o \
                                          vorbis_data.o
OBJS-$(CONFIG_RTP_MUXER)               += mpeg4audio.o mpegvideo.o xiph.o
OBJS-$(CONFIG_SPDIF_DEMUXER)           += aacadtsdec.o mpeg4audio.o
OBJS-$(CONFIG_WEBM_MUXER)              += xiph.o mpeg4audio.o \
                                          flacdec.o flacdata.o flac.o \
                                          mpegaudiodata.o vorbis_data.o
OBJS-$(CONFIG_WTV_DEMUXER)             += mpeg4audio.o mpegaudiodata.o

# external codec libraries
OBJS-$(CONFIG_LIBAACPLUS_ENCODER)         += libaacplus.o
OBJS-$(CONFIG_LIBCELT_DECODER)            += libcelt_dec.o
OBJS-$(CONFIG_LIBFAAC_ENCODER)            += libfaac.o audio_frame_queue.o
OBJS-$(CONFIG_LIBGSM_DECODER)             += libgsm.o
OBJS-$(CONFIG_LIBGSM_ENCODER)             += libgsm.o
OBJS-$(CONFIG_LIBGSM_MS_DECODER)          += libgsm.o
OBJS-$(CONFIG_LIBGSM_MS_ENCODER)          += libgsm.o
OBJS-$(CONFIG_LIBMP3LAME_ENCODER)         += libmp3lame.o mpegaudiodecheader.o \
                                             audio_frame_queue.o
OBJS-$(CONFIG_LIBOPENCORE_AMRNB_DECODER)  += libopencore-amr.o \
                                             audio_frame_queue.o
OBJS-$(CONFIG_LIBOPENCORE_AMRNB_ENCODER)  += libopencore-amr.o \
                                             audio_frame_queue.o
OBJS-$(CONFIG_LIBOPENCORE_AMRWB_DECODER)  += libopencore-amr.o
OBJS-$(CONFIG_LIBOPENJPEG_DECODER)        += libopenjpegdec.o
OBJS-$(CONFIG_LIBOPENJPEG_ENCODER)        += libopenjpegenc.o
OBJS-$(CONFIG_LIBSCHROEDINGER_DECODER)    += libschroedingerdec.o \
                                             libschroedinger.o
OBJS-$(CONFIG_LIBSCHROEDINGER_ENCODER)    += libschroedingerenc.o \
                                             libschroedinger.o
OBJS-$(CONFIG_LIBSPEEX_DECODER)           += libspeexdec.o
OBJS-$(CONFIG_LIBSPEEX_ENCODER)           += libspeexenc.o audio_frame_queue.o
OBJS-$(CONFIG_LIBSTAGEFRIGHT_H264_DECODER)+= libstagefright.o
OBJS-$(CONFIG_LIBTHEORA_ENCODER)          += libtheoraenc.o
OBJS-$(CONFIG_LIBUTVIDEO_DECODER)         += libutvideodec.o
OBJS-$(CONFIG_LIBUTVIDEO_ENCODER)         += libutvideoenc.o
OBJS-$(CONFIG_LIBVO_AACENC_ENCODER)       += libvo-aacenc.o mpeg4audio.o \
                                             audio_frame_queue.o
OBJS-$(CONFIG_LIBVO_AMRWBENC_ENCODER)     += libvo-amrwbenc.o
OBJS-$(CONFIG_LIBVORBIS_ENCODER)          += libvorbis.o audio_frame_queue.o \
                                             vorbis_data.o vorbis_parser.o
OBJS-$(CONFIG_LIBVPX_DECODER)             += libvpxdec.o
OBJS-$(CONFIG_LIBVPX_ENCODER)             += libvpxenc.o
OBJS-$(CONFIG_LIBX264_ENCODER)            += libx264.o
OBJS-$(CONFIG_LIBXAVS_ENCODER)            += libxavs.o
OBJS-$(CONFIG_LIBXVID_ENCODER)            += libxvid.o

# parsers
OBJS-$(CONFIG_AAC_PARSER)              += aac_parser.o aac_ac3_parser.o \
                                          aacadtsdec.o mpeg4audio.o
OBJS-$(CONFIG_AC3_PARSER)              += ac3_parser.o ac3tab.o \
                                          aac_ac3_parser.o
OBJS-$(CONFIG_ADX_PARSER)              += adx_parser.o adx.o
OBJS-$(CONFIG_CAVSVIDEO_PARSER)        += cavs_parser.o
OBJS-$(CONFIG_COOK_PARSER)             += cook_parser.o
OBJS-$(CONFIG_DCA_PARSER)              += dca_parser.o
OBJS-$(CONFIG_DIRAC_PARSER)            += dirac_parser.o
OBJS-$(CONFIG_DNXHD_PARSER)            += dnxhd_parser.o
OBJS-$(CONFIG_DVBSUB_PARSER)           += dvbsub_parser.o
OBJS-$(CONFIG_DVDSUB_PARSER)           += dvdsub_parser.o
OBJS-$(CONFIG_FLAC_PARSER)             += flac_parser.o flacdata.o flac.o \
                                          vorbis_data.o
OBJS-$(CONFIG_GSM_PARSER)              += gsm_parser.o
OBJS-$(CONFIG_H261_PARSER)             += h261_parser.o
OBJS-$(CONFIG_H263_PARSER)             += h263_parser.o
OBJS-$(CONFIG_H264_PARSER)             += h264_parser.o h264.o            \
                                          cabac.o                         \
                                          h264_refs.o h264_sei.o h264_direct.o \
                                          h264_loopfilter.o h264_cabac.o \
                                          h264_cavlc.o h264_ps.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_AAC_LATM_PARSER)         += latm_parser.o
OBJS-$(CONFIG_MJPEG_PARSER)            += mjpeg_parser.o
OBJS-$(CONFIG_MLP_PARSER)              += mlp_parser.o mlp.o
OBJS-$(CONFIG_MPEG4VIDEO_PARSER)       += mpeg4video_parser.o h263.o \
                                          mpegvideo.o error_resilience.o \
                                          mpeg4videodec.o mpeg4video.o \
                                          ituh263dec.o h263dec.o
OBJS-$(CONFIG_PNG_PARSER)              += png_parser.o
OBJS-$(CONFIG_MPEGAUDIO_PARSER)        += mpegaudio_parser.o \
                                          mpegaudiodecheader.o mpegaudiodata.o
OBJS-$(CONFIG_MPEGVIDEO_PARSER)        += mpegvideo_parser.o    \
                                          mpeg12.o mpeg12data.o \
                                          mpegvideo.o error_resilience.o
OBJS-$(CONFIG_PNM_PARSER)              += pnm_parser.o pnm.o
OBJS-$(CONFIG_RV30_PARSER)             += rv34_parser.o
OBJS-$(CONFIG_RV40_PARSER)             += rv34_parser.o
OBJS-$(CONFIG_VC1_PARSER)              += vc1_parser.o vc1.o vc1data.o \
                                          msmpeg4.o msmpeg4data.o mpeg4video.o \
                                          h263.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_VORBIS_PARSER)           += vorbis_parser.o xiph.o
OBJS-$(CONFIG_VP3_PARSER)              += vp3_parser.o
OBJS-$(CONFIG_VP8_PARSER)              += vp8_parser.o

# bitstream filters
OBJS-$(CONFIG_AAC_ADTSTOASC_BSF)          += aac_adtstoasc_bsf.o aacadtsdec.o \
                                             mpeg4audio.o
OBJS-$(CONFIG_CHOMP_BSF)                  += chomp_bsf.o
OBJS-$(CONFIG_DUMP_EXTRADATA_BSF)         += dump_extradata_bsf.o
OBJS-$(CONFIG_H264_MP4TOANNEXB_BSF)       += h264_mp4toannexb_bsf.o
OBJS-$(CONFIG_IMX_DUMP_HEADER_BSF)        += imx_dump_header_bsf.o
OBJS-$(CONFIG_MJPEG2JPEG_BSF)             += mjpeg2jpeg_bsf.o mjpeg.o
OBJS-$(CONFIG_MJPEGA_DUMP_HEADER_BSF)     += mjpega_dump_header_bsf.o
OBJS-$(CONFIG_MOV2TEXTSUB_BSF)            += movsub_bsf.o
OBJS-$(CONFIG_MP3_HEADER_COMPRESS_BSF)    += mp3_header_compress_bsf.o
OBJS-$(CONFIG_MP3_HEADER_DECOMPRESS_BSF)  += mp3_header_decompress_bsf.o \
                                             mpegaudiodata.o
OBJS-$(CONFIG_NOISE_BSF)                  += noise_bsf.o
OBJS-$(CONFIG_REMOVE_EXTRADATA_BSF)       += remove_extradata_bsf.o
OBJS-$(CONFIG_TEXT2MOVSUB_BSF)            += movsub_bsf.o

# thread libraries
OBJS-$(HAVE_PTHREADS)                  += pthread.o
OBJS-$(HAVE_W32THREADS)                += pthread.o
OBJS-$(HAVE_OS2THREADS)                += pthread.o

# inverse.o contains the ff_inverse table definition, which is used by
# the FASTDIV macro (from libavutil); since referencing the external
# table has a negative effect on performance, copy it in libavcodec as
# well.
OBJS-$(!CONFIG_SMALL)                  += inverse.o

SKIPHEADERS                            += %_tablegen.h                  \
                                          %_tables.h                    \
                                          aac_tablegen_decl.h           \
                                          fft-internal.h                \
                                          tableprint.h                  \
                                          $(ARCH)/vp56_arith.h          \

SKIPHEADERS-$(CONFIG_DXVA2)            += dxva2.h dxva2_internal.h
SKIPHEADERS-$(CONFIG_LIBSCHROEDINGER)  += libschroedinger.h
SKIPHEADERS-$(CONFIG_MPEG_XVMC_DECODER) += xvmc.h
SKIPHEADERS-$(CONFIG_VAAPI)            += vaapi_internal.h
SKIPHEADERS-$(CONFIG_VDA)              += vda_internal.h
SKIPHEADERS-$(CONFIG_VDPAU)            += vdpau.h
SKIPHEADERS-$(HAVE_W32THREADS)         += w32pthreads.h

TESTPROGS = cabac                                                       \
            dct                                                         \
            fft                                                         \
            fft-fixed                                                   \
            golomb                                                      \
            iirfilter                                                   \
            rangecoder                                                  \
            snowenc                                                     \

TESTPROGS-$(HAVE_MMX) += motion
TESTOBJS = dctref.o

HOSTPROGS = aac_tablegen                                                \
            aacps_tablegen                                              \
            cbrt_tablegen                                               \
            cos_tablegen                                                \
            dv_tablegen                                                 \
            motionpixels_tablegen                                       \
            mpegaudio_tablegen                                          \
            pcm_tablegen                                                \
            qdm2_tablegen                                               \
            sinewin_tablegen                                            \

CLEANFILES = *_tables.c *_tables.h *_tablegen$(HOSTEXESUF)

$(SUBDIR)dct-test$(EXESUF): $(SUBDIR)dctref.o

TRIG_TABLES  = cos cos_fixed sin
TRIG_TABLES := $(TRIG_TABLES:%=$(SUBDIR)%_tables.c)

$(TRIG_TABLES): $(SUBDIR)%_tables.c: $(SUBDIR)cos_tablegen$(HOSTEXESUF)
	$(M)./$< $* > $@

ifdef CONFIG_SMALL
$(SUBDIR)%_tablegen$(HOSTEXESUF): HOSTCFLAGS += -DCONFIG_SMALL=1
else
$(SUBDIR)%_tablegen$(HOSTEXESUF): HOSTCFLAGS += -DCONFIG_SMALL=0
endif

GEN_HEADERS = cbrt_tables.h aacps_tables.h aac_tables.h dv_tables.h     \
              sinewin_tables.h mpegaudio_tables.h motionpixels_tables.h \
              pcm_tables.h qdm2_tables.h
GEN_HEADERS := $(addprefix $(SUBDIR), $(GEN_HEADERS))

$(GEN_HEADERS): $(SUBDIR)%_tables.h: $(SUBDIR)%_tablegen$(HOSTEXESUF)
	$(M)./$< > $@

ifdef CONFIG_HARDCODED_TABLES
$(SUBDIR)aacdec.o: $(SUBDIR)cbrt_tables.h
$(SUBDIR)aacps.o: $(SUBDIR)aacps_tables.h
$(SUBDIR)aactab.o: $(SUBDIR)aac_tables.h
$(SUBDIR)dv.o: $(SUBDIR)dv_tables.h
$(SUBDIR)sinewin.o: $(SUBDIR)sinewin_tables.h
$(SUBDIR)mpegaudiodec.o: $(SUBDIR)mpegaudio_tables.h
$(SUBDIR)mpegaudiodec_float.o: $(SUBDIR)mpegaudio_tables.h
$(SUBDIR)motionpixels.o: $(SUBDIR)motionpixels_tables.h
$(SUBDIR)pcm.o: $(SUBDIR)pcm_tables.h
$(SUBDIR)qdm2.o: $(SUBDIR)qdm2_tables.h
endif

CODEC_NAMES_SH := $(SRC_PATH)/$(SUBDIR)codec_names.sh
AVCODEC_H      := $(SRC_PATH)/$(SUBDIR)avcodec.h
$(SUBDIR)codec_names.h: $(CODEC_NAMES_SH) config.h $(AVCODEC_H)
	$(CC) $(CPPFLAGS) $(CFLAGS) -E $(AVCODEC_H) | \
	$(CODEC_NAMES_SH) config.h $@
$(SUBDIR)utils.o: $(SUBDIR)codec_names.h
