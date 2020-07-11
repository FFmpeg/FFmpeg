NAME = avcodec
DESC = FFmpeg codec library

HEADERS = ac3_parser.h                                                  \
          adts_parser.h                                                 \
          avcodec.h                                                     \
          avdct.h                                                       \
          avfft.h                                                       \
          bsf.h                                                         \
          codec.h                                                       \
          codec_desc.h                                                  \
          codec_id.h                                                    \
          codec_par.h                                                   \
          d3d11va.h                                                     \
          dirac.h                                                       \
          dv_profile.h                                                  \
          dxva2.h                                                       \
          jni.h                                                         \
          mediacodec.h                                                  \
          packet.h                                                      \
          qsv.h                                                         \
          vaapi.h                                                       \
          vdpau.h                                                       \
          version.h                                                     \
          videotoolbox.h                                                \
          vorbis_parser.h                                               \
          xvmc.h                                                        \

OBJS = ac3_parser.o                                                     \
       adts_parser.o                                                    \
       allcodecs.o                                                      \
       avdct.o                                                          \
       avpacket.o                                                       \
       avpicture.o                                                      \
       bitstream.o                                                      \
       bitstream_filter.o                                               \
       bitstream_filters.o                                              \
       bsf.o                                                            \
       codec_desc.o                                                     \
       d3d11va.o                                                        \
       decode.o                                                         \
       dirac.o                                                          \
       dv_profile.o                                                     \
       encode.o                                                         \
       imgconvert.o                                                     \
       jni.o                                                            \
       mathtables.o                                                     \
       mediacodec.o                                                     \
       mpeg12framerate.o                                                \
       options.o                                                        \
       mjpegenc_huffman.o                                               \
       parser.o                                                         \
       parsers.o                                                        \
       profiles.o                                                       \
       qsv_api.o                                                        \
       raw.o                                                            \
       utils.o                                                          \
       vorbis_parser.o                                                  \
       xiph.o                                                           \

# subsystems
OBJS-$(CONFIG_AANDCTTABLES)            += aandcttab.o
OBJS-$(CONFIG_AC3DSP)                  += ac3dsp.o ac3.o ac3tab.o
OBJS-$(CONFIG_ADTS_HEADER)             += adts_header.o mpeg4audio.o
OBJS-$(CONFIG_AMF)                     += amfenc.o
OBJS-$(CONFIG_AUDIO_FRAME_QUEUE)       += audio_frame_queue.o
OBJS-$(CONFIG_AUDIODSP)                += audiodsp.o
OBJS-$(CONFIG_BLOCKDSP)                += blockdsp.o
OBJS-$(CONFIG_BSWAPDSP)                += bswapdsp.o
OBJS-$(CONFIG_CABAC)                   += cabac.o
OBJS-$(CONFIG_CBS)                     += cbs.o
OBJS-$(CONFIG_CBS_AV1)                 += cbs_av1.o
OBJS-$(CONFIG_CBS_H264)                += cbs_h2645.o h2645_parse.o
OBJS-$(CONFIG_CBS_H265)                += cbs_h2645.o h2645_parse.o
OBJS-$(CONFIG_CBS_JPEG)                += cbs_jpeg.o
OBJS-$(CONFIG_CBS_MPEG2)               += cbs_mpeg2.o
OBJS-$(CONFIG_CBS_VP9)                 += cbs_vp9.o
OBJS-$(CONFIG_CRYSTALHD)               += crystalhd.o
OBJS-$(CONFIG_DCT)                     += dct.o dct32_fixed.o dct32_float.o
OBJS-$(CONFIG_ERROR_RESILIENCE)        += error_resilience.o
OBJS-$(CONFIG_EXIF)                    += exif.o tiff_common.o
OBJS-$(CONFIG_FAANDCT)                 += faandct.o
OBJS-$(CONFIG_FAANIDCT)                += faanidct.o
OBJS-$(CONFIG_FDCTDSP)                 += fdctdsp.o jfdctfst.o jfdctint.o
FFT-OBJS-$(CONFIG_HARDCODED_TABLES)    += cos_tables.o cos_fixed_tables.o
OBJS-$(CONFIG_FFT)                     += avfft.o fft_fixed.o fft_float.o \
                                          fft_fixed_32.o fft_init_table.o \
                                          $(FFT-OBJS-yes)
OBJS-$(CONFIG_FLACDSP)                 += flacdsp.o
OBJS-$(CONFIG_FMTCONVERT)              += fmtconvert.o
OBJS-$(CONFIG_GOLOMB)                  += golomb.o
OBJS-$(CONFIG_H263DSP)                 += h263dsp.o
OBJS-$(CONFIG_H264CHROMA)              += h264chroma.o
OBJS-$(CONFIG_H264DSP)                 += h264dsp.o h264idct.o
OBJS-$(CONFIG_H264PARSE)               += h264_parse.o h2645_parse.o h264_ps.o
OBJS-$(CONFIG_H264PRED)                += h264pred.o
OBJS-$(CONFIG_H264QPEL)                += h264qpel.o
OBJS-$(CONFIG_HEVCPARSE)               += hevc_parse.o h2645_parse.o hevc_ps.o hevc_sei.o hevc_data.o
OBJS-$(CONFIG_HPELDSP)                 += hpeldsp.o
OBJS-$(CONFIG_HUFFMAN)                 += huffman.o
OBJS-$(CONFIG_HUFFYUVDSP)              += huffyuvdsp.o
OBJS-$(CONFIG_HUFFYUVENCDSP)           += huffyuvencdsp.o
OBJS-$(CONFIG_IDCTDSP)                 += idctdsp.o simple_idct.o jrevdct.o
OBJS-$(CONFIG_IIRFILTER)               += iirfilter.o
OBJS-$(CONFIG_MDCT15)                  += mdct15.o
OBJS-$(CONFIG_INTRAX8)                 += intrax8.o intrax8dsp.o msmpeg4data.o
OBJS-$(CONFIG_IVIDSP)                  += ivi_dsp.o
OBJS-$(CONFIG_JNI)                     += ffjni.o jni.o
OBJS-$(CONFIG_JPEGTABLES)              += jpegtables.o
OBJS-$(CONFIG_LLAUDDSP)                += lossless_audiodsp.o
OBJS-$(CONFIG_LLVIDDSP)                += lossless_videodsp.o
OBJS-$(CONFIG_LLVIDENCDSP)             += lossless_videoencdsp.o
OBJS-$(CONFIG_LPC)                     += lpc.o
OBJS-$(CONFIG_LSP)                     += lsp.o
OBJS-$(CONFIG_LZF)                     += lzf.o
OBJS-$(CONFIG_MDCT)                    += mdct_fixed.o mdct_float.o mdct_fixed_32.o
OBJS-$(CONFIG_ME_CMP)                  += me_cmp.o
OBJS-$(CONFIG_MEDIACODEC)              += mediacodecdec_common.o mediacodec_surface.o mediacodec_wrapper.o mediacodec_sw_buffer.o
OBJS-$(CONFIG_MPEG_ER)                 += mpeg_er.o
OBJS-$(CONFIG_MPEGAUDIO)               += mpegaudio.o
OBJS-$(CONFIG_MPEGAUDIODSP)            += mpegaudiodsp.o                \
                                          mpegaudiodsp_data.o           \
                                          mpegaudiodsp_fixed.o          \
                                          mpegaudiodsp_float.o
OBJS-$(CONFIG_MPEGAUDIOHEADER)         += mpegaudiodecheader.o mpegaudiodata.o
OBJS-$(CONFIG_MPEGVIDEO)               += mpegvideo.o mpegvideodsp.o rl.o \
                                          mpegvideo_motion.o mpegutils.o \
                                          mpegvideodata.o mpegpicture.o
OBJS-$(CONFIG_MPEGVIDEOENC)            += mpegvideo_enc.o mpeg12data.o  \
                                          motion_est.o ratecontrol.o    \
                                          mpegvideoencdsp.o
OBJS-$(CONFIG_MSS34DSP)                += mss34dsp.o
OBJS-$(CONFIG_NVENC)                   += nvenc.o
OBJS-$(CONFIG_PIXBLOCKDSP)             += pixblockdsp.o
OBJS-$(CONFIG_QPELDSP)                 += qpeldsp.o
OBJS-$(CONFIG_QSV)                     += qsv.o
OBJS-$(CONFIG_QSVDEC)                  += qsvdec.o
OBJS-$(CONFIG_QSVENC)                  += qsvenc.o
OBJS-$(CONFIG_RANGECODER)              += rangecoder.o
OBJS-$(CONFIG_RDFT)                    += rdft.o
OBJS-$(CONFIG_RV34DSP)                 += rv34dsp.o
OBJS-$(CONFIG_SHARED)                  += log2_tab.o reverse.o
OBJS-$(CONFIG_SINEWIN)                 += sinewin.o sinewin_fixed.o
OBJS-$(CONFIG_SNAPPY)                  += snappy.o
OBJS-$(CONFIG_STARTCODE)               += startcode.o
OBJS-$(CONFIG_TEXTUREDSP)              += texturedsp.o
OBJS-$(CONFIG_TEXTUREDSPENC)           += texturedspenc.o
OBJS-$(CONFIG_TPELDSP)                 += tpeldsp.o
OBJS-$(CONFIG_VAAPI_ENCODE)            += vaapi_encode.o
OBJS-$(CONFIG_VC1DSP)                  += vc1dsp.o
OBJS-$(CONFIG_VIDEODSP)                += videodsp.o
OBJS-$(CONFIG_VP3DSP)                  += vp3dsp.o
OBJS-$(CONFIG_VP56DSP)                 += vp56dsp.o
OBJS-$(CONFIG_VP8DSP)                  += vp8dsp.o
OBJS-$(CONFIG_V4L2_M2M)                += v4l2_m2m.o v4l2_context.o v4l2_buffers.o v4l2_fmt.o
OBJS-$(CONFIG_WMA_FREQS)               += wma_freqs.o
OBJS-$(CONFIG_WMV2DSP)                 += wmv2dsp.o

# decoders/encoders
OBJS-$(CONFIG_ZERO12V_DECODER)         += 012v.o
OBJS-$(CONFIG_A64MULTI_ENCODER)        += a64multienc.o elbg.o
OBJS-$(CONFIG_A64MULTI5_ENCODER)       += a64multienc.o elbg.o
OBJS-$(CONFIG_AAC_DECODER)             += aacdec.o aactab.o aacsbr.o aacps_float.o \
                                          mpeg4audio.o kbdwin.o \
                                          sbrdsp.o aacpsdsp_float.o cbrt_data.o
OBJS-$(CONFIG_AAC_FIXED_DECODER)       += aacdec_fixed.o aactab.o aacsbr_fixed.o aacps_fixed.o \
                                          mpeg4audio.o kbdwin.o \
                                          sbrdsp_fixed.o aacpsdsp_fixed.o cbrt_data_fixed.o
OBJS-$(CONFIG_AAC_ENCODER)             += aacenc.o aaccoder.o aacenctab.o    \
                                          aacpsy.o aactab.o      \
                                          aacenc_is.o \
                                          aacenc_tns.o \
                                          aacenc_ltp.o \
                                          aacenc_pred.o \
                                          psymodel.o mpeg4audio.o kbdwin.o cbrt_data.o
OBJS-$(CONFIG_AAC_MF_ENCODER)          += mfenc.o mf_utils.o
OBJS-$(CONFIG_AASC_DECODER)            += aasc.o msrledec.o
OBJS-$(CONFIG_AC3_DECODER)             += ac3dec_float.o ac3dec_data.o ac3.o kbdwin.o ac3tab.o
OBJS-$(CONFIG_AC3_FIXED_DECODER)       += ac3dec_fixed.o ac3dec_data.o ac3.o kbdwin.o ac3tab.o
OBJS-$(CONFIG_AC3_ENCODER)             += ac3enc_float.o ac3enc.o ac3tab.o \
                                          ac3.o kbdwin.o
