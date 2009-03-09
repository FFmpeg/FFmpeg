include $(SUBDIR)../config.mak

NAME = avcodec
FFLIBS = avutil

HEADERS = avcodec.h opt.h vdpau.h xvmc.h

OBJS = allcodecs.o                                                      \
       audioconvert.o                                                   \
       bitstream.o                                                      \
       bitstream_filter.o                                               \
       dsputil.o                                                        \
       eval.o                                                           \
       faanidct.o                                                       \
       imgconvert.o                                                     \
       jrevdct.o                                                        \
       opt.o                                                            \
       options.o                                                        \
       parser.o                                                         \
       raw.o                                                            \
       resample.o                                                       \
       resample2.o                                                      \
       simple_idct.o                                                    \
       utils.o                                                          \

# parts needed for many different codecs
OBJS-$(CONFIG_AANDCT)                  += aandcttab.o
OBJS-$(CONFIG_ENCODERS)                += faandct.o jfdctfst.o jfdctint.o
OBJS-$(CONFIG_FFT)                     += fft.o
OBJS-$(CONFIG_GOLOMB)                  += golomb.o
OBJS-$(CONFIG_MDCT)                    += mdct.o
OBJS-$(CONFIG_RDFT)                    += rdft.o

# decoders/encoders
OBJS-$(CONFIG_AAC_DECODER)             += aac.o aactab.o mpeg4audio.o aac_parser.o aac_ac3_parser.o
OBJS-$(CONFIG_AASC_DECODER)            += aasc.o msrledec.o
OBJS-$(CONFIG_AC3_DECODER)             += eac3dec.o ac3dec.o ac3tab.o ac3dec_data.o ac3.o
OBJS-$(CONFIG_AC3_ENCODER)             += ac3enc.o ac3tab.o ac3.o
OBJS-$(CONFIG_ALAC_DECODER)            += alac.o
OBJS-$(CONFIG_ALAC_ENCODER)            += alacenc.o lpc.o
OBJS-$(CONFIG_AMV_DECODER)             += sp5xdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_APE_DECODER)             += apedec.o
OBJS-$(CONFIG_ASV1_DECODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV1_ENCODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV2_DECODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ASV2_ENCODER)            += asv1.o mpeg12data.o
OBJS-$(CONFIG_ATRAC3_DECODER)          += atrac3.o
OBJS-$(CONFIG_AVS_DECODER)             += avs.o
OBJS-$(CONFIG_BETHSOFTVID_DECODER)     += bethsoftvideo.o
OBJS-$(CONFIG_BFI_DECODER)             += bfi.o
OBJS-$(CONFIG_BMP_DECODER)             += bmp.o msrledec.o
OBJS-$(CONFIG_BMP_ENCODER)             += bmpenc.o
OBJS-$(CONFIG_C93_DECODER)             += c93.o
OBJS-$(CONFIG_CAVS_DECODER)            += cavs.o cavsdec.o cavsdsp.o mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_CINEPAK_DECODER)         += cinepak.o
OBJS-$(CONFIG_CLJR_DECODER)            += cljr.o
OBJS-$(CONFIG_CLJR_ENCODER)            += cljr.o
OBJS-$(CONFIG_COOK_DECODER)            += cook.o
OBJS-$(CONFIG_CSCD_DECODER)            += cscd.o
OBJS-$(CONFIG_CYUV_DECODER)            += cyuv.o
OBJS-$(CONFIG_DCA_DECODER)             += dca.o
OBJS-$(CONFIG_DNXHD_DECODER)           += dnxhddec.o dnxhddata.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += dnxhdenc.o dnxhddata.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_DSICINAUDIO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DSICINVIDEO_DECODER)     += dsicinav.o
OBJS-$(CONFIG_DVBSUB_DECODER)          += dvbsubdec.o
OBJS-$(CONFIG_DVBSUB_ENCODER)          += dvbsub.o
OBJS-$(CONFIG_DVDSUB_DECODER)          += dvdsubdec.o
OBJS-$(CONFIG_DVDSUB_ENCODER)          += dvdsubenc.o
OBJS-$(CONFIG_DVVIDEO_DECODER)         += dv.o
OBJS-$(CONFIG_DVVIDEO_ENCODER)         += dv.o
OBJS-$(CONFIG_DXA_DECODER)             += dxa.o
OBJS-$(CONFIG_EAC3_DECODER)            += eac3dec.o ac3dec.o ac3tab.o ac3dec_data.o ac3.o
OBJS-$(CONFIG_EACMV_DECODER)           += eacmv.o
OBJS-$(CONFIG_EATGQ_DECODER)           += eatgq.o eaidct.o
OBJS-$(CONFIG_EATGV_DECODER)           += eatgv.o
OBJS-$(CONFIG_EATQI_DECODER)           += eatqi.o eaidct.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_EIGHTBPS_DECODER)        += 8bps.o
OBJS-$(CONFIG_EIGHTSVX_EXP_DECODER)    += 8svx.o
OBJS-$(CONFIG_EIGHTSVX_FIB_DECODER)    += 8svx.o
OBJS-$(CONFIG_ESCAPE124_DECODER)       += escape124.o
OBJS-$(CONFIG_FFV1_DECODER)            += ffv1.o rangecoder.o
OBJS-$(CONFIG_FFV1_ENCODER)            += ffv1.o rangecoder.o
OBJS-$(CONFIG_FFVHUFF_DECODER)         += huffyuv.o
OBJS-$(CONFIG_FFVHUFF_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_FLAC_DECODER)            += flacdec.o
OBJS-$(CONFIG_FLAC_ENCODER)            += flacenc.o lpc.o
OBJS-$(CONFIG_FLASHSV_DECODER)         += flashsv.o
OBJS-$(CONFIG_FLASHSV_ENCODER)         += flashsvenc.o
OBJS-$(CONFIG_FLIC_DECODER)            += flicvideo.o
OBJS-$(CONFIG_FLV_DECODER)             += h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_FLV_ENCODER)             += mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_FOURXM_DECODER)          += 4xm.o
OBJS-$(CONFIG_FRAPS_DECODER)           += fraps.o huffman.o
OBJS-$(CONFIG_GIF_DECODER)             += gifdec.o lzw.o
OBJS-$(CONFIG_GIF_ENCODER)             += gif.o
OBJS-$(CONFIG_H261_DECODER)            += h261dec.o h261.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H261_ENCODER)            += h261enc.o h261.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_H263_DECODER)            += h263dec.o h263.o h263_parser.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H263I_DECODER)           += h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H263_ENCODER)            += mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H263P_ENCODER)           += mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H264_DECODER)            += h264.o h264idct.o h264pred.o h264_parser.o cabac.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_H264_ENCODER)            += h264enc.o h264dspenc.o
OBJS-$(CONFIG_H264_VDPAU_DECODER)      += vdpau.o h264.o h264idct.o h264pred.o h264_parser.o cabac.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_HUFFYUV_DECODER)         += huffyuv.o
OBJS-$(CONFIG_HUFFYUV_ENCODER)         += huffyuv.o
OBJS-$(CONFIG_IDCIN_DECODER)           += idcinvideo.o
OBJS-$(CONFIG_IMC_DECODER)             += imc.o
OBJS-$(CONFIG_INDEO2_DECODER)          += indeo2.o
OBJS-$(CONFIG_INDEO3_DECODER)          += indeo3.o
OBJS-$(CONFIG_INTERPLAY_DPCM_DECODER)  += dpcm.o
OBJS-$(CONFIG_INTERPLAY_VIDEO_DECODER) += interplayvideo.o
OBJS-$(CONFIG_JPEGLS_DECODER)          += jpeglsdec.o jpegls.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_JPEGLS_ENCODER)          += jpeglsenc.o jpegls.o
OBJS-$(CONFIG_KMVC_DECODER)            += kmvc.o
OBJS-$(CONFIG_LJPEG_ENCODER)           += ljpegenc.o mjpegenc.o mjpeg.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_LOCO_DECODER)            += loco.o
OBJS-$(CONFIG_MACE3_DECODER)           += mace.o
OBJS-$(CONFIG_MACE6_DECODER)           += mace.o
OBJS-$(CONFIG_MDEC_DECODER)            += mdec.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MIMIC_DECODER)           += mimic.o
OBJS-$(CONFIG_MJPEG_DECODER)           += mjpegdec.o mjpeg.o
OBJS-$(CONFIG_MJPEG_ENCODER)           += mjpegenc.o mjpeg.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12data.o mpegvideo.o
OBJS-$(CONFIG_MJPEGB_DECODER)          += mjpegbdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_MLP_DECODER)             += mlpdec.o mlp_parser.o mlp.o
OBJS-$(CONFIG_MMVIDEO_DECODER)         += mmvideo.o
OBJS-$(CONFIG_MOTIONPIXELS_DECODER)    += motionpixels.o
OBJS-$(CONFIG_MP1_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP2_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP2_ENCODER)             += mpegaudioenc.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3ADU_DECODER)          += mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MP3ON4_DECODER)          += mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o mpeg4audio.o
OBJS-$(CONFIG_MP3_DECODER)             += mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MPC7_DECODER)            += mpc7.o mpc.o mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MPC8_DECODER)            += mpc8.o mpc.o mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_MPEG_VDPAU_DECODER)      += vdpau.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG1_VDPAU_DECODER)     += vdpau.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG_XVMC_DECODER)       += mpegvideo_xvmc.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEGVIDEO_DECODER)       += mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG1VIDEO_DECODER)      += mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG1VIDEO_ENCODER)      += mpeg12enc.o mpeg12data.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG2VIDEO_DECODER)      += mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG2VIDEO_ENCODER)      += mpeg12enc.o mpeg12data.o mpegvideo_enc.o motion_est.o ratecontrol.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG4_DECODER)           += h263dec.o h263.o mpeg4video_parser.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEG4_ENCODER)           += mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V1_DECODER)       += msmpeg4.o msmpeg4data.o h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V1_ENCODER)       += msmpeg4.o msmpeg4data.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V2_DECODER)       += msmpeg4.o msmpeg4data.o h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V2_ENCODER)       += msmpeg4.o msmpeg4data.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V3_DECODER)       += msmpeg4.o msmpeg4data.o h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSMPEG4V3_ENCODER)       += msmpeg4.o msmpeg4data.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MSRLE_DECODER)           += msrle.o msrledec.o
OBJS-$(CONFIG_MSVIDEO1_DECODER)        += msvideo1.o
OBJS-$(CONFIG_MSZH_DECODER)            += lcldec.o
OBJS-$(CONFIG_NELLYMOSER_DECODER)      += nellymoserdec.o nellymoser.o
OBJS-$(CONFIG_NELLYMOSER_ENCODER)      += nellymoserenc.o nellymoser.o
OBJS-$(CONFIG_NUV_DECODER)             += nuv.o rtjpeg.o
OBJS-$(CONFIG_PAM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PBM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PCX_DECODER)             += pcx.o
OBJS-$(CONFIG_PGM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PGMYUV_ENCODER)          += pnmenc.o pnm.o
OBJS-$(CONFIG_PNG_DECODER)             += png.o pngdec.o
OBJS-$(CONFIG_PNG_ENCODER)             += png.o pngenc.o
OBJS-$(CONFIG_PPM_ENCODER)             += pnmenc.o pnm.o
OBJS-$(CONFIG_PTX_DECODER)             += ptx.o
OBJS-$(CONFIG_QCELP_DECODER)           += qcelpdec.o qcelp_lsp.o celp_math.o celp_filters.o acelp_vectors.o
OBJS-$(CONFIG_QDM2_DECODER)            += qdm2.o mpegaudiodec.o mpegaudiodecheader.o mpegaudio.o mpegaudiodata.o
OBJS-$(CONFIG_QDRAW_DECODER)           += qdrw.o
OBJS-$(CONFIG_QPEG_DECODER)            += qpeg.o
OBJS-$(CONFIG_QTRLE_DECODER)           += qtrle.o
OBJS-$(CONFIG_QTRLE_ENCODER)           += qtrleenc.o
OBJS-$(CONFIG_RA_144_DECODER)          += ra144.o celp_filters.o
OBJS-$(CONFIG_RA_288_DECODER)          += ra288.o celp_math.o celp_filters.o
OBJS-$(CONFIG_RAWVIDEO_DECODER)        += rawdec.o
OBJS-$(CONFIG_RAWVIDEO_ENCODER)        += rawenc.o
OBJS-$(CONFIG_RL2_DECODER)             += rl2.o
OBJS-$(CONFIG_ROQ_DECODER)             += roqvideodec.o roqvideo.o
OBJS-$(CONFIG_ROQ_ENCODER)             += roqvideoenc.o roqvideo.o elbg.o
OBJS-$(CONFIG_ROQ_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_ROQ_DPCM_ENCODER)        += roqaudioenc.o
OBJS-$(CONFIG_RPZA_DECODER)            += rpza.o
OBJS-$(CONFIG_RV10_DECODER)            += rv10.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV10_ENCODER)            += rv10.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV20_DECODER)            += rv10.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV20_ENCODER)            += rv10.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV30_DECODER)            += rv30.o rv34.o h264pred.o rv30dsp.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_RV40_DECODER)            += rv40.o rv34.o h264pred.o rv40dsp.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_SGI_DECODER)             += sgidec.o
OBJS-$(CONFIG_SGI_ENCODER)             += sgienc.o rle.o
OBJS-$(CONFIG_SHORTEN_DECODER)         += shorten.o
OBJS-$(CONFIG_SMACKAUD_DECODER)        += smacker.o
OBJS-$(CONFIG_SMACKER_DECODER)         += smacker.o
OBJS-$(CONFIG_SMC_DECODER)             += smc.o
OBJS-$(CONFIG_SNOW_DECODER)            += snow.o rangecoder.o
OBJS-$(CONFIG_SNOW_ENCODER)            += snow.o rangecoder.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_SOL_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_SONIC_DECODER)           += sonic.o
OBJS-$(CONFIG_SONIC_ENCODER)           += sonic.o
OBJS-$(CONFIG_SONIC_LS_ENCODER)        += sonic.o
OBJS-$(CONFIG_SP5X_DECODER)            += sp5xdec.o mjpegdec.o mjpeg.o
OBJS-$(CONFIG_SUNRAST_DECODER)         += sunrast.o
OBJS-$(CONFIG_SVQ1_DECODER)            += svq1dec.o svq1.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += svq1enc.o svq1.o motion_est.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_SVQ3_DECODER)            += h264.o h264idct.o h264pred.o h264_parser.o cabac.o mpegvideo.o error_resilience.o svq1dec.o svq1.o h263.o
OBJS-$(CONFIG_TARGA_DECODER)           += targa.o
OBJS-$(CONFIG_TARGA_ENCODER)           += targaenc.o rle.o
OBJS-$(CONFIG_THEORA_DECODER)          += vp3.o xiph.o vp3dsp.o
OBJS-$(CONFIG_THP_DECODER)             += mjpegdec.o mjpeg.o
OBJS-$(CONFIG_TIERTEXSEQVIDEO_DECODER) += tiertexseqv.o
OBJS-$(CONFIG_TIFF_DECODER)            += tiff.o lzw.o faxcompr.o
OBJS-$(CONFIG_TIFF_ENCODER)            += tiffenc.o rle.o lzwenc.o
OBJS-$(CONFIG_TRUEMOTION1_DECODER)     += truemotion1.o
OBJS-$(CONFIG_TRUEMOTION2_DECODER)     += truemotion2.o
OBJS-$(CONFIG_TRUESPEECH_DECODER)      += truespeech.o
OBJS-$(CONFIG_TSCC_DECODER)            += tscc.o msrledec.o
OBJS-$(CONFIG_TTA_DECODER)             += tta.o
OBJS-$(CONFIG_TXD_DECODER)             += txd.o s3tc.o
OBJS-$(CONFIG_ULTI_DECODER)            += ulti.o
OBJS-$(CONFIG_VB_DECODER)              += vb.o
OBJS-$(CONFIG_VC1_DECODER)             += vc1.o vc1data.o vc1dsp.o msmpeg4data.o h263dec.o h263.o intrax8.o intrax8dsp.o error_resilience.o mpegvideo.o msmpeg4.o
OBJS-$(CONFIG_VC1_VDPAU_DECODER)       += vdpau.o vc1.o vc1data.o vc1dsp.o msmpeg4data.o h263dec.o h263.o intrax8.o intrax8dsp.o error_resilience.o mpegvideo.o msmpeg4.o
OBJS-$(CONFIG_VCR1_DECODER)            += vcr1.o
OBJS-$(CONFIG_VCR1_ENCODER)            += vcr1.o
OBJS-$(CONFIG_VMDAUDIO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMDVIDEO_DECODER)        += vmdav.o
OBJS-$(CONFIG_VMNC_DECODER)            += vmnc.o
OBJS-$(CONFIG_VORBIS_DECODER)          += vorbis_dec.o vorbis.o vorbis_data.o xiph.o
OBJS-$(CONFIG_VORBIS_ENCODER)          += vorbis_enc.o vorbis.o vorbis_data.o
OBJS-$(CONFIG_VP3_DECODER)             += vp3.o vp3dsp.o
OBJS-$(CONFIG_VP5_DECODER)             += vp5.o vp56.o vp56data.o vp3dsp.o
OBJS-$(CONFIG_VP6_DECODER)             += vp6.o vp56.o vp56data.o vp3dsp.o vp6dsp.o huffman.o
OBJS-$(CONFIG_VP6A_DECODER)            += vp6.o vp56.o vp56data.o vp3dsp.o vp6dsp.o huffman.o
OBJS-$(CONFIG_VP6F_DECODER)            += vp6.o vp56.o vp56data.o vp3dsp.o vp6dsp.o huffman.o
OBJS-$(CONFIG_VQA_DECODER)             += vqavideo.o
OBJS-$(CONFIG_WAVPACK_DECODER)         += wavpack.o
OBJS-$(CONFIG_WMAV1_DECODER)           += wmadec.o wma.o
OBJS-$(CONFIG_WMAV1_ENCODER)           += wmaenc.o wma.o
OBJS-$(CONFIG_WMAV2_DECODER)           += wmadec.o wma.o
OBJS-$(CONFIG_WMAV2_ENCODER)           += wmaenc.o wma.o
OBJS-$(CONFIG_WMV1_DECODER)            += h263dec.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_WMV1_ENCODER)            += mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_WMV2_DECODER)            += wmv2dec.o wmv2.o msmpeg4.o msmpeg4data.o h263dec.o h263.o intrax8.o intrax8dsp.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_WMV2_ENCODER)            += wmv2enc.o wmv2.o msmpeg4.o msmpeg4data.o mpegvideo_enc.o motion_est.o ratecontrol.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_WMV3_DECODER)            += vc1.o vc1data.o vc1dsp.o msmpeg4data.o h263dec.o h263.o intrax8.o intrax8dsp.o error_resilience.o mpegvideo.o msmpeg4.o
OBJS-$(CONFIG_WMV3_VDPAU_DECODER)      += vdpau.o vc1.o vc1data.o vc1dsp.o msmpeg4data.o h263dec.o h263.o intrax8.o intrax8dsp.o error_resilience.o mpegvideo.o msmpeg4.o
OBJS-$(CONFIG_WNV1_DECODER)            += wnv1.o
OBJS-$(CONFIG_WS_SND1_DECODER)         += ws-snd1.o
OBJS-$(CONFIG_XAN_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_XAN_WC3_DECODER)         += xan.o
OBJS-$(CONFIG_XAN_WC4_DECODER)         += xan.o
OBJS-$(CONFIG_XL_DECODER)              += xl.o
OBJS-$(CONFIG_XSUB_DECODER)            += xsubdec.o
OBJS-$(CONFIG_ZLIB_DECODER)            += lcldec.o
OBJS-$(CONFIG_ZLIB_ENCODER)            += lclenc.o
OBJS-$(CONFIG_ZMBV_DECODER)            += zmbv.o
OBJS-$(CONFIG_ZMBV_ENCODER)            += zmbvenc.o