OBJS-$(CONFIG_AC3_FIXED_ENCODER)       += ac3enc_fixed.o ac3enc.o ac3tab.o ac3.o
OBJS-$(CONFIG_AC3_MF_ENCODER)          += mfenc.o mf_utils.o
OBJS-$(CONFIG_ACELP_KELVIN_DECODER)    += g729dec.o lsp.o celp_math.o celp_filters.o acelp_filters.o acelp_pitch_delay.o acelp_vectors.o g729postfilter.o
OBJS-$(CONFIG_AGM_DECODER)             += agm.o
OBJS-$(CONFIG_AIC_DECODER)             += aic.o
OBJS-$(CONFIG_ALAC_DECODER)            += alac.o alac_data.o alacdsp.o
OBJS-$(CONFIG_ALAC_ENCODER)            += alacenc.o alac_data.o
OBJS-$(CONFIG_ALIAS_PIX_DECODER)       += aliaspixdec.o
OBJS-$(CONFIG_ALIAS_PIX_ENCODER)       += aliaspixenc.o
OBJS-$(CONFIG_ALS_DECODER)             += alsdec.o bgmc.o mlz.o mpeg4audio.o
OBJS-$(CONFIG_AMRNB_DECODER)           += amrnbdec.o celp_filters.o   \
                                          celp_math.o acelp_filters.o \
                                          acelp_vectors.o             \
                                          acelp_pitch_delay.o
OBJS-$(CONFIG_AMRWB_DECODER)           += amrwbdec.o celp_filters.o   \
                                          celp_math.o acelp_filters.o \
                                          acelp_vectors.o             \
                                          acelp_pitch_delay.o
OBJS-$(CONFIG_AMV_ENCODER)             += mjpegenc.o mjpegenc_common.o \
                                          mjpegenc_huffman.o
OBJS-$(CONFIG_ANM_DECODER)             += anm.o
OBJS-$(CONFIG_ANSI_DECODER)            += ansi.o cga_data.o
OBJS-$(CONFIG_APE_DECODER)             += apedec.o
OBJS-$(CONFIG_APTX_DECODER)            += aptxdec.o aptx.o
OBJS-$(CONFIG_APTX_ENCODER)            += aptxenc.o aptx.o
OBJS-$(CONFIG_APTX_HD_DECODER)         += aptxdec.o aptx.o
OBJS-$(CONFIG_APTX_HD_ENCODER)         += aptxenc.o aptx.o
OBJS-$(CONFIG_APNG_DECODER)            += png.o pngdec.o pngdsp.o
OBJS-$(CONFIG_APNG_ENCODER)            += png.o pngenc.o
OBJS-$(CONFIG_ARBC_DECODER)            += arbc.o
OBJS-$(CONFIG_SSA_DECODER)             += assdec.o ass.o
OBJS-$(CONFIG_SSA_ENCODER)             += assenc.o ass.o
OBJS-$(CONFIG_ASS_DECODER)             += assdec.o ass.o
OBJS-$(CONFIG_ASS_ENCODER)             += assenc.o ass.o
OBJS-$(CONFIG_ASV1_DECODER)            += asvdec.o asv.o mpeg12data.o
OBJS-$(CONFIG_ASV1_ENCODER)            += asvenc.o asv.o mpeg12data.o
OBJS-$(CONFIG_ASV2_DECODER)            += asvdec.o asv.o mpeg12data.o
OBJS-$(CONFIG_ASV2_ENCODER)            += asvenc.o asv.o mpeg12data.o
OBJS-$(CONFIG_ATRAC1_DECODER)          += atrac1.o atrac.o
OBJS-$(CONFIG_ATRAC3_DECODER)          += atrac3.o atrac.o
OBJS-$(CONFIG_ATRAC3AL_DECODER)        += atrac3.o atrac.o
OBJS-$(CONFIG_ATRAC3P_DECODER)         += atrac3plusdec.o atrac3plus.o \
                                          atrac3plusdsp.o atrac.o
OBJS-$(CONFIG_ATRAC3PAL_DECODER)       += atrac3plusdec.o atrac3plus.o \
                                          atrac3plusdsp.o atrac.o
OBJS-$(CONFIG_ATRAC9_DECODER)          += atrac9dec.o
OBJS-$(CONFIG_AURA_DECODER)            += cyuv.o
OBJS-$(CONFIG_AURA2_DECODER)           += aura.o
OBJS-$(CONFIG_AVRN_DECODER)            += avrndec.o mjpegdec.o
OBJS-$(CONFIG_AVRP_DECODER)            += r210dec.o
OBJS-$(CONFIG_AVRP_ENCODER)            += r210enc.o
OBJS-$(CONFIG_AVS_DECODER)             += avs.o
OBJS-$(CONFIG_AVUI_DECODER)            += avuidec.o
OBJS-$(CONFIG_AVUI_ENCODER)            += avuienc.o
OBJS-$(CONFIG_AYUV_DECODER)            += v408dec.o
OBJS-$(CONFIG_AYUV_ENCODER)            += v408enc.o
OBJS-$(CONFIG_BETHSOFTVID_DECODER)     += bethsoftvideo.o
OBJS-$(CONFIG_BFI_DECODER)             += bfi.o
OBJS-$(CONFIG_BINK_DECODER)            += bink.o binkdsp.o
OBJS-$(CONFIG_BINKAUDIO_DCT_DECODER)   += binkaudio.o
OBJS-$(CONFIG_BINKAUDIO_RDFT_DECODER)  += binkaudio.o
OBJS-$(CONFIG_BINTEXT_DECODER)         += bintext.o cga_data.o
OBJS-$(CONFIG_BITPACKED_DECODER)       += bitpacked.o
OBJS-$(CONFIG_BMP_DECODER)             += bmp.o msrledec.o
OBJS-$(CONFIG_BMP_ENCODER)             += bmpenc.o
OBJS-$(CONFIG_BMV_AUDIO_DECODER)       += bmvaudio.o
OBJS-$(CONFIG_BMV_VIDEO_DECODER)       += bmvvideo.o
OBJS-$(CONFIG_BRENDER_PIX_DECODER)     += brenderpix.o
OBJS-$(CONFIG_C93_DECODER)             += c93.o
OBJS-$(CONFIG_CAVS_DECODER)            += cavs.o cavsdec.o cavsdsp.o \
                                          cavsdata.o
OBJS-$(CONFIG_CCAPTION_DECODER)        += ccaption_dec.o ass.o
OBJS-$(CONFIG_CDGRAPHICS_DECODER)      += cdgraphics.o
OBJS-$(CONFIG_CDTOONS_DECODER)         += cdtoons.o
OBJS-$(CONFIG_CDXL_DECODER)            += cdxl.o
OBJS-$(CONFIG_CFHD_DECODER)            += cfhd.o cfhddata.o
OBJS-$(CONFIG_CINEPAK_DECODER)         += cinepak.o
OBJS-$(CONFIG_CINEPAK_ENCODER)         += cinepakenc.o elbg.o
OBJS-$(CONFIG_CLEARVIDEO_DECODER)      += clearvideo.o
OBJS-$(CONFIG_CLJR_DECODER)            += cljrdec.o
OBJS-$(CONFIG_CLJR_ENCODER)            += cljrenc.o
OBJS-$(CONFIG_CLLC_DECODER)            += cllc.o canopus.o
OBJS-$(CONFIG_COMFORTNOISE_DECODER)    += cngdec.o celp_filters.o
OBJS-$(CONFIG_COMFORTNOISE_ENCODER)    += cngenc.o
OBJS-$(CONFIG_COOK_DECODER)            += cook.o
OBJS-$(CONFIG_CPIA_DECODER)            += cpia.o
OBJS-$(CONFIG_CSCD_DECODER)            += cscd.o
OBJS-$(CONFIG_CYUV_DECODER)            += cyuv.o
OBJS-$(CONFIG_DCA_DECODER)             += dcadec.o dca.o dcadata.o dcahuff.o \
                                          dca_core.o dca_exss.o dca_xll.o dca_lbr.o \
                                          dcadsp.o dcadct.o synth_filter.o
OBJS-$(CONFIG_DCA_ENCODER)             += dcaenc.o dca.o dcadata.o dcahuff.o \
                                          dcaadpcm.o
OBJS-$(CONFIG_DDS_DECODER)             += dds.o
OBJS-$(CONFIG_DERF_DPCM_DECODER)       += dpcm.o
OBJS-$(CONFIG_DIRAC_DECODER)           += diracdec.o dirac.o diracdsp.o diractab.o \
                                          dirac_arith.o dirac_dwt.o dirac_vlc.o
OBJS-$(CONFIG_DFA_DECODER)             += dfa.o
OBJS-$(CONFIG_DNXHD_DECODER)           += dnxhddec.o dnxhddata.o
OBJS-$(CONFIG_DNXHD_ENCODER)           += dnxhdenc.o dnxhddata.o
OBJS-$(CONFIG_DOLBY_E_DECODER)         += dolby_e.o kbdwin.o
OBJS-$(CONFIG_DPX_DECODER)             += dpx.o
OBJS-$(CONFIG_DPX_ENCODER)             += dpxenc.o
OBJS-$(CONFIG_DSD_LSBF_DECODER)        += dsddec.o dsd.o
OBJS-$(CONFIG_DSD_MSBF_DECODER)        += dsddec.o dsd.o
OBJS-$(CONFIG_DSD_LSBF_PLANAR_DECODER) += dsddec.o dsd.o
OBJS-$(CONFIG_DSD_MSBF_PLANAR_DECODER) += dsddec.o dsd.o
OBJS-$(CONFIG_DSICINAUDIO_DECODER)     += dsicinaudio.o
OBJS-$(CONFIG_DSICINVIDEO_DECODER)     += dsicinvideo.o
OBJS-$(CONFIG_DSS_SP_DECODER)          += dss_sp.o
OBJS-$(CONFIG_DST_DECODER)             += dstdec.o dsd.o
OBJS-$(CONFIG_DVBSUB_DECODER)          += dvbsubdec.o
OBJS-$(CONFIG_DVBSUB_ENCODER)          += dvbsub.o
OBJS-$(CONFIG_DVDSUB_DECODER)          += dvdsubdec.o dvdsub.o
OBJS-$(CONFIG_DVDSUB_ENCODER)          += dvdsubenc.o dvdsub.o
OBJS-$(CONFIG_DVAUDIO_DECODER)         += dvaudiodec.o
OBJS-$(CONFIG_DVVIDEO_DECODER)         += dvdec.o dv.o dvdata.o
OBJS-$(CONFIG_DVVIDEO_ENCODER)         += dvenc.o dv.o dvdata.o
OBJS-$(CONFIG_DXA_DECODER)             += dxa.o
OBJS-$(CONFIG_DXTORY_DECODER)          += dxtory.o
OBJS-$(CONFIG_DXV_DECODER)             += dxv.o
OBJS-$(CONFIG_EAC3_DECODER)            += eac3_data.o
OBJS-$(CONFIG_EAC3_ENCODER)            += eac3enc.o eac3_data.o
OBJS-$(CONFIG_EACMV_DECODER)           += eacmv.o
OBJS-$(CONFIG_EAMAD_DECODER)           += eamad.o eaidct.o mpeg12.o \
                                          mpeg12data.o
OBJS-$(CONFIG_EATGQ_DECODER)           += eatgq.o eaidct.o
OBJS-$(CONFIG_EATGV_DECODER)           += eatgv.o
OBJS-$(CONFIG_EATQI_DECODER)           += eatqi.o eaidct.o mpeg12.o mpeg12data.o mpegvideodata.o rl.o
OBJS-$(CONFIG_EIGHTBPS_DECODER)        += 8bps.o
OBJS-$(CONFIG_EIGHTSVX_EXP_DECODER)    += 8svx.o
OBJS-$(CONFIG_EIGHTSVX_FIB_DECODER)    += 8svx.o
OBJS-$(CONFIG_ESCAPE124_DECODER)       += escape124.o
OBJS-$(CONFIG_ESCAPE130_DECODER)       += escape130.o
OBJS-$(CONFIG_EVRC_DECODER)            += evrcdec.o acelp_vectors.o lsp.o
OBJS-$(CONFIG_EXR_DECODER)             += exr.o exrdsp.o
OBJS-$(CONFIG_FFV1_DECODER)            += ffv1dec.o ffv1.o
OBJS-$(CONFIG_FFV1_ENCODER)            += ffv1enc.o ffv1.o
OBJS-$(CONFIG_FFWAVESYNTH_DECODER)     += ffwavesynth.o
OBJS-$(CONFIG_FIC_DECODER)             += fic.o
OBJS-$(CONFIG_FITS_DECODER)            += fitsdec.o fits.o
OBJS-$(CONFIG_FITS_ENCODER)            += fitsenc.o
OBJS-$(CONFIG_FLAC_DECODER)            += flacdec.o flacdata.o flac.o
OBJS-$(CONFIG_FLAC_ENCODER)            += flacenc.o flacdata.o flac.o vorbis_data.o
OBJS-$(CONFIG_FLASHSV_DECODER)         += flashsv.o
OBJS-$(CONFIG_FLASHSV_ENCODER)         += flashsvenc.o
OBJS-$(CONFIG_FLASHSV2_ENCODER)        += flashsv2enc.o
OBJS-$(CONFIG_FLASHSV2_DECODER)        += flashsv.o
OBJS-$(CONFIG_FLIC_DECODER)            += flicvideo.o
OBJS-$(CONFIG_FMVC_DECODER)            += fmvc.o
OBJS-$(CONFIG_FOURXM_DECODER)          += 4xm.o
OBJS-$(CONFIG_FRAPS_DECODER)           += fraps.o
OBJS-$(CONFIG_FRWU_DECODER)            += frwu.o
OBJS-$(CONFIG_G2M_DECODER)             += g2meet.o elsdec.o
OBJS-$(CONFIG_G723_1_DECODER)          += g723_1dec.o g723_1.o \
                                          acelp_vectors.o celp_filters.o celp_math.o