# (AD)PCM decoders/encoders
OBJS-$(CONFIG_PCM_ALAW_DECODER)           += pcm.o
OBJS-$(CONFIG_PCM_ALAW_ENCODER)           += pcm.o
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
OBJS-$(CONFIG_PCM_MULAW_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_MULAW_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S8_DECODER)             += pcm.o
OBJS-$(CONFIG_PCM_S8_ENCODER)             += pcm.o
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
OBJS-$(CONFIG_PCM_ZORK_ENCODER)           += pcm.o

OBJS-$(CONFIG_ADPCM_4XM_DECODER)          += adpcm.o
OBJS-$(CONFIG_ADPCM_ADX_DECODER)          += adxdec.o
OBJS-$(CONFIG_ADPCM_ADX_ENCODER)          += adxenc.o
OBJS-$(CONFIG_ADPCM_CT_DECODER)           += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_DECODER)           += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_MAXIS_XA_DECODER)  += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_R1_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_R2_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_R3_DECODER)        += adpcm.o
OBJS-$(CONFIG_ADPCM_EA_XAS_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_G726_DECODER)         += g726.o
OBJS-$(CONFIG_ADPCM_G726_ENCODER)         += g726.o
OBJS-$(CONFIG_ADPCM_IMA_AMV_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_DK3_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_DK4_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_EA_EACS_DECODER)  += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_EA_SEAD_DECODER)  += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_ISS_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_QT_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_QT_ENCODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_SMJPEG_DECODER)   += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_ENCODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_IMA_WS_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_MS_DECODER)           += adpcm.o
OBJS-$(CONFIG_ADPCM_MS_ENCODER)           += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_2_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_3_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_SBPRO_4_DECODER)      += adpcm.o
OBJS-$(CONFIG_ADPCM_SWF_DECODER)          += adpcm.o
OBJS-$(CONFIG_ADPCM_SWF_ENCODER)          += adpcm.o
OBJS-$(CONFIG_ADPCM_THP_DECODER)          += adpcm.o
OBJS-$(CONFIG_ADPCM_XA_DECODER)           += adpcm.o
OBJS-$(CONFIG_ADPCM_YAMAHA_DECODER)       += adpcm.o
OBJS-$(CONFIG_ADPCM_YAMAHA_ENCODER)       += adpcm.o