OBJS-$(CONFIG_G723_1_ENCODER)          += g723_1enc.o g723_1.o \
                                          acelp_vectors.o celp_filters.o celp_math.o
OBJS-$(CONFIG_G729_DECODER)            += g729dec.o lsp.o celp_math.o celp_filters.o acelp_filters.o acelp_pitch_delay.o acelp_vectors.o g729postfilter.o
OBJS-$(CONFIG_GDV_DECODER)             += gdv.o
OBJS-$(CONFIG_GIF_DECODER)             += gifdec.o lzw.o
OBJS-$(CONFIG_GIF_ENCODER)             += gif.o lzwenc.o
OBJS-$(CONFIG_GREMLIN_DPCM_DECODER)    += dpcm.o
OBJS-$(CONFIG_GSM_DECODER)             += gsmdec.o gsmdec_data.o msgsmdec.o
OBJS-$(CONFIG_GSM_MS_DECODER)          += gsmdec.o gsmdec_data.o msgsmdec.o
OBJS-$(CONFIG_H261_DECODER)            += h261dec.o h261data.o h261.o
OBJS-$(CONFIG_H261_ENCODER)            += h261enc.o h261data.o h261.o
OBJS-$(CONFIG_H263_DECODER)            += h263dec.o h263.o ituh263dec.o        \
                                          mpeg4video.o mpeg4videodec.o flvdec.o\
                                          intelh263dec.o h263data.o
OBJS-$(CONFIG_H263_ENCODER)            += mpeg4videoenc.o mpeg4video.o  \
                                          h263.o ituh263enc.o flvenc.o h263data.o
OBJS-$(CONFIG_H263_V4L2M2M_DECODER)    += v4l2_m2m_dec.o
OBJS-$(CONFIG_H263_V4L2M2M_ENCODER)    += v4l2_m2m_enc.o
OBJS-$(CONFIG_H264_DECODER)            += h264dec.o h264_cabac.o h264_cavlc.o \
                                          h264_direct.o h264_loopfilter.o  \
                                          h264_mb.o h264_picture.o \
                                          h264_refs.o h264_sei.o \
                                          h264_slice.o h264data.o
OBJS-$(CONFIG_H264_AMF_ENCODER)        += amfenc_h264.o
OBJS-$(CONFIG_H264_CUVID_DECODER)      += cuviddec.o
OBJS-$(CONFIG_H264_MEDIACODEC_DECODER) += mediacodecdec.o
OBJS-$(CONFIG_H264_MF_ENCODER)         += mfenc.o mf_utils.o
OBJS-$(CONFIG_H264_MMAL_DECODER)       += mmaldec.o
OBJS-$(CONFIG_H264_NVENC_ENCODER)      += nvenc_h264.o
OBJS-$(CONFIG_NVENC_ENCODER)           += nvenc_h264.o
OBJS-$(CONFIG_NVENC_H264_ENCODER)      += nvenc_h264.o
OBJS-$(CONFIG_H264_OMX_ENCODER)        += omx.o
OBJS-$(CONFIG_H264_QSV_DECODER)        += qsvdec_h2645.o
OBJS-$(CONFIG_H264_QSV_ENCODER)        += qsvenc_h264.o
OBJS-$(CONFIG_H264_RKMPP_DECODER)      += rkmppdec.o
OBJS-$(CONFIG_H264_VAAPI_ENCODER)      += vaapi_encode_h264.o h264_levels.o
OBJS-$(CONFIG_H264_VIDEOTOOLBOX_ENCODER) += videotoolboxenc.o
OBJS-$(CONFIG_H264_V4L2M2M_DECODER)    += v4l2_m2m_dec.o
OBJS-$(CONFIG_H264_V4L2M2M_ENCODER)    += v4l2_m2m_enc.o
OBJS-$(CONFIG_HAP_DECODER)             += hapdec.o hap.o
OBJS-$(CONFIG_HAP_ENCODER)             += hapenc.o hap.o
OBJS-$(CONFIG_HCA_DECODER)             += hcadec.o
OBJS-$(CONFIG_HCOM_DECODER)            += hcom.o
OBJS-$(CONFIG_HEVC_DECODER)            += hevcdec.o hevc_mvs.o \
                                          hevc_cabac.o hevc_refs.o hevcpred.o    \
                                          hevcdsp.o hevc_filter.o hevc_data.o
OBJS-$(CONFIG_HEVC_AMF_ENCODER)        += amfenc_hevc.o
OBJS-$(CONFIG_HEVC_CUVID_DECODER)      += cuviddec.o
OBJS-$(CONFIG_HEVC_MEDIACODEC_DECODER) += mediacodecdec.o
OBJS-$(CONFIG_HEVC_MF_ENCODER)         += mfenc.o mf_utils.o
OBJS-$(CONFIG_HEVC_NVENC_ENCODER)      += nvenc_hevc.o
OBJS-$(CONFIG_NVENC_HEVC_ENCODER)      += nvenc_hevc.o
OBJS-$(CONFIG_HEVC_QSV_DECODER)        += qsvdec_h2645.o
OBJS-$(CONFIG_HEVC_QSV_ENCODER)        += qsvenc_hevc.o hevc_ps_enc.o       \
                                          hevc_data.o
OBJS-$(CONFIG_HEVC_RKMPP_DECODER)      += rkmppdec.o
OBJS-$(CONFIG_HEVC_VAAPI_ENCODER)      += vaapi_encode_h265.o h265_profile_level.o
OBJS-$(CONFIG_HEVC_V4L2M2M_DECODER)    += v4l2_m2m_dec.o
OBJS-$(CONFIG_HEVC_V4L2M2M_ENCODER)    += v4l2_m2m_enc.o
OBJS-$(CONFIG_HNM4_VIDEO_DECODER)      += hnm4video.o
OBJS-$(CONFIG_HQ_HQA_DECODER)          += hq_hqa.o hq_hqadata.o hq_hqadsp.o \
                                          canopus.o
OBJS-$(CONFIG_HQX_DECODER)             += hqx.o hqxvlc.o hqxdsp.o canopus.o
OBJS-$(CONFIG_HUFFYUV_DECODER)         += huffyuv.o huffyuvdec.o
OBJS-$(CONFIG_HUFFYUV_ENCODER)         += huffyuv.o huffyuvenc.o
OBJS-$(CONFIG_HYMT_DECODER)            += huffyuv.o huffyuvdec.o
OBJS-$(CONFIG_IDCIN_DECODER)           += idcinvideo.o
OBJS-$(CONFIG_IDF_DECODER)             += bintext.o cga_data.o
OBJS-$(CONFIG_IFF_ILBM_DECODER)        += iff.o
OBJS-$(CONFIG_ILBC_DECODER)            += ilbcdec.o
OBJS-$(CONFIG_IMC_DECODER)             += imc.o
OBJS-$(CONFIG_IMM4_DECODER)            += imm4.o
OBJS-$(CONFIG_IMM5_DECODER)            += imm5.o
OBJS-$(CONFIG_INDEO2_DECODER)          += indeo2.o
OBJS-$(CONFIG_INDEO3_DECODER)          += indeo3.o
OBJS-$(CONFIG_INDEO4_DECODER)          += indeo4.o ivi.o
OBJS-$(CONFIG_INDEO5_DECODER)          += indeo5.o ivi.o
OBJS-$(CONFIG_INTERPLAY_ACM_DECODER)   += interplayacm.o
OBJS-$(CONFIG_INTERPLAY_DPCM_DECODER)  += dpcm.o
OBJS-$(CONFIG_INTERPLAY_VIDEO_DECODER) += interplayvideo.o
OBJS-$(CONFIG_JACOSUB_DECODER)         += jacosubdec.o ass.o
OBJS-$(CONFIG_JPEG2000_ENCODER)        += j2kenc.o mqcenc.o mqc.o jpeg2000.o \
                                          jpeg2000dwt.o
OBJS-$(CONFIG_JPEG2000_DECODER)        += jpeg2000dec.o jpeg2000.o jpeg2000dsp.o \
                                          jpeg2000dwt.o mqcdec.o mqc.o
OBJS-$(CONFIG_JPEGLS_DECODER)          += jpeglsdec.o jpegls.o
OBJS-$(CONFIG_JPEGLS_ENCODER)          += jpeglsenc.o jpegls.o
OBJS-$(CONFIG_JV_DECODER)              += jvdec.o
OBJS-$(CONFIG_KGV1_DECODER)            += kgv1dec.o
OBJS-$(CONFIG_KMVC_DECODER)            += kmvc.o
OBJS-$(CONFIG_LAGARITH_DECODER)        += lagarith.o lagarithrac.o
OBJS-$(CONFIG_LJPEG_ENCODER)           += ljpegenc.o mjpegenc_common.o
OBJS-$(CONFIG_LOCO_DECODER)            += loco.o
OBJS-$(CONFIG_LSCR_DECODER)            += png.o pngdec.o pngdsp.o
OBJS-$(CONFIG_M101_DECODER)            += m101.o
OBJS-$(CONFIG_MACE3_DECODER)           += mace.o
OBJS-$(CONFIG_MACE6_DECODER)           += mace.o
OBJS-$(CONFIG_MAGICYUV_DECODER)        += magicyuv.o
OBJS-$(CONFIG_MAGICYUV_ENCODER)        += magicyuvenc.o
OBJS-$(CONFIG_MDEC_DECODER)            += mdec.o mpeg12.o mpeg12data.o
OBJS-$(CONFIG_METASOUND_DECODER)       += metasound.o metasound_data.o \
                                          twinvq.o
OBJS-$(CONFIG_MICRODVD_DECODER)        += microdvddec.o ass.o
OBJS-$(CONFIG_MIMIC_DECODER)           += mimic.o
OBJS-$(CONFIG_MJPEG_DECODER)           += mjpegdec.o
OBJS-$(CONFIG_MJPEG_QSV_DECODER)       += qsvdec_other.o
OBJS-$(CONFIG_MJPEG_ENCODER)           += mjpegenc.o mjpegenc_common.o \
                                          mjpegenc_huffman.o
OBJS-$(CONFIG_MJPEGB_DECODER)          += mjpegbdec.o
OBJS-$(CONFIG_MJPEG_CUVID_DECODER)     += cuviddec.o
OBJS-$(CONFIG_MJPEG_QSV_ENCODER)       += qsvenc_jpeg.o
OBJS-$(CONFIG_MJPEG_VAAPI_ENCODER)     += vaapi_encode_mjpeg.o
OBJS-$(CONFIG_MLP_DECODER)             += mlpdec.o mlpdsp.o
OBJS-$(CONFIG_MLP_ENCODER)             += mlpenc.o mlp.o
OBJS-$(CONFIG_MMVIDEO_DECODER)         += mmvideo.o
OBJS-$(CONFIG_MOTIONPIXELS_DECODER)    += motionpixels.o
OBJS-$(CONFIG_MOVTEXT_DECODER)         += movtextdec.o ass.o
OBJS-$(CONFIG_MOVTEXT_ENCODER)         += movtextenc.o ass_split.o
OBJS-$(CONFIG_MP1_DECODER)             += mpegaudiodec_fixed.o
OBJS-$(CONFIG_MP1FLOAT_DECODER)        += mpegaudiodec_float.o
OBJS-$(CONFIG_MP2_DECODER)             += mpegaudiodec_fixed.o
OBJS-$(CONFIG_MP2_ENCODER)             += mpegaudioenc_float.o mpegaudio.o \
                                          mpegaudiodata.o mpegaudiodsp_data.o