# libavformat dependencies
OBJS-$(CONFIG_EAC3_DEMUXER)            += ac3_parser.o ac3tab.o aac_ac3_parser.o
OBJS-$(CONFIG_FLAC_DEMUXER)            += flacdec.o
OBJS-$(CONFIG_FLAC_MUXER)              += flacdec.o
OBJS-$(CONFIG_GXF_DEMUXER)             += mpeg12data.o
OBJS-$(CONFIG_MATROSKA_AUDIO_MUXER)    += xiph.o mpeg4audio.o flacdec.o
OBJS-$(CONFIG_MATROSKA_DEMUXER)        += mpeg4audio.o
OBJS-$(CONFIG_MATROSKA_MUXER)          += xiph.o mpeg4audio.o flacdec.o
OBJS-$(CONFIG_MOV_DEMUXER)             += mpeg4audio.o mpegaudiodata.o
OBJS-$(CONFIG_MPEGTS_MUXER)            += mpegvideo.o
OBJS-$(CONFIG_NUT_MUXER)               += mpegaudiodata.o
OBJS-$(CONFIG_OGG_DEMUXER)             += flacdec.o
OBJS-$(CONFIG_OGG_MUXER)               += xiph.o flacdec.o
OBJS-$(CONFIG_RTP_MUXER)               += mpegvideo.o

# external codec libraries
OBJS-$(CONFIG_LIBAMR_NB)               += libamr.o
OBJS-$(CONFIG_LIBAMR_WB)               += libamr.o
OBJS-$(CONFIG_LIBDIRAC_DECODER)        += libdiracdec.o
OBJS-$(CONFIG_LIBDIRAC_ENCODER)        += libdiracenc.o libdirac_libschro.o
OBJS-$(CONFIG_LIBFAAC)                 += libfaac.o
OBJS-$(CONFIG_LIBFAAD)                 += libfaad.o
OBJS-$(CONFIG_LIBGSM)                  += libgsm.o
OBJS-$(CONFIG_LIBMP3LAME)              += libmp3lame.o
OBJS-$(CONFIG_LIBOPENJPEG)             += libopenjpeg.o
OBJS-$(CONFIG_LIBSCHROEDINGER_DECODER) += libschroedingerdec.o libschroedinger.o libdirac_libschro.o
OBJS-$(CONFIG_LIBSCHROEDINGER_ENCODER) += libschroedingerenc.o libschroedinger.o libdirac_libschro.o
OBJS-$(CONFIG_LIBSPEEX)                += libspeexdec.o
OBJS-$(CONFIG_LIBTHEORA)               += libtheoraenc.o
OBJS-$(CONFIG_LIBVORBIS)               += libvorbis.o
OBJS-$(CONFIG_LIBX264)                 += libx264.o
OBJS-$(CONFIG_LIBXVID)                 += libxvidff.o libxvid_rc.o