OBJS-$(CONFIG_MP2FIXED_ENCODER)        += mpegaudioenc_fixed.o mpegaudio.o \
                                          mpegaudiodata.o mpegaudiodsp_data.o
OBJS-$(CONFIG_MP2FLOAT_DECODER)        += mpegaudiodec_float.o
OBJS-$(CONFIG_MP3_DECODER)             += mpegaudiodec_fixed.o
OBJS-$(CONFIG_MP3_MF_ENCODER)          += mfenc.o mf_utils.o
OBJS-$(CONFIG_MP3ADU_DECODER)          += mpegaudiodec_fixed.o
OBJS-$(CONFIG_MP3ADUFLOAT_DECODER)     += mpegaudiodec_float.o
OBJS-$(CONFIG_MP3FLOAT_DECODER)        += mpegaudiodec_float.o
OBJS-$(CONFIG_MP3ON4_DECODER)          += mpegaudiodec_fixed.o mpeg4audio.o
OBJS-$(CONFIG_MP3ON4FLOAT_DECODER)     += mpegaudiodec_float.o mpeg4audio.o
OBJS-$(CONFIG_MPC7_DECODER)            += mpc7.o mpc.o
OBJS-$(CONFIG_MPC8_DECODER)            += mpc8.o mpc.o
OBJS-$(CONFIG_MPEGVIDEO_DECODER)       += mpeg12dec.o mpeg12.o mpeg12data.o
OBJS-$(CONFIG_MPEG1VIDEO_DECODER)      += mpeg12dec.o mpeg12.o mpeg12data.o
OBJS-$(CONFIG_MPEG1VIDEO_ENCODER)      += mpeg12enc.o mpeg12.o
OBJS-$(CONFIG_MPEG1_CUVID_DECODER)     += cuviddec.o
OBJS-$(CONFIG_MPEG1_V4L2M2M_DECODER)   += v4l2_m2m_dec.o
OBJS-$(CONFIG_MPEG2_MMAL_DECODER)      += mmaldec.o
OBJS-$(CONFIG_MPEG2_QSV_DECODER)       += qsvdec_other.o
OBJS-$(CONFIG_MPEG2_QSV_ENCODER)       += qsvenc_mpeg2.o
OBJS-$(CONFIG_MPEG2VIDEO_DECODER)      += mpeg12dec.o mpeg12.o mpeg12data.o
OBJS-$(CONFIG_MPEG2VIDEO_ENCODER)      += mpeg12enc.o mpeg12.o
OBJS-$(CONFIG_MPEG2_CUVID_DECODER)     += cuviddec.o
OBJS-$(CONFIG_MPEG2_MEDIACODEC_DECODER) += mediacodecdec.o
OBJS-$(CONFIG_MPEG2_VAAPI_ENCODER)     += vaapi_encode_mpeg2.o
OBJS-$(CONFIG_MPEG2_V4L2M2M_DECODER)   += v4l2_m2m_dec.o
OBJS-$(CONFIG_MPEG4_DECODER)           += xvididct.o
OBJS-$(CONFIG_MPEG4_CUVID_DECODER)     += cuviddec.o
OBJS-$(CONFIG_MPEG4_MEDIACODEC_DECODER) += mediacodecdec.o
OBJS-$(CONFIG_MPEG4_OMX_ENCODER)       += omx.o
OBJS-$(CONFIG_MPEG4_V4L2M2M_DECODER)   += v4l2_m2m_dec.o
OBJS-$(CONFIG_MPEG4_V4L2M2M_ENCODER)   += v4l2_m2m_enc.o
OBJS-$(CONFIG_MPL2_DECODER)            += mpl2dec.o ass.o
OBJS-$(CONFIG_MSA1_DECODER)            += mss3.o
OBJS-$(CONFIG_MSCC_DECODER)            += mscc.o
OBJS-$(CONFIG_MSMPEG4V1_DECODER)       += msmpeg4dec.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSMPEG4V2_DECODER)       += msmpeg4dec.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSMPEG4V2_ENCODER)       += msmpeg4enc.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSMPEG4V3_DECODER)       += msmpeg4dec.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSMPEG4V3_ENCODER)       += msmpeg4enc.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_MSRLE_DECODER)           += msrle.o msrledec.o
OBJS-$(CONFIG_MSS1_DECODER)            += mss1.o mss12.o
OBJS-$(CONFIG_MSS2_DECODER)            += mss2.o mss12.o mss2dsp.o wmv2data.o
OBJS-$(CONFIG_MSVIDEO1_DECODER)        += msvideo1.o
OBJS-$(CONFIG_MSVIDEO1_ENCODER)        += msvideo1enc.o elbg.o
OBJS-$(CONFIG_MSZH_DECODER)            += lcldec.o
OBJS-$(CONFIG_MTS2_DECODER)            += mss4.o
OBJS-$(CONFIG_MV30_DECODER)            += mv30.o
OBJS-$(CONFIG_MVC1_DECODER)            += mvcdec.o
OBJS-$(CONFIG_MVC2_DECODER)            += mvcdec.o
OBJS-$(CONFIG_MVDV_DECODER)            += midivid.o
OBJS-$(CONFIG_MVHA_DECODER)            += mvha.o
OBJS-$(CONFIG_MWSC_DECODER)            += mwsc.o
OBJS-$(CONFIG_MXPEG_DECODER)           += mxpegdec.o
OBJS-$(CONFIG_NELLYMOSER_DECODER)      += nellymoserdec.o nellymoser.o
OBJS-$(CONFIG_NELLYMOSER_ENCODER)      += nellymoserenc.o nellymoser.o
OBJS-$(CONFIG_NOTCHLC_DECODER)         += notchlc.o
OBJS-$(CONFIG_NUV_DECODER)             += nuv.o rtjpeg.o
OBJS-$(CONFIG_ON2AVC_DECODER)          += on2avc.o on2avcdata.o
OBJS-$(CONFIG_OPUS_DECODER)            += opusdec.o opus.o opus_celt.o opus_rc.o \
                                          opus_pvq.o opus_silk.o opustab.o vorbis_data.o \
                                          opusdsp.o
OBJS-$(CONFIG_OPUS_ENCODER)            += opusenc.o opus.o opus_rc.o opustab.o opus_pvq.o \
                                          opusenc_psy.o vorbis_data.o
OBJS-$(CONFIG_PAF_AUDIO_DECODER)       += pafaudio.o
OBJS-$(CONFIG_PAF_VIDEO_DECODER)       += pafvideo.o
OBJS-$(CONFIG_PAM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PAM_ENCODER)             += pamenc.o
OBJS-$(CONFIG_PBM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PBM_ENCODER)             += pnmenc.o
OBJS-$(CONFIG_PCX_DECODER)             += pcx.o
OBJS-$(CONFIG_PCX_ENCODER)             += pcxenc.o
OBJS-$(CONFIG_PFM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PGM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PGM_ENCODER)             += pnmenc.o
OBJS-$(CONFIG_PGMYUV_DECODER)          += pnmdec.o pnm.o
OBJS-$(CONFIG_PGMYUV_ENCODER)          += pnmenc.o
OBJS-$(CONFIG_PGSSUB_DECODER)          += pgssubdec.o
OBJS-$(CONFIG_PGX_DECODER)             += pgxdec.o
OBJS-$(CONFIG_PICTOR_DECODER)          += pictordec.o cga_data.o
OBJS-$(CONFIG_PIXLET_DECODER)          += pixlet.o
OBJS-$(CONFIG_PJS_DECODER)             += textdec.o ass.o
OBJS-$(CONFIG_PNG_DECODER)             += png.o pngdec.o pngdsp.o
OBJS-$(CONFIG_PNG_ENCODER)             += png.o pngenc.o
OBJS-$(CONFIG_PPM_DECODER)             += pnmdec.o pnm.o
OBJS-$(CONFIG_PPM_ENCODER)             += pnmenc.o
OBJS-$(CONFIG_PRORES_DECODER)          += proresdec2.o proresdsp.o proresdata.o
OBJS-$(CONFIG_PRORES_ENCODER)          += proresenc_anatoliy.o proresdata.o
OBJS-$(CONFIG_PRORES_AW_ENCODER)       += proresenc_anatoliy.o proresdata.o
OBJS-$(CONFIG_PRORES_KS_ENCODER)       += proresenc_kostya.o proresdata.o
OBJS-$(CONFIG_PROSUMER_DECODER)        += prosumer.o
OBJS-$(CONFIG_PSD_DECODER)             += psd.o
OBJS-$(CONFIG_PTX_DECODER)             += ptx.o
OBJS-$(CONFIG_QCELP_DECODER)           += qcelpdec.o                     \
                                          celp_filters.o acelp_vectors.o \
                                          acelp_filters.o
OBJS-$(CONFIG_QDM2_DECODER)            += qdm2.o
OBJS-$(CONFIG_QDMC_DECODER)            += qdmc.o
OBJS-$(CONFIG_QDRAW_DECODER)           += qdrw.o
OBJS-$(CONFIG_QPEG_DECODER)            += qpeg.o
OBJS-$(CONFIG_QTRLE_DECODER)           += qtrle.o
OBJS-$(CONFIG_QTRLE_ENCODER)           += qtrleenc.o
OBJS-$(CONFIG_R10K_DECODER)            += r210dec.o
OBJS-$(CONFIG_R10K_ENCODER)            += r210enc.o
OBJS-$(CONFIG_R210_DECODER)            += r210dec.o
OBJS-$(CONFIG_R210_ENCODER)            += r210enc.o
OBJS-$(CONFIG_RA_144_DECODER)          += ra144dec.o ra144.o celp_filters.o
OBJS-$(CONFIG_RA_144_ENCODER)          += ra144enc.o ra144.o celp_filters.o
OBJS-$(CONFIG_RA_288_DECODER)          += ra288.o celp_filters.o
OBJS-$(CONFIG_RALF_DECODER)            += ralf.o
OBJS-$(CONFIG_RASC_DECODER)            += rasc.o
OBJS-$(CONFIG_RAWVIDEO_DECODER)        += rawdec.o
OBJS-$(CONFIG_RAWVIDEO_ENCODER)        += rawenc.o
OBJS-$(CONFIG_REALTEXT_DECODER)        += realtextdec.o ass.o
OBJS-$(CONFIG_RL2_DECODER)             += rl2.o
OBJS-$(CONFIG_ROQ_DECODER)             += roqvideodec.o roqvideo.o
OBJS-$(CONFIG_ROQ_ENCODER)             += roqvideoenc.o roqvideo.o elbg.o
OBJS-$(CONFIG_ROQ_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_ROQ_DPCM_ENCODER)        += roqaudioenc.o
OBJS-$(CONFIG_RPZA_DECODER)            += rpza.o
OBJS-$(CONFIG_RSCC_DECODER)            += rscc.o
OBJS-$(CONFIG_RV10_DECODER)            += rv10.o
OBJS-$(CONFIG_RV10_ENCODER)            += rv10enc.o
OBJS-$(CONFIG_RV20_DECODER)            += rv10.o
OBJS-$(CONFIG_RV20_ENCODER)            += rv20enc.o
OBJS-$(CONFIG_RV30_DECODER)            += rv30.o rv34.o rv30dsp.o
OBJS-$(CONFIG_RV40_DECODER)            += rv40.o rv34.o rv40dsp.o
OBJS-$(CONFIG_SAMI_DECODER)            += samidec.o ass.o htmlsubtitles.o
OBJS-$(CONFIG_S302M_DECODER)           += s302m.o
OBJS-$(CONFIG_S302M_ENCODER)           += s302menc.o
OBJS-$(CONFIG_SANM_DECODER)            += sanm.o
OBJS-$(CONFIG_SCPR_DECODER)            += scpr.o
OBJS-$(CONFIG_SCREENPRESSO_DECODER)    += screenpresso.o
OBJS-$(CONFIG_SDX2_DPCM_DECODER)       += dpcm.o
OBJS-$(CONFIG_SGI_DECODER)             += sgidec.o
OBJS-$(CONFIG_SGI_ENCODER)             += sgienc.o rle.o
OBJS-$(CONFIG_SGIRLE_DECODER)          += sgirledec.o
OBJS-$(CONFIG_SHEERVIDEO_DECODER)      += sheervideo.o
OBJS-$(CONFIG_SHORTEN_DECODER)         += shorten.o
OBJS-$(CONFIG_SIPR_DECODER)            += sipr.o acelp_pitch_delay.o \
                                          celp_math.o acelp_vectors.o \
                                          acelp_filters.o celp_filters.o \
                                          sipr16k.o