# parsers
OBJS-$(CONFIG_AAC_PARSER)              += aac_parser.o aac_ac3_parser.o mpeg4audio.o
OBJS-$(CONFIG_AC3_PARSER)              += ac3_parser.o ac3tab.o aac_ac3_parser.o
OBJS-$(CONFIG_CAVSVIDEO_PARSER)        += cavs_parser.o
OBJS-$(CONFIG_DCA_PARSER)              += dca_parser.o
OBJS-$(CONFIG_DIRAC_PARSER)            += dirac_parser.o
OBJS-$(CONFIG_DNXHD_PARSER)            += dnxhd_parser.o
OBJS-$(CONFIG_DVBSUB_PARSER)           += dvbsub_parser.o
OBJS-$(CONFIG_DVDSUB_PARSER)           += dvdsub_parser.o
OBJS-$(CONFIG_H261_PARSER)             += h261_parser.o
OBJS-$(CONFIG_H263_PARSER)             += h263_parser.o
OBJS-$(CONFIG_H264_PARSER)             += h264_parser.o
OBJS-$(CONFIG_MJPEG_PARSER)            += mjpeg_parser.o
OBJS-$(CONFIG_MLP_PARSER)              += mlp_parser.o mlp.o
OBJS-$(CONFIG_MPEG4VIDEO_PARSER)       += mpeg4video_parser.o h263.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_MPEGAUDIO_PARSER)        += mpegaudio_parser.o mpegaudiodecheader.o mpegaudiodata.o
OBJS-$(CONFIG_MPEGVIDEO_PARSER)        += mpegvideo_parser.o mpeg12.o mpeg12data.o mpegvideo.o error_resilience.o
OBJS-$(CONFIG_PNM_PARSER)              += pnm_parser.o pnm.o
OBJS-$(CONFIG_VC1_PARSER)              += vc1_parser.o
OBJS-$(CONFIG_VP3_PARSER)              += vp3_parser.o

# bitstream filters
OBJS-$(CONFIG_DUMP_EXTRADATA_BSF)         += dump_extradata_bsf.o
OBJS-$(CONFIG_H264_MP4TOANNEXB_BSF)       += h264_mp4toannexb_bsf.o
OBJS-$(CONFIG_IMX_DUMP_HEADER_BSF)        += imx_dump_header_bsf.o
OBJS-$(CONFIG_MJPEGA_DUMP_HEADER_BSF)     += mjpega_dump_header_bsf.o
OBJS-$(CONFIG_MOV2TEXTSUB_BSF)            += movsub_bsf.o
OBJS-$(CONFIG_MP3_HEADER_COMPRESS_BSF)    += mp3_header_compress_bsf.o
OBJS-$(CONFIG_MP3_HEADER_DECOMPRESS_BSF)  += mp3_header_decompress_bsf.o mpegaudiodata.o
OBJS-$(CONFIG_NOISE_BSF)                  += noise_bsf.o
OBJS-$(CONFIG_REMOVE_EXTRADATA_BSF)       += remove_extradata_bsf.o
OBJS-$(CONFIG_TEXT2MOVSUB_BSF)            += movsub_bsf.o

# thread libraries
OBJS-$(HAVE_BEOSTHREADS)               += beosthread.o
OBJS-$(HAVE_OS2THREADS)                += os2thread.o
OBJS-$(HAVE_PTHREADS)                  += pthread.o
OBJS-$(HAVE_W32THREADS)                += w32thread.o