OBJS-$(CONFIG_SIREN_DECODER)           += siren.o
OBJS-$(CONFIG_SMACKAUD_DECODER)        += smacker.o
OBJS-$(CONFIG_SMACKER_DECODER)         += smacker.o
OBJS-$(CONFIG_SMC_DECODER)             += smc.o
OBJS-$(CONFIG_SMVJPEG_DECODER)         += smvjpegdec.o
OBJS-$(CONFIG_SNOW_DECODER)            += snowdec.o snow.o snow_dwt.o
OBJS-$(CONFIG_SNOW_ENCODER)            += snowenc.o snow.o snow_dwt.o             \
                                          h263.o h263data.o ituh263enc.o
OBJS-$(CONFIG_SOL_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_SONIC_DECODER)           += sonic.o
OBJS-$(CONFIG_SONIC_ENCODER)           += sonic.o
OBJS-$(CONFIG_SONIC_LS_ENCODER)        += sonic.o
OBJS-$(CONFIG_SPEEDHQ_DECODER)         += speedhq.o mpeg12.o mpeg12data.o simple_idct.o
OBJS-$(CONFIG_SP5X_DECODER)            += sp5xdec.o
OBJS-$(CONFIG_SRGC_DECODER)            += mscc.o
OBJS-$(CONFIG_SRT_DECODER)             += srtdec.o ass.o htmlsubtitles.o
OBJS-$(CONFIG_SRT_ENCODER)             += srtenc.o ass_split.o
OBJS-$(CONFIG_STL_DECODER)             += textdec.o ass.o
OBJS-$(CONFIG_SUBRIP_DECODER)          += srtdec.o ass.o htmlsubtitles.o
OBJS-$(CONFIG_SUBRIP_ENCODER)          += srtenc.o ass_split.o
OBJS-$(CONFIG_SUBVIEWER1_DECODER)      += textdec.o ass.o
OBJS-$(CONFIG_SUBVIEWER_DECODER)       += subviewerdec.o ass.o
OBJS-$(CONFIG_SUNRAST_DECODER)         += sunrast.o
OBJS-$(CONFIG_SUNRAST_ENCODER)         += sunrastenc.o
OBJS-$(CONFIG_LIBRSVG_DECODER)         += librsvgdec.o
OBJS-$(CONFIG_SBC_DECODER)             += sbcdec.o sbcdec_data.o sbc.o
OBJS-$(CONFIG_SBC_ENCODER)             += sbcenc.o sbc.o sbcdsp.o sbcdsp_data.o
OBJS-$(CONFIG_SVQ1_DECODER)            += svq1dec.o svq1.o h263data.o
OBJS-$(CONFIG_SVQ1_ENCODER)            += svq1enc.o svq1.o  h263data.o  \
                                          h263.o ituh263enc.o
OBJS-$(CONFIG_SVQ3_DECODER)            += svq3.o mpegutils.o h264data.o
OBJS-$(CONFIG_TEXT_DECODER)            += textdec.o ass.o
OBJS-$(CONFIG_TEXT_ENCODER)            += srtenc.o ass_split.o
OBJS-$(CONFIG_TAK_DECODER)             += takdec.o tak.o takdsp.o
OBJS-$(CONFIG_TARGA_DECODER)           += targa.o
OBJS-$(CONFIG_TARGA_ENCODER)           += targaenc.o rle.o
OBJS-$(CONFIG_TARGA_Y216_DECODER)      += targa_y216dec.o
OBJS-$(CONFIG_TDSC_DECODER)            += tdsc.o
OBJS-$(CONFIG_TIERTEXSEQVIDEO_DECODER) += tiertexseqv.o
OBJS-$(CONFIG_TIFF_DECODER)            += tiff.o lzw.o faxcompr.o tiff_data.o tiff_common.o mjpegdec.o
OBJS-$(CONFIG_TIFF_ENCODER)            += tiffenc.o rle.o lzwenc.o tiff_data.o
OBJS-$(CONFIG_TMV_DECODER)             += tmv.o cga_data.o
OBJS-$(CONFIG_TRUEHD_DECODER)          += mlpdec.o mlpdsp.o
OBJS-$(CONFIG_TRUEHD_ENCODER)          += mlpenc.o mlp.o
OBJS-$(CONFIG_TRUEMOTION1_DECODER)     += truemotion1.o
OBJS-$(CONFIG_TRUEMOTION2_DECODER)     += truemotion2.o
OBJS-$(CONFIG_TRUEMOTION2RT_DECODER)   += truemotion2rt.o
OBJS-$(CONFIG_TRUESPEECH_DECODER)      += truespeech.o
OBJS-$(CONFIG_TSCC_DECODER)            += tscc.o msrledec.o
OBJS-$(CONFIG_TSCC2_DECODER)           += tscc2.o
OBJS-$(CONFIG_TTA_DECODER)             += tta.o ttadata.o ttadsp.o
OBJS-$(CONFIG_TTA_ENCODER)             += ttaenc.o ttaencdsp.o ttadata.o
OBJS-$(CONFIG_TWINVQ_DECODER)          += twinvqdec.o twinvq.o
OBJS-$(CONFIG_TXD_DECODER)             += txd.o
OBJS-$(CONFIG_ULTI_DECODER)            += ulti.o
OBJS-$(CONFIG_UTVIDEO_DECODER)         += utvideodec.o utvideo.o utvideodsp.o
OBJS-$(CONFIG_UTVIDEO_ENCODER)         += utvideoenc.o utvideo.o
OBJS-$(CONFIG_V210_DECODER)            += v210dec.o
OBJS-$(CONFIG_V210_ENCODER)            += v210enc.o
OBJS-$(CONFIG_V210X_DECODER)           += v210x.o
OBJS-$(CONFIG_V308_DECODER)            += v308dec.o
OBJS-$(CONFIG_V308_ENCODER)            += v308enc.o
OBJS-$(CONFIG_V408_DECODER)            += v408dec.o
OBJS-$(CONFIG_V408_ENCODER)            += v408enc.o
OBJS-$(CONFIG_V410_DECODER)            += v410dec.o
OBJS-$(CONFIG_V410_ENCODER)            += v410enc.o
OBJS-$(CONFIG_VB_DECODER)              += vb.o
OBJS-$(CONFIG_VBLE_DECODER)            += vble.o
OBJS-$(CONFIG_VC1_DECODER)             += vc1dec.o vc1_block.o vc1_loopfilter.o \
                                          vc1_mc.o vc1_pred.o vc1.o vc1data.o \
                                          msmpeg4dec.o msmpeg4.o msmpeg4data.o \
                                          wmv2dsp.o wmv2data.o
OBJS-$(CONFIG_VC1_CUVID_DECODER)       += cuviddec.o
OBJS-$(CONFIG_VC1_MMAL_DECODER)        += mmaldec.o
OBJS-$(CONFIG_VC1_QSV_DECODER)         += qsvdec_other.o
OBJS-$(CONFIG_VC1_V4L2M2M_DECODER)     += v4l2_m2m_dec.o
OBJS-$(CONFIG_VC2_ENCODER)             += vc2enc.o vc2enc_dwt.o diractab.o
OBJS-$(CONFIG_VCR1_DECODER)            += vcr1.o
OBJS-$(CONFIG_VMDAUDIO_DECODER)        += vmdaudio.o
OBJS-$(CONFIG_VMDVIDEO_DECODER)        += vmdvideo.o
OBJS-$(CONFIG_VMNC_DECODER)            += vmnc.o
OBJS-$(CONFIG_VORBIS_DECODER)          += vorbisdec.o vorbisdsp.o vorbis.o \
                                          vorbis_data.o
OBJS-$(CONFIG_VORBIS_ENCODER)          += vorbisenc.o vorbis.o \
                                          vorbis_data.o
OBJS-$(CONFIG_VP3_DECODER)             += vp3.o
OBJS-$(CONFIG_VP5_DECODER)             += vp5.o vp56.o vp56data.o vp56rac.o
OBJS-$(CONFIG_VP6_DECODER)             += vp6.o vp56.o vp56data.o \
                                          vp6dsp.o vp56rac.o
OBJS-$(CONFIG_VP7_DECODER)             += vp8.o vp56rac.o
OBJS-$(CONFIG_VP8_DECODER)             += vp8.o vp56rac.o
OBJS-$(CONFIG_VP8_CUVID_DECODER)       += cuviddec.o
OBJS-$(CONFIG_VP8_MEDIACODEC_DECODER)  += mediacodecdec.o
OBJS-$(CONFIG_VP8_QSV_DECODER)         += qsvdec_other.o
OBJS-$(CONFIG_VP8_RKMPP_DECODER)       += rkmppdec.o
OBJS-$(CONFIG_VP8_VAAPI_ENCODER)       += vaapi_encode_vp8.o
OBJS-$(CONFIG_VP8_V4L2M2M_DECODER)     += v4l2_m2m_dec.o
OBJS-$(CONFIG_VP8_V4L2M2M_ENCODER)     += v4l2_m2m_enc.o
OBJS-$(CONFIG_VP9_DECODER)             += vp9.o vp9data.o vp9dsp.o vp9lpf.o vp9recon.o \
                                          vp9block.o vp9prob.o vp9mvs.o vp56rac.o \
                                          vp9dsp_8bpp.o vp9dsp_10bpp.o vp9dsp_12bpp.o
OBJS-$(CONFIG_VP9_CUVID_DECODER)       += cuviddec.o
OBJS-$(CONFIG_VP9_MEDIACODEC_DECODER)  += mediacodecdec.o
OBJS-$(CONFIG_VP9_RKMPP_DECODER)       += rkmppdec.o
OBJS-$(CONFIG_VP9_VAAPI_ENCODER)       += vaapi_encode_vp9.o
OBJS-$(CONFIG_VP9_QSV_ENCODER)         += qsvenc_vp9.o
OBJS-$(CONFIG_VPLAYER_DECODER)         += textdec.o ass.o
OBJS-$(CONFIG_VP9_V4L2M2M_DECODER)     += v4l2_m2m_dec.o
OBJS-$(CONFIG_VQA_DECODER)             += vqavideo.o
OBJS-$(CONFIG_WAVPACK_DECODER)         += wavpack.o dsd.o
OBJS-$(CONFIG_WAVPACK_ENCODER)         += wavpackenc.o
OBJS-$(CONFIG_WCMV_DECODER)            += wcmv.o
OBJS-$(CONFIG_WEBP_DECODER)            += webp.o
OBJS-$(CONFIG_WEBVTT_DECODER)          += webvttdec.o ass.o
OBJS-$(CONFIG_WEBVTT_ENCODER)          += webvttenc.o ass_split.o
OBJS-$(CONFIG_WMALOSSLESS_DECODER)     += wmalosslessdec.o wma_common.o
OBJS-$(CONFIG_WMAPRO_DECODER)          += wmaprodec.o wma.o wma_common.o
OBJS-$(CONFIG_WMAV1_DECODER)           += wmadec.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV1_ENCODER)           += wmaenc.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV2_DECODER)           += wmadec.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAV2_ENCODER)           += wmaenc.o wma.o wma_common.o aactab.o
OBJS-$(CONFIG_WMAVOICE_DECODER)        += wmavoice.o \
                                          celp_filters.o \
                                          acelp_vectors.o acelp_filters.o
OBJS-$(CONFIG_WMV1_DECODER)            += msmpeg4dec.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_WMV1_ENCODER)            += msmpeg4enc.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_WMV2_DECODER)            += wmv2dec.o wmv2.o wmv2data.o \
                                          msmpeg4dec.o msmpeg4.o msmpeg4data.o
OBJS-$(CONFIG_WMV2_ENCODER)            += wmv2enc.o wmv2.o wmv2data.o \
                                          msmpeg4.o msmpeg4enc.o msmpeg4data.o