# processor-specific code
YASM-OBJS-FFT-$(HAVE_AMD3DNOW)         += x86/fft_3dn.o
YASM-OBJS-FFT-$(HAVE_AMD3DNOWEXT)      += x86/fft_3dn2.o
YASM-OBJS-FFT-$(HAVE_SSE)              += x86/fft_sse.o
YASM-OBJS-$(CONFIG_FFT)                += x86/fft_mmx.o $(YASM-OBJS-FFT-yes)
YASM-OBJS-$(CONFIG_GPL)                += x86/h264_deblock_sse2.o       \
                                          x86/h264_idct_sse2.o          \

MMX-OBJS-$(CONFIG_CAVS_DECODER)        += x86/cavsdsp_mmx.o
MMX-OBJS-$(CONFIG_ENCODERS)            += x86/dsputilenc_mmx.o
MMX-OBJS-$(CONFIG_FLAC_ENCODER)        += x86/flacdsp_mmx.o
MMX-OBJS-$(CONFIG_GPL)                 += x86/idct_mmx.o
MMX-OBJS-$(CONFIG_SNOW_DECODER)        += x86/snowdsp_mmx.o
MMX-OBJS-$(CONFIG_THEORA_DECODER)      += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o
MMX-OBJS-$(CONFIG_VC1_DECODER)         += x86/vc1dsp_mmx.o
MMX-OBJS-$(CONFIG_VP3_DECODER)         += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o
MMX-OBJS-$(CONFIG_VP5_DECODER)         += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o
MMX-OBJS-$(CONFIG_VP6_DECODER)         += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o \
                                          x86/vp6dsp_mmx.o x86/vp6dsp_sse2.o
MMX-OBJS-$(CONFIG_VP6A_DECODER)        += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o \
                                          x86/vp6dsp_mmx.o x86/vp6dsp_sse2.o
MMX-OBJS-$(CONFIG_VP6F_DECODER)        += x86/vp3dsp_mmx.o x86/vp3dsp_sse2.o \
                                          x86/vp6dsp_mmx.o x86/vp6dsp_sse2.o