OBJS-$(CONFIG_WNV1_DECODER)            += wnv1.o
OBJS-$(CONFIG_WRAPPED_AVFRAME_DECODER) += wrapped_avframe.o
OBJS-$(CONFIG_WRAPPED_AVFRAME_ENCODER) += wrapped_avframe.o
OBJS-$(CONFIG_WS_SND1_DECODER)         += ws-snd1.o
OBJS-$(CONFIG_XAN_DPCM_DECODER)        += dpcm.o
OBJS-$(CONFIG_XAN_WC3_DECODER)         += xan.o
OBJS-$(CONFIG_XAN_WC4_DECODER)         += xxan.o
OBJS-$(CONFIG_XBIN_DECODER)            += bintext.o cga_data.o
OBJS-$(CONFIG_XBM_DECODER)             += xbmdec.o
OBJS-$(CONFIG_XBM_ENCODER)             += xbmenc.o
OBJS-$(CONFIG_XFACE_DECODER)           += xfacedec.o xface.o
OBJS-$(CONFIG_XFACE_ENCODER)           += xfaceenc.o xface.o
OBJS-$(CONFIG_XL_DECODER)              += xl.o
OBJS-$(CONFIG_XMA1_DECODER)            += wmaprodec.o wma.o wma_common.o
OBJS-$(CONFIG_XMA2_DECODER)            += wmaprodec.o wma.o wma_common.o
OBJS-$(CONFIG_XPM_DECODER)             += xpmdec.o
OBJS-$(CONFIG_XSUB_DECODER)            += xsubdec.o
OBJS-$(CONFIG_XSUB_ENCODER)            += xsubenc.o
OBJS-$(CONFIG_XWD_DECODER)             += xwddec.o
OBJS-$(CONFIG_XWD_ENCODER)             += xwdenc.o
OBJS-$(CONFIG_Y41P_DECODER)            += y41pdec.o
OBJS-$(CONFIG_Y41P_ENCODER)            += y41penc.o
OBJS-$(CONFIG_YLC_DECODER)             += ylc.o
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
OBJS-$(CONFIG_PCM_BLURAY_DECODER)         += pcm-bluray.o
OBJS-$(CONFIG_PCM_DVD_DECODER)            += pcm-dvd.o
OBJS-$(CONFIG_PCM_DVD_ENCODER)            += pcm-dvdenc.o
OBJS-$(CONFIG_PCM_F16LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_F24LE_DECODER)          += pcm.o
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
OBJS-$(CONFIG_PCM_S8_PLANAR_DECODER)      += pcm.o
OBJS-$(CONFIG_PCM_S8_PLANAR_ENCODER)      += pcm.o
OBJS-$(CONFIG_PCM_S16BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16BE_PLANAR_DECODER)   += pcm.o
OBJS-$(CONFIG_PCM_S16BE_PLANAR_ENCODER)   += pcm.o
OBJS-$(CONFIG_PCM_S16LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S16LE_PLANAR_DECODER)   += pcm.o
OBJS-$(CONFIG_PCM_S16LE_PLANAR_ENCODER)   += pcm.o
OBJS-$(CONFIG_PCM_S24BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_DECODER)        += pcm.o
OBJS-$(CONFIG_PCM_S24DAUD_ENCODER)        += pcm.o
OBJS-$(CONFIG_PCM_S24LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S24LE_PLANAR_DECODER)   += pcm.o
OBJS-$(CONFIG_PCM_S24LE_PLANAR_ENCODER)   += pcm.o
OBJS-$(CONFIG_PCM_S32BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32LE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S32LE_PLANAR_DECODER)   += pcm.o
OBJS-$(CONFIG_PCM_S32LE_PLANAR_ENCODER)   += pcm.o
OBJS-$(CONFIG_PCM_S64BE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S64BE_ENCODER)          += pcm.o
OBJS-$(CONFIG_PCM_S64LE_DECODER)          += pcm.o
OBJS-$(CONFIG_PCM_S64LE_ENCODER)          += pcm.o
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
OBJS-$(CONFIG_PCM_VIDC_DECODER)           += pcm.o
OBJS-$(CONFIG_PCM_VIDC_ENCODER)           += pcm.o

OBJS-$(CONFIG_ADPCM_4XM_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_ADX_DECODER)          += adxdec.o adx.o
OBJS-$(CONFIG_ADPCM_ADX_ENCODER)          += adxenc.o adx.o
OBJS-$(CONFIG_ADPCM_AFC_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_AGM_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_AICA_DECODER)         += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_ARGO_DECODER)         += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_CT_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_DTK_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_MAXIS_XA_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R1_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R2_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_R3_DECODER)        += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_EA_XAS_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_G722_DECODER)         += g722.o g722dsp.o g722dec.o
OBJS-$(CONFIG_ADPCM_G722_ENCODER)         += g722.o g722dsp.o g722enc.o
OBJS-$(CONFIG_ADPCM_G726_DECODER)         += g726.o
OBJS-$(CONFIG_ADPCM_G726_ENCODER)         += g726.o
OBJS-$(CONFIG_ADPCM_G726LE_DECODER)       += g726.o
OBJS-$(CONFIG_ADPCM_G726LE_ENCODER)       += g726.o
OBJS-$(CONFIG_ADPCM_IMA_AMV_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_ALP_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_APC_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_APM_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_CUNNING_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_DAT4_DECODER)     += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_DK3_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_DK4_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_EA_EACS_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_EA_SEAD_DECODER)  += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_ISS_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_MTF_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_OKI_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_QT_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_QT_ENCODER)       += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_RAD_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_SSI_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_SSI_ENCODER)      += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_SMJPEG_DECODER)   += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WAV_ENCODER)      += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_IMA_WS_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_MS_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_MS_ENCODER)           += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_MTAF_DECODER)         += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_PSX_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_2_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_3_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SBPRO_4_DECODER)      += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SWF_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_SWF_ENCODER)          += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_THP_DECODER)          += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_THP_LE_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_VIMA_DECODER)         += vima.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_XA_DECODER)           += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_YAMAHA_DECODER)       += adpcm.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_YAMAHA_ENCODER)       += adpcmenc.o adpcm_data.o
OBJS-$(CONFIG_ADPCM_ZORK_DECODER)         += adpcm.o adpcm_data.o

# hardware accelerators
OBJS-$(CONFIG_D3D11VA)                    += dxva2.o
OBJS-$(CONFIG_DXVA2)                      += dxva2.o
OBJS-$(CONFIG_NVDEC)                      += nvdec.o
OBJS-$(CONFIG_VAAPI)                      += vaapi_decode.o
OBJS-$(CONFIG_VIDEOTOOLBOX)               += videotoolbox.o
OBJS-$(CONFIG_VDPAU)                      += vdpau.o

OBJS-$(CONFIG_H263_VAAPI_HWACCEL)         += vaapi_mpeg4.o
OBJS-$(CONFIG_H263_VIDEOTOOLBOX_HWACCEL)  += videotoolbox.o
OBJS-$(CONFIG_H264_D3D11VA_HWACCEL)       += dxva2_h264.o
OBJS-$(CONFIG_H264_DXVA2_HWACCEL)         += dxva2_h264.o
OBJS-$(CONFIG_H264_NVDEC_HWACCEL)         += nvdec_h264.o
OBJS-$(CONFIG_H264_QSV_HWACCEL)           += qsvdec_h2645.o
OBJS-$(CONFIG_H264_VAAPI_HWACCEL)         += vaapi_h264.o
OBJS-$(CONFIG_H264_VDPAU_HWACCEL)         += vdpau_h264.o
OBJS-$(CONFIG_H264_VIDEOTOOLBOX_HWACCEL)  += videotoolbox.o
OBJS-$(CONFIG_HEVC_D3D11VA_HWACCEL)       += dxva2_hevc.o
OBJS-$(CONFIG_HEVC_DXVA2_HWACCEL)         += dxva2_hevc.o
OBJS-$(CONFIG_HEVC_NVDEC_HWACCEL)         += nvdec_hevc.o
OBJS-$(CONFIG_HEVC_QSV_HWACCEL)           += qsvdec_h2645.o
OBJS-$(CONFIG_HEVC_VAAPI_HWACCEL)         += vaapi_hevc.o h265_profile_level.o
OBJS-$(CONFIG_HEVC_VDPAU_HWACCEL)         += vdpau_hevc.o h265_profile_level.o
OBJS-$(CONFIG_MJPEG_NVDEC_HWACCEL)        += nvdec_mjpeg.o
OBJS-$(CONFIG_MJPEG_VAAPI_HWACCEL)        += vaapi_mjpeg.o
OBJS-$(CONFIG_MPEG1_NVDEC_HWACCEL)        += nvdec_mpeg12.o
OBJS-$(CONFIG_MPEG1_VDPAU_HWACCEL)        += vdpau_mpeg12.o
OBJS-$(CONFIG_MPEG1_VIDEOTOOLBOX_HWACCEL) += videotoolbox.o
OBJS-$(CONFIG_MPEG1_XVMC_HWACCEL)         += mpegvideo_xvmc.o
OBJS-$(CONFIG_MPEG2_D3D11VA_HWACCEL)      += dxva2_mpeg2.o
OBJS-$(CONFIG_MPEG2_DXVA2_HWACCEL)        += dxva2_mpeg2.o
OBJS-$(CONFIG_MPEG2_NVDEC_HWACCEL)        += nvdec_mpeg12.o
OBJS-$(CONFIG_MPEG2_QSV_HWACCEL)          += qsvdec_other.o
OBJS-$(CONFIG_MPEG2_VAAPI_HWACCEL)        += vaapi_mpeg2.o
OBJS-$(CONFIG_MPEG2_VDPAU_HWACCEL)        += vdpau_mpeg12.o
OBJS-$(CONFIG_MPEG2_VIDEOTOOLBOX_HWACCEL) += videotoolbox.o
OBJS-$(CONFIG_MPEG2_XVMC_HWACCEL)         += mpegvideo_xvmc.o
OBJS-$(CONFIG_MPEG4_NVDEC_HWACCEL)        += nvdec_mpeg4.o
OBJS-$(CONFIG_MPEG4_VAAPI_HWACCEL)        += vaapi_mpeg4.o
OBJS-$(CONFIG_MPEG4_VDPAU_HWACCEL)        += vdpau_mpeg4.o
OBJS-$(CONFIG_MPEG4_VIDEOTOOLBOX_HWACCEL) += videotoolbox.o
OBJS-$(CONFIG_VC1_D3D11VA_HWACCEL)        += dxva2_vc1.o
OBJS-$(CONFIG_VC1_DXVA2_HWACCEL)          += dxva2_vc1.o
OBJS-$(CONFIG_VC1_NVDEC_HWACCEL)          += nvdec_vc1.o
OBJS-$(CONFIG_VC1_QSV_HWACCEL)            += qsvdec_other.o
OBJS-$(CONFIG_VC1_VAAPI_HWACCEL)          += vaapi_vc1.o
OBJS-$(CONFIG_VC1_VDPAU_HWACCEL)          += vdpau_vc1.o
OBJS-$(CONFIG_VP8_NVDEC_HWACCEL)          += nvdec_vp8.o
OBJS-$(CONFIG_VP8_VAAPI_HWACCEL)          += vaapi_vp8.o
OBJS-$(CONFIG_VP9_D3D11VA_HWACCEL)        += dxva2_vp9.o
OBJS-$(CONFIG_VP9_DXVA2_HWACCEL)          += dxva2_vp9.o
OBJS-$(CONFIG_VP9_NVDEC_HWACCEL)          += nvdec_vp9.o
OBJS-$(CONFIG_VP9_VAAPI_HWACCEL)          += vaapi_vp9.o
OBJS-$(CONFIG_VP9_VDPAU_HWACCEL)          += vdpau_vp9.o
OBJS-$(CONFIG_VP8_QSV_HWACCEL)            += qsvdec_other.o

# libavformat dependencies
OBJS-$(CONFIG_ISO_MEDIA)               += mpeg4audio.o mpegaudiodata.o

OBJS-$(CONFIG_ADTS_MUXER)              += mpeg4audio.o
OBJS-$(CONFIG_CAF_DEMUXER)             += ac3tab.o
OBJS-$(CONFIG_CODEC2_DEMUXER)          += codec2utils.o
OBJS-$(CONFIG_CODEC2_MUXER)            += codec2utils.o
OBJS-$(CONFIG_CODEC2RAW_DEMUXER)       += codec2utils.o
OBJS-$(CONFIG_DNXHD_DEMUXER)           += dnxhddata.o
OBJS-$(CONFIG_FITS_DEMUXER)            += fits.o
OBJS-$(CONFIG_FLV_DEMUXER)             += mpeg4audio.o
OBJS-$(CONFIG_LATM_MUXER)              += mpeg4audio.o
OBJS-$(CONFIG_MATROSKA_AUDIO_MUXER)    += mpeg4audio.o
OBJS-$(CONFIG_MATROSKA_MUXER)          += mpeg4audio.o
OBJS-$(CONFIG_MOV_DEMUXER)             += ac3tab.o
OBJS-$(CONFIG_MXF_MUXER)               += dnxhddata.o
OBJS-$(CONFIG_NUT_MUXER)               += mpegaudiodata.o
OBJS-$(CONFIG_NUT_DEMUXER)             += mpegaudiodata.o mpeg4audio.o
OBJS-$(CONFIG_RTP_MUXER)               += mpeg4audio.o
OBJS-$(CONFIG_SPDIF_MUXER)             += dca.o
OBJS-$(CONFIG_TAK_DEMUXER)             += tak.o
OBJS-$(CONFIG_WEBM_MUXER)              += mpeg4audio.o

# libavfilter dependencies
OBJS-$(CONFIG_ELBG_FILTER)             += elbg.o

# external codec libraries
OBJS-$(CONFIG_AAC_AT_DECODER)             += audiotoolboxdec.o
OBJS-$(CONFIG_AC3_AT_DECODER)             += audiotoolboxdec.o
OBJS-$(CONFIG_ADPCM_IMA_QT_AT_DECODER)    += audiotoolboxdec.o
OBJS-$(CONFIG_ALAC_AT_DECODER)            += audiotoolboxdec.o
OBJS-$(CONFIG_AMR_NB_AT_DECODER)          += audiotoolboxdec.o
OBJS-$(CONFIG_EAC3_AT_DECODER)            += audiotoolboxdec.o
OBJS-$(CONFIG_GSM_MS_AT_DECODER)          += audiotoolboxdec.o
OBJS-$(CONFIG_ILBC_AT_DECODER)            += audiotoolboxdec.o
OBJS-$(CONFIG_MP1_AT_DECODER)             += audiotoolboxdec.o
OBJS-$(CONFIG_MP2_AT_DECODER)             += audiotoolboxdec.o
OBJS-$(CONFIG_MP3_AT_DECODER)             += audiotoolboxdec.o
OBJS-$(CONFIG_PCM_MULAW_AT_DECODER)       += audiotoolboxdec.o
OBJS-$(CONFIG_PCM_ALAW_AT_DECODER)        += audiotoolboxdec.o
OBJS-$(CONFIG_QDMC_AT_DECODER)            += audiotoolboxdec.o
OBJS-$(CONFIG_QDM2_AT_DECODER)            += audiotoolboxdec.o
OBJS-$(CONFIG_AAC_AT_ENCODER)             += audiotoolboxenc.o
OBJS-$(CONFIG_ALAC_AT_ENCODER)            += audiotoolboxenc.o
OBJS-$(CONFIG_ILBC_AT_ENCODER)            += audiotoolboxenc.o
OBJS-$(CONFIG_PCM_ALAW_AT_ENCODER)        += audiotoolboxenc.o
OBJS-$(CONFIG_PCM_MULAW_AT_ENCODER)       += audiotoolboxenc.o
OBJS-$(CONFIG_LIBAOM_AV1_DECODER)         += libaomdec.o
OBJS-$(CONFIG_LIBAOM_AV1_ENCODER)         += libaomenc.o
OBJS-$(CONFIG_LIBARIBB24_DECODER)         += libaribb24.o ass.o
OBJS-$(CONFIG_LIBCELT_DECODER)            += libcelt_dec.o
OBJS-$(CONFIG_LIBCODEC2_DECODER)          += libcodec2.o codec2utils.o
OBJS-$(CONFIG_LIBCODEC2_ENCODER)          += libcodec2.o codec2utils.o
OBJS-$(CONFIG_LIBDAV1D_DECODER)           += libdav1d.o
OBJS-$(CONFIG_LIBDAVS2_DECODER)           += libdavs2.o
OBJS-$(CONFIG_LIBFDK_AAC_DECODER)         += libfdk-aacdec.o
OBJS-$(CONFIG_LIBFDK_AAC_ENCODER)         += libfdk-aacenc.o
OBJS-$(CONFIG_LIBGSM_DECODER)             += libgsmdec.o
OBJS-$(CONFIG_LIBGSM_ENCODER)             += libgsmenc.o
OBJS-$(CONFIG_LIBGSM_MS_DECODER)          += libgsmdec.o
OBJS-$(CONFIG_LIBGSM_MS_ENCODER)          += libgsmenc.o
OBJS-$(CONFIG_LIBILBC_DECODER)            += libilbc.o
OBJS-$(CONFIG_LIBILBC_ENCODER)            += libilbc.o
OBJS-$(CONFIG_LIBKVAZAAR_ENCODER)         += libkvazaar.o
OBJS-$(CONFIG_LIBMP3LAME_ENCODER)         += libmp3lame.o
OBJS-$(CONFIG_LIBOPENCORE_AMRNB_DECODER)  += libopencore-amr.o
OBJS-$(CONFIG_LIBOPENCORE_AMRNB_ENCODER)  += libopencore-amr.o
OBJS-$(CONFIG_LIBOPENCORE_AMRWB_DECODER)  += libopencore-amr.o
OBJS-$(CONFIG_LIBOPENH264_DECODER)        += libopenh264dec.o libopenh264.o
OBJS-$(CONFIG_LIBOPENH264_ENCODER)        += libopenh264enc.o libopenh264.o
OBJS-$(CONFIG_LIBOPENJPEG_DECODER)        += libopenjpegdec.o
OBJS-$(CONFIG_LIBOPENJPEG_ENCODER)        += libopenjpegenc.o
OBJS-$(CONFIG_LIBOPUS_DECODER)            += libopusdec.o libopus.o     \
                                             vorbis_data.o
OBJS-$(CONFIG_LIBOPUS_ENCODER)            += libopusenc.o libopus.o     \
                                             vorbis_data.o
OBJS-$(CONFIG_LIBRAV1E_ENCODER)           += librav1e.o
OBJS-$(CONFIG_LIBSHINE_ENCODER)           += libshine.o
OBJS-$(CONFIG_LIBSPEEX_DECODER)           += libspeexdec.o
OBJS-$(CONFIG_LIBSPEEX_ENCODER)           += libspeexenc.o
OBJS-$(CONFIG_LIBTHEORA_ENCODER)          += libtheoraenc.o
OBJS-$(CONFIG_LIBTWOLAME_ENCODER)         += libtwolame.o
OBJS-$(CONFIG_LIBVO_AMRWBENC_ENCODER)     += libvo-amrwbenc.o
OBJS-$(CONFIG_LIBVORBIS_DECODER)          += libvorbisdec.o
OBJS-$(CONFIG_LIBVORBIS_ENCODER)          += libvorbisenc.o \
                                             vorbis_data.o
OBJS-$(CONFIG_LIBVPX_VP8_DECODER)         += libvpxdec.o
OBJS-$(CONFIG_LIBVPX_VP8_ENCODER)         += libvpxenc.o
OBJS-$(CONFIG_LIBVPX_VP9_DECODER)         += libvpxdec.o libvpx.o
OBJS-$(CONFIG_LIBVPX_VP9_ENCODER)         += libvpxenc.o libvpx.o
OBJS-$(CONFIG_LIBWAVPACK_ENCODER)         += libwavpackenc.o
OBJS-$(CONFIG_LIBWEBP_ENCODER)            += libwebpenc_common.o libwebpenc.o
OBJS-$(CONFIG_LIBWEBP_ANIM_ENCODER)       += libwebpenc_common.o libwebpenc_animencoder.o
OBJS-$(CONFIG_LIBX262_ENCODER)            += libx264.o
OBJS-$(CONFIG_LIBX264_ENCODER)            += libx264.o
OBJS-$(CONFIG_LIBX265_ENCODER)            += libx265.o
OBJS-$(CONFIG_LIBXAVS_ENCODER)            += libxavs.o
OBJS-$(CONFIG_LIBXAVS2_ENCODER)           += libxavs2.o
OBJS-$(CONFIG_LIBXVID_ENCODER)            += libxvid.o
OBJS-$(CONFIG_LIBZVBI_TELETEXT_DECODER)   += libzvbi-teletextdec.o ass.o

# parsers
OBJS-$(CONFIG_AAC_LATM_PARSER)         += latm_parser.o
OBJS-$(CONFIG_AAC_PARSER)              += aac_parser.o aac_ac3_parser.o \
                                          mpeg4audio.o
OBJS-$(CONFIG_AC3_PARSER)              += ac3tab.o aac_ac3_parser.o
OBJS-$(CONFIG_ADX_PARSER)              += adx_parser.o adx.o
OBJS-$(CONFIG_AV1_PARSER)              += av1_parser.o av1_parse.o
OBJS-$(CONFIG_AVS2_PARSER)             += avs2_parser.o
OBJS-$(CONFIG_BMP_PARSER)              += bmp_parser.o
OBJS-$(CONFIG_CAVSVIDEO_PARSER)        += cavs_parser.o
OBJS-$(CONFIG_COOK_PARSER)             += cook_parser.o
OBJS-$(CONFIG_DCA_PARSER)              += dca_parser.o dca_exss.o dca.o
OBJS-$(CONFIG_DIRAC_PARSER)            += dirac_parser.o
OBJS-$(CONFIG_DNXHD_PARSER)            += dnxhd_parser.o dnxhddata.o
OBJS-$(CONFIG_DPX_PARSER)              += dpx_parser.o
OBJS-$(CONFIG_DVAUDIO_PARSER)          += dvaudio_parser.o
OBJS-$(CONFIG_DVBSUB_PARSER)           += dvbsub_parser.o
OBJS-$(CONFIG_DVD_NAV_PARSER)          += dvd_nav_parser.o
OBJS-$(CONFIG_DVDSUB_PARSER)           += dvdsub_parser.o
OBJS-$(CONFIG_FLAC_PARSER)             += flac_parser.o flacdata.o flac.o \
                                          vorbis_data.o
OBJS-$(CONFIG_G723_1_PARSER)           += g723_1_parser.o
OBJS-$(CONFIG_G729_PARSER)             += g729_parser.o
OBJS-$(CONFIG_GIF_PARSER)              += gif_parser.o
OBJS-$(CONFIG_GSM_PARSER)              += gsm_parser.o
OBJS-$(CONFIG_H261_PARSER)             += h261_parser.o
OBJS-$(CONFIG_H263_PARSER)             += h263_parser.o
OBJS-$(CONFIG_H264_PARSER)             += h264_parser.o h264_sei.o h264data.o
OBJS-$(CONFIG_HEVC_PARSER)             += hevc_parser.o hevc_data.o
OBJS-$(CONFIG_JPEG2000_PARSER)         += jpeg2000_parser.o
OBJS-$(CONFIG_MJPEG_PARSER)            += mjpeg_parser.o
OBJS-$(CONFIG_MLP_PARSER)              += mlp_parse.o mlp_parser.o mlp.o
OBJS-$(CONFIG_MPEG4VIDEO_PARSER)       += mpeg4video_parser.o h263.o \
                                          mpeg4videodec.o mpeg4video.o \
                                          ituh263dec.o h263dec.o h263data.o
OBJS-$(CONFIG_MPEGAUDIO_PARSER)        += mpegaudio_parser.o
OBJS-$(CONFIG_MPEGVIDEO_PARSER)        += mpegvideo_parser.o    \
                                          mpeg12.o mpeg12data.o
OBJS-$(CONFIG_OPUS_PARSER)             += opus_parser.o opus.o opustab.o \
                                          opus_rc.o vorbis_data.o