MMX-OBJS-$(CONFIG_WMV3_DECODER)        += x86/vc1dsp_mmx.o
MMX-OBJS-$(HAVE_YASM)                  += x86/dsputil_yasm.o            \
                                          $(YASM-OBJS-yes)

OBJS-$(HAVE_MMX)                       += x86/cpuid.o                   \
                                          x86/dnxhd_mmx.o               \
                                          x86/dsputil_mmx.o             \
                                          x86/fdct_mmx.o                \
                                          x86/idct_mmx_xvid.o           \
                                          x86/idct_sse2_xvid.o          \
                                          x86/motion_est_mmx.o          \
                                          x86/mpegvideo_mmx.o           \
                                          x86/simple_idct_mmx.o         \
                                          $(MMX-OBJS-yes)

OBJS-$(ARCH_ALPHA)                     += alpha/dsputil_alpha.o         \
                                          alpha/dsputil_alpha_asm.o     \
                                          alpha/motion_est_alpha.o      \
                                          alpha/motion_est_mvi_asm.o    \
                                          alpha/mpegvideo_alpha.o       \
                                          alpha/simple_idct_alpha.o     \

OBJS-$(ARCH_ARM)                       += arm/dsputil_arm.o             \
                                          arm/dsputil_arm_s.o           \
                                          arm/jrevdct_arm.o             \
                                          arm/mpegvideo_arm.o           \
                                          arm/simple_idct_arm.o         \

OBJS-$(HAVE_ARMV5TE)                   += arm/mpegvideo_armv5te.o       \
                                          arm/mpegvideo_armv5te_s.o     \
                                          arm/simple_idct_armv5te.o     \

OBJS-$(HAVE_ARMV6)                     += arm/simple_idct_armv6.o       \

OBJS-$(HAVE_ARMVFP)                    += arm/dsputil_vfp.o             \
                                          arm/float_arm_vfp.o           \

OBJS-$(HAVE_IWMMXT)                    += arm/dsputil_iwmmxt.o          \
                                          arm/mpegvideo_iwmmxt.o        \

OBJS-$(HAVE_NEON)                      += arm/dsputil_neon.o            \
                                          arm/dsputil_neon_s.o          \
                                          arm/h264dsp_neon.o            \
                                          arm/h264idct_neon.o           \
                                          arm/simple_idct_neon.o        \

OBJS-$(ARCH_BFIN)                      += bfin/dsputil_bfin.o           \
                                          bfin/fdct_bfin.o              \
                                          bfin/idct_bfin.o              \
                                          bfin/mpegvideo_bfin.o         \
                                          bfin/pixels_bfin.o            \
                                          bfin/vp3_bfin.o               \
                                          bfin/vp3_idct_bfin.o          \

OBJS-$(ARCH_PPC)                       += ppc/dsputil_ppc.o             \

ALTIVEC-OBJS-$(CONFIG_H264_DECODER)    += ppc/h264_altivec.o
ALTIVEC-OBJS-$(CONFIG_SNOW_DECODER)    += ppc/snow_altivec.o
ALTIVEC-OBJS-$(CONFIG_VC1_DECODER)     += ppc/vc1dsp_altivec.o
ALTIVEC-OBJS-$(CONFIG_WMV3_DECODER)    += ppc/vc1dsp_altivec.o

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

OBJS-$(ARCH_SH4)                       += sh4/dsputil_align.o           \
                                          sh4/dsputil_sh4.o             \
                                          sh4/idct_sh4.o                \

OBJS-$(CONFIG_MLIB)                    += mlib/dsputil_mlib.o           \

OBJS-$(HAVE_MMI)                       += ps2/dsputil_mmi.o             \
                                          ps2/idct_mmi.o                \
                                          ps2/mpegvideo_mmi.o           \

OBJS-$(HAVE_VIS)                       += sparc/dsputil_vis.o           \
                                          sparc/simple_idct_vis.o       \


TESTS = $(addsuffix -test$(EXESUF), cabac dct eval fft h264 rangecoder snow)
TESTS-$(ARCH_X86) += x86/cpuid-test$(EXESUF) motion-test$(EXESUF)

CLEANFILES = apiexample$(EXESUF)
DIRS = alpha arm bfin mlib ppc ps2 sh4 sparc x86

include $(SUBDIR)../subdir.mak

$(SUBDIR)dct-test$(EXESUF): $(SUBDIR)fdctref.o $(SUBDIR)aandcttab.o
$(SUBDIR)fft-test$(EXESUF): $(SUBDIR)fdctref.o