OBJS-$(CONFIG_PNG_PARSER)              += png_parser.o
OBJS-$(CONFIG_PNM_PARSER)              += pnm_parser.o pnm.o
OBJS-$(CONFIG_RV30_PARSER)             += rv34_parser.o
OBJS-$(CONFIG_RV40_PARSER)             += rv34_parser.o
OBJS-$(CONFIG_SBC_PARSER)              += sbc_parser.o
OBJS-$(CONFIG_SIPR_PARSER)             += sipr_parser.o
OBJS-$(CONFIG_TAK_PARSER)              += tak_parser.o tak.o
OBJS-$(CONFIG_VC1_PARSER)              += vc1_parser.o vc1.o vc1data.o  \
                                          simple_idct.o wmv2data.o
OBJS-$(CONFIG_VP3_PARSER)              += vp3_parser.o
OBJS-$(CONFIG_VP8_PARSER)              += vp8_parser.o
OBJS-$(CONFIG_VP9_PARSER)              += vp9_parser.o
OBJS-$(CONFIG_WEBP_PARSER)             += webp_parser.o
OBJS-$(CONFIG_XMA_PARSER)              += xma_parser.o

# bitstream filters
OBJS-$(CONFIG_AAC_ADTSTOASC_BSF)          += aac_adtstoasc_bsf.o mpeg4audio.o
OBJS-$(CONFIG_AV1_METADATA_BSF)           += av1_metadata_bsf.o
OBJS-$(CONFIG_AV1_FRAME_MERGE_BSF)        += av1_frame_merge_bsf.o
OBJS-$(CONFIG_AV1_FRAME_SPLIT_BSF)        += av1_frame_split_bsf.o
OBJS-$(CONFIG_CHOMP_BSF)                  += chomp_bsf.o
OBJS-$(CONFIG_DUMP_EXTRADATA_BSF)         += dump_extradata_bsf.o
OBJS-$(CONFIG_DCA_CORE_BSF)               += dca_core_bsf.o
OBJS-$(CONFIG_EAC3_CORE_BSF)              += eac3_core_bsf.o
OBJS-$(CONFIG_EXTRACT_EXTRADATA_BSF)      += extract_extradata_bsf.o    \
                                             av1_parse.o h2645_parse.o
OBJS-$(CONFIG_FILTER_UNITS_BSF)           += filter_units_bsf.o
OBJS-$(CONFIG_H264_METADATA_BSF)          += h264_metadata_bsf.o h264_levels.o
OBJS-$(CONFIG_H264_MP4TOANNEXB_BSF)       += h264_mp4toannexb_bsf.o
OBJS-$(CONFIG_H264_REDUNDANT_PPS_BSF)     += h264_redundant_pps_bsf.o
OBJS-$(CONFIG_HAPQA_EXTRACT_BSF)          += hapqa_extract_bsf.o hap.o
OBJS-$(CONFIG_HEVC_METADATA_BSF)          += h265_metadata_bsf.o h265_profile_level.o
OBJS-$(CONFIG_HEVC_MP4TOANNEXB_BSF)       += hevc_mp4toannexb_bsf.o
OBJS-$(CONFIG_IMX_DUMP_HEADER_BSF)        += imx_dump_header_bsf.o
OBJS-$(CONFIG_MJPEG2JPEG_BSF)             += mjpeg2jpeg_bsf.o
OBJS-$(CONFIG_MJPEGA_DUMP_HEADER_BSF)     += mjpega_dump_header_bsf.o
OBJS-$(CONFIG_MPEG4_UNPACK_BFRAMES_BSF)   += mpeg4_unpack_bframes_bsf.o
OBJS-$(CONFIG_MOV2TEXTSUB_BSF)            += movsub_bsf.o
OBJS-$(CONFIG_MP3_HEADER_DECOMPRESS_BSF)  += mp3_header_decompress_bsf.o \
                                             mpegaudiodata.o
OBJS-$(CONFIG_MPEG2_METADATA_BSF)         += mpeg2_metadata_bsf.o
OBJS-$(CONFIG_NOISE_BSF)                  += noise_bsf.o
OBJS-$(CONFIG_NULL_BSF)                   += null_bsf.o
OBJS-$(CONFIG_OPUS_METADATA_BSF)          += opus_metadata_bsf.o
OBJS-$(CONFIG_PCM_RECHUNK_BSF)            += pcm_rechunk_bsf.o
OBJS-$(CONFIG_PRORES_METADATA_BSF)        += prores_metadata_bsf.o
OBJS-$(CONFIG_REMOVE_EXTRADATA_BSF)       += remove_extradata_bsf.o
OBJS-$(CONFIG_TEXT2MOVSUB_BSF)            += movsub_bsf.o
OBJS-$(CONFIG_TRACE_HEADERS_BSF)          += trace_headers_bsf.o
OBJS-$(CONFIG_TRUEHD_CORE_BSF)            += truehd_core_bsf.o mlp_parse.o mlp.o
OBJS-$(CONFIG_VP9_METADATA_BSF)           += vp9_metadata_bsf.o
OBJS-$(CONFIG_VP9_RAW_REORDER_BSF)        += vp9_raw_reorder_bsf.o
OBJS-$(CONFIG_VP9_SUPERFRAME_BSF)         += vp9_superframe_bsf.o
OBJS-$(CONFIG_VP9_SUPERFRAME_SPLIT_BSF)   += vp9_superframe_split_bsf.o

# thread libraries
OBJS-$(HAVE_LIBC_MSVCRT)               += file_open.o
OBJS-$(HAVE_THREADS)                   += pthread.o pthread_slice.o pthread_frame.o

OBJS-$(CONFIG_FRAME_THREAD_ENCODER)    += frame_thread_encoder.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)           += avcodecres.o

SKIPHEADERS                            += %_tablegen.h                  \
                                          %_tables.h                    \
                                          fft-internal.h                \
                                          tableprint.h                  \
                                          tableprint_vlc.h              \
                                          aaccoder_twoloop.h            \
                                          aaccoder_trellis.h            \
                                          aacenc_quantization.h         \
                                          aacenc_quantization_misc.h    \
                                          $(ARCH)/vp56_arith.h          \

SKIPHEADERS-$(CONFIG_AMF)              += amfenc.h
SKIPHEADERS-$(CONFIG_D3D11VA)          += d3d11va.h dxva2_internal.h
SKIPHEADERS-$(CONFIG_DXVA2)            += dxva2.h dxva2_internal.h
SKIPHEADERS-$(CONFIG_JNI)              += ffjni.h
SKIPHEADERS-$(CONFIG_LIBVPX)           += libvpx.h
SKIPHEADERS-$(CONFIG_LIBWEBP_ENCODER)  += libwebpenc_common.h
SKIPHEADERS-$(CONFIG_MEDIACODEC)       += mediacodecdec_common.h mediacodec_surface.h mediacodec_wrapper.h mediacodec_sw_buffer.h
SKIPHEADERS-$(CONFIG_MEDIAFOUNDATION)  += mf_utils.h
SKIPHEADERS-$(CONFIG_NVDEC)            += nvdec.h
SKIPHEADERS-$(CONFIG_NVENC)            += nvenc.h
SKIPHEADERS-$(CONFIG_QSV)              += qsv.h qsv_internal.h
SKIPHEADERS-$(CONFIG_QSVDEC)           += qsvdec.h
SKIPHEADERS-$(CONFIG_QSVENC)           += qsvenc.h
SKIPHEADERS-$(CONFIG_XVMC)             += xvmc.h
SKIPHEADERS-$(CONFIG_VAAPI)            += vaapi_decode.h vaapi_encode.h
SKIPHEADERS-$(CONFIG_VDPAU)            += vdpau.h vdpau_internal.h
SKIPHEADERS-$(CONFIG_VIDEOTOOLBOX)     += videotoolbox.h vt_internal.h
SKIPHEADERS-$(CONFIG_V4L2_M2M)         += v4l2_buffers.h v4l2_context.h v4l2_m2m.h

TESTPROGS = avpacket                                                    \
            celp_math                                                   \
            codec_desc                                                  \
            htmlsubtitles                                               \
            imgconvert                                                  \
            jpeg2000dwt                                                 \
            mathops                                                    \
            options                                                     \
            mjpegenc_huffman                                            \
            utils                                                       \

TESTPROGS-$(CONFIG_CABAC)                 += cabac
TESTPROGS-$(CONFIG_DCT)                   += avfft
TESTPROGS-$(CONFIG_FFT)                   += fft fft-fixed fft-fixed32
TESTPROGS-$(CONFIG_GOLOMB)                += golomb
TESTPROGS-$(CONFIG_IDCTDSP)               += dct
TESTPROGS-$(CONFIG_IIRFILTER)             += iirfilter
TESTPROGS-$(HAVE_MMX)                     += motion
TESTPROGS-$(CONFIG_MPEGVIDEO)             += mpeg12framerate
TESTPROGS-$(CONFIG_H264_METADATA_BSF)     += h264_levels
TESTPROGS-$(CONFIG_HEVC_METADATA_BSF)     += h265_levels
TESTPROGS-$(CONFIG_RANGECODER)            += rangecoder
TESTPROGS-$(CONFIG_SNOW_ENCODER)          += snowenc

TESTOBJS = dctref.o

TOOLS = fourcc2pixfmt

HOSTPROGS = aacps_tablegen                                              \
            aacps_fixed_tablegen                                        \
            cbrt_tablegen                                               \
            cbrt_fixed_tablegen                                         \
            cos_tablegen                                                \
            dv_tablegen                                                 \
            motionpixels_tablegen                                       \
            mpegaudio_tablegen                                          \
            pcm_tablegen                                                \
            qdm2_tablegen                                               \
            sinewin_tablegen                                            \
            sinewin_fixed_tablegen                                      \

CLEANFILES = *_tables.c *_tables.h *_tablegen$(HOSTEXESUF)

$(SUBDIR)tests/dct$(EXESUF): $(SUBDIR)dctref.o $(SUBDIR)aandcttab.o
$(SUBDIR)dv_tablegen$(HOSTEXESUF): $(SUBDIR)dvdata_host.o

TRIG_TABLES  = cos cos_fixed sin
TRIG_TABLES := $(TRIG_TABLES:%=$(SUBDIR)%_tables.c)

$(TRIG_TABLES): $(SUBDIR)%_tables.c: $(SUBDIR)cos_tablegen$(HOSTEXESUF)
	$(M)./$< $* > $@

ifdef CONFIG_SMALL
$(SUBDIR)%_tablegen$(HOSTEXESUF): HOSTCFLAGS += -DCONFIG_SMALL=1
else
$(SUBDIR)%_tablegen$(HOSTEXESUF): HOSTCFLAGS += -DCONFIG_SMALL=0
endif

GEN_HEADERS = cbrt_tables.h cbrt_fixed_tables.h aacps_tables.h aacps_fixed_tables.h \
              dv_tables.h     \
              sinewin_tables.h sinewin_fixed_tables.h mpegaudio_tables.h motionpixels_tables.h \
              pcm_tables.h qdm2_tables.h
GEN_HEADERS := $(addprefix $(SUBDIR), $(GEN_HEADERS))

$(GEN_HEADERS): $(SUBDIR)%_tables.h: $(SUBDIR)%_tablegen$(HOSTEXESUF)
	$(M)./$< > $@

ifdef CONFIG_HARDCODED_TABLES
$(SUBDIR)cbrt_data.o: $(SUBDIR)cbrt_tables.h
$(SUBDIR)cbrt_data_fixed.o: $(SUBDIR)cbrt_fixed_tables.h
$(SUBDIR)aacps_float.o: $(SUBDIR)aacps_tables.h
$(SUBDIR)aacps_fixed.o: $(SUBDIR)aacps_fixed_tables.h
$(SUBDIR)aactab_fixed.o: $(SUBDIR)aac_fixed_tables.h
$(SUBDIR)dvenc.o: $(SUBDIR)dv_tables.h
$(SUBDIR)motionpixels.o: $(SUBDIR)motionpixels_tables.h
$(SUBDIR)mpegaudiodec_fixed.o: $(SUBDIR)mpegaudio_tables.h
$(SUBDIR)mpegaudiodec_float.o: $(SUBDIR)mpegaudio_tables.h
$(SUBDIR)pcm.o: $(SUBDIR)pcm_tables.h
$(SUBDIR)qdm2.o: $(SUBDIR)qdm2_tables.h
$(SUBDIR)sinewin.o: $(SUBDIR)sinewin_tables.h
$(SUBDIR)sinewin_fixed.o: $(SUBDIR)sinewin_fixed_tables.h
endif
