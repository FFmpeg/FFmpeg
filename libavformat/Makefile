NAME = avformat
DESC = FFmpeg container format library

HEADERS = avformat.h                                                    \
          avio.h                                                        \
          version.h                                                     \
          version_major.h                                               \

OBJS = allformats.o         \
       avformat.o           \
       avio.o               \
       aviobuf.o            \
       demux.o              \
       demux_utils.o        \
       dump.o               \
       format.o             \
       id3v1.o              \
       id3v2.o              \
       isom_tags.o          \
       metadata.o           \
       mux.o                \
       mux_utils.o          \
       options.o            \
       os_support.o         \
       protocols.o          \
       riff.o               \
       sdp.o                \
       seek.o               \
       url.o                \
       utils.o              \
       version.o            \

OBJS-$(HAVE_LIBC_MSVCRT)                 += file_open.o

# subsystems
OBJS-$(CONFIG_ISO_MEDIA)                 += isom.o
OBJS-$(CONFIG_NETWORK)                   += network.o
OBJS-$(CONFIG_RIFFDEC)                   += riffdec.o
OBJS-$(CONFIG_RIFFENC)                   += riffenc.o
OBJS-$(CONFIG_RTPDEC)                    += rdt.o                       \
                                            rtp.o                       \
                                            rtpdec.o                    \
                                            rtpdec_ac3.o                \
                                            rtpdec_amr.o                \
                                            rtpdec_asf.o                \
                                            rtpdec_dv.o                 \
                                            rtpdec_g726.o               \
                                            rtpdec_h261.o               \
                                            rtpdec_h263.o               \
                                            rtpdec_h263_rfc2190.o       \
                                            rtpdec_h264.o               \
                                            rtpdec_hevc.o               \
                                            rtpdec_ilbc.o               \
                                            rtpdec_jpeg.o               \
                                            rtpdec_latm.o               \
                                            rtpdec_mpa_robust.o         \
                                            rtpdec_mpeg12.o             \
                                            rtpdec_mpeg4.o              \
                                            rtpdec_mpegts.o             \
                                            rtpdec_qcelp.o              \
                                            rtpdec_qdm2.o               \
                                            rtpdec_qt.o                 \
                                            rtpdec_rfc4175.o            \
                                            rtpdec_svq3.o               \
                                            rtpdec_vc2hq.o              \
                                            rtpdec_vp8.o                \
                                            rtpdec_vp9.o                \
                                            rtpdec_xiph.o
OBJS-$(CONFIG_RTPENC_CHAIN)              += rtpenc_chain.o rtp.o
OBJS-$(CONFIG_SRTP)                      += srtp.o

# muxers/demuxers
OBJS-$(CONFIG_A64_MUXER)                 += a64.o rawenc.o
OBJS-$(CONFIG_AA_DEMUXER)                += aadec.o
OBJS-$(CONFIG_AAC_DEMUXER)               += aacdec.o apetag.o img2.o rawdec.o
OBJS-$(CONFIG_AAX_DEMUXER)               += aaxdec.o
OBJS-$(CONFIG_AC3_DEMUXER)               += ac3dec.o rawdec.o
OBJS-$(CONFIG_AC3_MUXER)                 += rawenc.o
OBJS-$(CONFIG_ACE_DEMUXER)               += acedec.o
OBJS-$(CONFIG_ACM_DEMUXER)               += acm.o rawdec.o
OBJS-$(CONFIG_ACT_DEMUXER)               += act.o
OBJS-$(CONFIG_ADF_DEMUXER)               += bintext.o sauce.o
OBJS-$(CONFIG_ADP_DEMUXER)               += adp.o
OBJS-$(CONFIG_ADS_DEMUXER)               += ads.o
OBJS-$(CONFIG_ADTS_MUXER)                += adtsenc.o apetag.o img2.o \
                                            id3v2enc.o
OBJS-$(CONFIG_ADX_DEMUXER)               += adxdec.o
OBJS-$(CONFIG_ADX_MUXER)                 += rawenc.o
OBJS-$(CONFIG_AEA_DEMUXER)               += aea.o pcm.o
OBJS-$(CONFIG_AFC_DEMUXER)               += afc.o
OBJS-$(CONFIG_AIFF_DEMUXER)              += aiffdec.o aiff.o pcm.o \
                                            mov_chan.o replaygain.o
OBJS-$(CONFIG_AIFF_MUXER)                += aiffenc.o aiff.o id3v2enc.o
OBJS-$(CONFIG_AIX_DEMUXER)               += aixdec.o
OBJS-$(CONFIG_ALP_DEMUXER)               += alp.o
OBJS-$(CONFIG_ALP_MUXER)                 += alp.o rawenc.o
OBJS-$(CONFIG_AMR_DEMUXER)               += amr.o rawdec.o
OBJS-$(CONFIG_AMR_MUXER)                 += amr.o rawenc.o
OBJS-$(CONFIG_AMRNB_DEMUXER)             += amr.o rawdec.o
OBJS-$(CONFIG_AMRWB_DEMUXER)             += amr.o rawdec.o
OBJS-$(CONFIG_AMV_MUXER)                 += amvenc.o
OBJS-$(CONFIG_ANM_DEMUXER)               += anm.o
OBJS-$(CONFIG_APC_DEMUXER)               += apc.o
OBJS-$(CONFIG_APE_DEMUXER)               += ape.o apetag.o img2.o
OBJS-$(CONFIG_APM_DEMUXER)               += apm.o
OBJS-$(CONFIG_APM_MUXER)                 += apm.o rawenc.o
OBJS-$(CONFIG_APNG_DEMUXER)              += apngdec.o
OBJS-$(CONFIG_APNG_MUXER)                += apngenc.o
OBJS-$(CONFIG_APTX_DEMUXER)              += aptxdec.o
OBJS-$(CONFIG_APTX_MUXER)                += rawenc.o
OBJS-$(CONFIG_APTX_HD_DEMUXER)           += aptxdec.o
OBJS-$(CONFIG_APTX_HD_MUXER)             += rawenc.o
OBJS-$(CONFIG_AQTITLE_DEMUXER)           += aqtitledec.o subtitles.o
OBJS-$(CONFIG_ARGO_ASF_DEMUXER)          += argo_asf.o
OBJS-$(CONFIG_ARGO_ASF_MUXER)            += argo_asf.o
OBJS-$(CONFIG_ARGO_BRP_DEMUXER)          += argo_brp.o argo_asf.o
OBJS-$(CONFIG_ARGO_CVG_DEMUXER)          += argo_cvg.o
OBJS-$(CONFIG_ARGO_CVG_MUXER)            += argo_cvg.o
OBJS-$(CONFIG_ASF_DEMUXER)               += asfdec_f.o asf.o asfcrypt.o \
                                            asf_tags.o avlanguage.o
OBJS-$(CONFIG_ASF_O_DEMUXER)             += asfdec_o.o asf.o asfcrypt.o \
                                            asf_tags.o avlanguage.o
OBJS-$(CONFIG_ASF_MUXER)                 += asfenc.o asf.o avlanguage.o
OBJS-$(CONFIG_ASS_DEMUXER)               += assdec.o subtitles.o
OBJS-$(CONFIG_ASS_MUXER)                 += assenc.o
OBJS-$(CONFIG_AST_DEMUXER)               += ast.o astdec.o
OBJS-$(CONFIG_AST_MUXER)                 += ast.o astenc.o
OBJS-$(CONFIG_AU_DEMUXER)                += au.o pcm.o
OBJS-$(CONFIG_AU_MUXER)                  += au.o rawenc.o
OBJS-$(CONFIG_AVI_DEMUXER)               += avidec.o
OBJS-$(CONFIG_AVI_MUXER)                 += avienc.o mpegtsenc.o avlanguage.o rawutils.o
OBJS-$(CONFIG_AVM2_MUXER)                += swfenc.o swf.o
OBJS-$(CONFIG_AVR_DEMUXER)               += avr.o pcm.o
OBJS-$(CONFIG_AVS_DEMUXER)               += avs.o voc_packet.o voc.o
OBJS-$(CONFIG_AVS2_DEMUXER)              += avs2dec.o rawdec.o
OBJS-$(CONFIG_AVS2_MUXER)                += rawenc.o
OBJS-$(CONFIG_AVS3_DEMUXER)              += avs3dec.o rawdec.o
OBJS-$(CONFIG_AVS3_MUXER)                += rawenc.o
OBJS-$(CONFIG_BETHSOFTVID_DEMUXER)       += bethsoftvid.o
OBJS-$(CONFIG_BFI_DEMUXER)               += bfi.o
OBJS-$(CONFIG_BINK_DEMUXER)              += bink.o
OBJS-$(CONFIG_BINKA_DEMUXER)             += binka.o
OBJS-$(CONFIG_BINTEXT_DEMUXER)           += bintext.o sauce.o
OBJS-$(CONFIG_BIT_DEMUXER)               += bit.o
OBJS-$(CONFIG_BIT_MUXER)                 += bit.o
OBJS-$(CONFIG_BITPACKED_DEMUXER)         += rawvideodec.o
OBJS-$(CONFIG_BMV_DEMUXER)               += bmv.o
OBJS-$(CONFIG_BOA_DEMUXER)               += boadec.o
OBJS-$(CONFIG_BFSTM_DEMUXER)             += brstm.o
OBJS-$(CONFIG_BRSTM_DEMUXER)             += brstm.o
OBJS-$(CONFIG_C93_DEMUXER)               += c93.o voc_packet.o voc.o
OBJS-$(CONFIG_CAF_DEMUXER)               += cafdec.o caf.o mov_chan.o mov_esds.o
OBJS-$(CONFIG_CAF_MUXER)                 += cafenc.o caf.o riff.o
OBJS-$(CONFIG_CAVSVIDEO_DEMUXER)         += cavsvideodec.o rawdec.o
OBJS-$(CONFIG_CAVSVIDEO_MUXER)           += rawenc.o
OBJS-$(CONFIG_CDG_DEMUXER)               += cdg.o
OBJS-$(CONFIG_CDXL_DEMUXER)              += cdxl.o
OBJS-$(CONFIG_CINE_DEMUXER)              += cinedec.o
OBJS-$(CONFIG_CODEC2_DEMUXER)            += codec2.o pcm.o
OBJS-$(CONFIG_CODEC2_MUXER)              += codec2.o rawenc.o
OBJS-$(CONFIG_CODEC2RAW_DEMUXER)         += codec2.o pcm.o
OBJS-$(CONFIG_CODEC2RAW_MUXER)           += rawenc.o
OBJS-$(CONFIG_CONCAT_DEMUXER)            += concatdec.o
OBJS-$(CONFIG_CRC_MUXER)                 += crcenc.o
OBJS-$(CONFIG_DATA_DEMUXER)              += rawdec.o
OBJS-$(CONFIG_DATA_MUXER)                += rawenc.o
OBJS-$(CONFIG_DASH_MUXER)                += dash.o dashenc.o hlsplaylist.o
OBJS-$(CONFIG_DASH_DEMUXER)              += dash.o dashdec.o
OBJS-$(CONFIG_DAUD_DEMUXER)              += dauddec.o
OBJS-$(CONFIG_DAUD_MUXER)                += daudenc.o
OBJS-$(CONFIG_DCSTR_DEMUXER)             += dcstr.o
OBJS-$(CONFIG_DERF_DEMUXER)              += derf.o pcm.o
OBJS-$(CONFIG_DFA_DEMUXER)               += dfa.o
OBJS-$(CONFIG_DFPWM_DEMUXER)             += dfpwmdec.o pcm.o
OBJS-$(CONFIG_DFPWM_MUXER)               += rawenc.o
OBJS-$(CONFIG_DHAV_DEMUXER)              += dhav.o
OBJS-$(CONFIG_DIRAC_DEMUXER)             += diracdec.o rawdec.o
OBJS-$(CONFIG_DIRAC_MUXER)               += rawenc.o
OBJS-$(CONFIG_DNXHD_DEMUXER)             += dnxhddec.o rawdec.o
OBJS-$(CONFIG_DNXHD_MUXER)               += rawenc.o
OBJS-$(CONFIG_DSF_DEMUXER)               += dsfdec.o
OBJS-$(CONFIG_DSICIN_DEMUXER)            += dsicin.o
OBJS-$(CONFIG_DSS_DEMUXER)               += dss.o
OBJS-$(CONFIG_DTSHD_DEMUXER)             += dtshddec.o
OBJS-$(CONFIG_DTS_DEMUXER)               += dtsdec.o rawdec.o
OBJS-$(CONFIG_DTS_MUXER)                 += rawenc.o
OBJS-$(CONFIG_DV_DEMUXER)                += dv.o
OBJS-$(CONFIG_DV_MUXER)                  += dvenc.o
OBJS-$(CONFIG_DVBSUB_DEMUXER)            += dvbsub.o rawdec.o
OBJS-$(CONFIG_DVBTXT_DEMUXER)            += dvbtxt.o rawdec.o
OBJS-$(CONFIG_DXA_DEMUXER)               += dxa.o
OBJS-$(CONFIG_EA_CDATA_DEMUXER)          += eacdata.o
OBJS-$(CONFIG_EA_DEMUXER)                += electronicarts.o
OBJS-$(CONFIG_EAC3_DEMUXER)              += ac3dec.o rawdec.o
OBJS-$(CONFIG_EAC3_MUXER)                += rawenc.o
OBJS-$(CONFIG_EPAF_DEMUXER)              += epafdec.o pcm.o
OBJS-$(CONFIG_FFMETADATA_DEMUXER)        += ffmetadec.o
OBJS-$(CONFIG_FFMETADATA_MUXER)          += ffmetaenc.o
OBJS-$(CONFIG_FIFO_MUXER)                += fifo.o
OBJS-$(CONFIG_FIFO_TEST_MUXER)           += fifo_test.o
OBJS-$(CONFIG_FILMSTRIP_DEMUXER)         += filmstripdec.o
OBJS-$(CONFIG_FILMSTRIP_MUXER)           += filmstripenc.o rawenc.o
OBJS-$(CONFIG_FITS_DEMUXER)              += fitsdec.o
OBJS-$(CONFIG_FITS_MUXER)                += fitsenc.o
OBJS-$(CONFIG_FLAC_DEMUXER)              += flacdec.o rawdec.o \
                                            flac_picture.o   \
                                            oggparsevorbis.o \
                                            replaygain.o     \
                                            vorbiscomment.o
OBJS-$(CONFIG_FLAC_MUXER)                += flacenc.o flacenc_header.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_FLIC_DEMUXER)              += flic.o
OBJS-$(CONFIG_FLV_DEMUXER)               += flvdec.o
OBJS-$(CONFIG_LIVE_FLV_DEMUXER)          += flvdec.o
OBJS-$(CONFIG_FLV_MUXER)                 += flvenc.o avc.o
OBJS-$(CONFIG_FOURXM_DEMUXER)            += 4xm.o
OBJS-$(CONFIG_FRAMECRC_MUXER)            += framecrcenc.o framehash.o
OBJS-$(CONFIG_FRAMEHASH_MUXER)           += hashenc.o framehash.o
OBJS-$(CONFIG_FRAMEMD5_MUXER)            += hashenc.o framehash.o
OBJS-$(CONFIG_FRM_DEMUXER)               += frmdec.o
OBJS-$(CONFIG_FSB_DEMUXER)               += fsb.o
OBJS-$(CONFIG_FWSE_DEMUXER)              += fwse.o pcm.o
OBJS-$(CONFIG_GIF_MUXER)                 += gif.o
OBJS-$(CONFIG_GIF_DEMUXER)               += gifdec.o
OBJS-$(CONFIG_GSM_DEMUXER)               += gsmdec.o
OBJS-$(CONFIG_GSM_MUXER)                 += rawenc.o
OBJS-$(CONFIG_GXF_DEMUXER)               += gxf.o
OBJS-$(CONFIG_GXF_MUXER)                 += gxfenc.o
OBJS-$(CONFIG_G722_DEMUXER)              += g722.o rawdec.o
OBJS-$(CONFIG_G722_MUXER)                += rawenc.o
OBJS-$(CONFIG_G723_1_DEMUXER)            += g723_1.o
OBJS-$(CONFIG_G723_1_MUXER)              += rawenc.o
OBJS-$(CONFIG_G726_DEMUXER)              += g726.o
OBJS-$(CONFIG_G726_MUXER)                += rawenc.o
OBJS-$(CONFIG_G726LE_DEMUXER)            += g726.o
OBJS-$(CONFIG_G726LE_MUXER)              += rawenc.o
OBJS-$(CONFIG_G729_DEMUXER)              += g729dec.o
OBJS-$(CONFIG_GDV_DEMUXER)               += gdv.o
OBJS-$(CONFIG_GENH_DEMUXER)              += genh.o
OBJS-$(CONFIG_H261_DEMUXER)              += h261dec.o rawdec.o
OBJS-$(CONFIG_H261_MUXER)                += rawenc.o
OBJS-$(CONFIG_H263_DEMUXER)              += h263dec.o rawdec.o
OBJS-$(CONFIG_H263_MUXER)                += rawenc.o
OBJS-$(CONFIG_H264_DEMUXER)              += h264dec.o rawdec.o
OBJS-$(CONFIG_H264_MUXER)                += rawenc.o
OBJS-$(CONFIG_HASH_MUXER)                += hashenc.o
OBJS-$(CONFIG_HCA_DEMUXER)               += hca.o
OBJS-$(CONFIG_HCOM_DEMUXER)              += hcom.o pcm.o
OBJS-$(CONFIG_HDS_MUXER)                 += hdsenc.o
OBJS-$(CONFIG_HEVC_DEMUXER)              += hevcdec.o rawdec.o
OBJS-$(CONFIG_HEVC_MUXER)                += rawenc.o
OBJS-$(CONFIG_HLS_DEMUXER)               += hls.o hls_sample_encryption.o
OBJS-$(CONFIG_HLS_MUXER)                 += hlsenc.o hlsplaylist.o avc.o
OBJS-$(CONFIG_HNM_DEMUXER)               += hnm.o
OBJS-$(CONFIG_ICO_DEMUXER)               += icodec.o
OBJS-$(CONFIG_ICO_MUXER)                 += icoenc.o
OBJS-$(CONFIG_IDCIN_DEMUXER)             += idcin.o
OBJS-$(CONFIG_IDF_DEMUXER)               += bintext.o sauce.o
OBJS-$(CONFIG_IFF_DEMUXER)               += iff.o
OBJS-$(CONFIG_IFV_DEMUXER)               += ifv.o
OBJS-$(CONFIG_ILBC_DEMUXER)              += ilbc.o
OBJS-$(CONFIG_ILBC_MUXER)                += ilbc.o rawenc.o
OBJS-$(CONFIG_IMAGE2_DEMUXER)            += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE2_MUXER)              += img2enc.o img2.o
OBJS-$(CONFIG_IMAGE2PIPE_DEMUXER)        += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE2PIPE_MUXER)          += img2enc.o img2.o
OBJS-$(CONFIG_IMAGE2_ALIAS_PIX_DEMUXER)  += img2_alias_pix.o
OBJS-$(CONFIG_IMAGE2_BRENDER_PIX_DEMUXER) += img2_brender_pix.o
OBJS-$(CONFIG_IMAGE_BMP_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_CRI_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_DDS_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_DPX_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_EXR_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_GEM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_GIF_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_J2K_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_JPEG_PIPE_DEMUXER)    += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_JPEGLS_PIPE_DEMUXER)  += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_JPEGXL_PIPE_DEMUXER)  += img2dec.o img2.o jpegxl_probe.o
OBJS-$(CONFIG_IMAGE_PAM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PBM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PCX_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PGMYUV_PIPE_DEMUXER)  += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PGM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PGX_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PHOTOCD_PIPE_DEMUXER) += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PICTOR_PIPE_DEMUXER)  += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PNG_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PPM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_PSD_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_QDRAW_PIPE_DEMUXER)   += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_QOI_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_SGI_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_SVG_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_SUNRAST_PIPE_DEMUXER) += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_TIFF_PIPE_DEMUXER)    += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_WEBP_PIPE_DEMUXER)    += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_XBM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_XPM_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMAGE_XWD_PIPE_DEMUXER)     += img2dec.o img2.o
OBJS-$(CONFIG_IMF_DEMUXER)               += imfdec.o imf_cpl.o
OBJS-$(CONFIG_INGENIENT_DEMUXER)         += ingenientdec.o rawdec.o
OBJS-$(CONFIG_IPMOVIE_DEMUXER)           += ipmovie.o
OBJS-$(CONFIG_IPU_DEMUXER)               += ipudec.o rawdec.o
OBJS-$(CONFIG_IRCAM_DEMUXER)             += ircamdec.o ircam.o pcm.o
OBJS-$(CONFIG_IRCAM_MUXER)               += ircamenc.o ircam.o rawenc.o
OBJS-$(CONFIG_ISS_DEMUXER)               += iss.o
OBJS-$(CONFIG_IV8_DEMUXER)               += iv8.o
OBJS-$(CONFIG_IVF_DEMUXER)               += ivfdec.o
OBJS-$(CONFIG_IVF_MUXER)                 += ivfenc.o
OBJS-$(CONFIG_IVR_DEMUXER)               += rmdec.o rm.o rmsipr.o
OBJS-$(CONFIG_JACOSUB_DEMUXER)           += jacosubdec.o subtitles.o
OBJS-$(CONFIG_JACOSUB_MUXER)             += jacosubenc.o rawenc.o
OBJS-$(CONFIG_JV_DEMUXER)                += jvdec.o
OBJS-$(CONFIG_KUX_DEMUXER)               += flvdec.o
OBJS-$(CONFIG_KVAG_DEMUXER)              += kvag.o
OBJS-$(CONFIG_KVAG_MUXER)                += kvag.o rawenc.o
OBJS-$(CONFIG_LATM_MUXER)                += latmenc.o rawenc.o
OBJS-$(CONFIG_LMLM4_DEMUXER)             += lmlm4.o
OBJS-$(CONFIG_LOAS_DEMUXER)              += loasdec.o rawdec.o
OBJS-$(CONFIG_LUODAT_DEMUXER)            += luodatdec.o
OBJS-$(CONFIG_LRC_DEMUXER)               += lrcdec.o lrc.o subtitles.o
OBJS-$(CONFIG_LRC_MUXER)                 += lrcenc.o lrc.o
OBJS-$(CONFIG_LVF_DEMUXER)               += lvfdec.o
OBJS-$(CONFIG_LXF_DEMUXER)               += lxfdec.o
OBJS-$(CONFIG_M4V_DEMUXER)               += m4vdec.o rawdec.o
OBJS-$(CONFIG_M4V_MUXER)                 += rawenc.o
OBJS-$(CONFIG_MATROSKA_DEMUXER)          += matroskadec.o matroska.o  \
                                            flac_picture.o rmsipr.o \
                                            oggparsevorbis.o vorbiscomment.o \
                                            qtpalette.o replaygain.o dovi_isom.o
OBJS-$(CONFIG_MATROSKA_MUXER)            += matroskaenc.o matroska.o \
                                            av1.o avc.o hevc.o \
                                            flacenc_header.o avlanguage.o \
                                            vorbiscomment.o wv.o dovi_isom.o
OBJS-$(CONFIG_MCA_DEMUXER)               += mca.o
OBJS-$(CONFIG_MCC_DEMUXER)               += mccdec.o subtitles.o
OBJS-$(CONFIG_MD5_MUXER)                 += hashenc.o
OBJS-$(CONFIG_MGSTS_DEMUXER)             += mgsts.o
OBJS-$(CONFIG_MICRODVD_DEMUXER)          += microdvddec.o subtitles.o
OBJS-$(CONFIG_MICRODVD_MUXER)            += microdvdenc.o
OBJS-$(CONFIG_MJPEG_2000_DEMUXER)        += rawdec.o mj2kdec.o
OBJS-$(CONFIG_MJPEG_DEMUXER)             += rawdec.o
OBJS-$(CONFIG_MJPEG_MUXER)               += rawenc.o
OBJS-$(CONFIG_MLP_DEMUXER)               += rawdec.o mlpdec.o
OBJS-$(CONFIG_MLP_MUXER)                 += rawenc.o
OBJS-$(CONFIG_MLV_DEMUXER)               += mlvdec.o riffdec.o
OBJS-$(CONFIG_MM_DEMUXER)                += mm.o
OBJS-$(CONFIG_MMF_DEMUXER)               += mmf.o
OBJS-$(CONFIG_MMF_MUXER)                 += mmf.o rawenc.o
OBJS-$(CONFIG_MODS_DEMUXER)              += mods.o
OBJS-$(CONFIG_MOFLEX_DEMUXER)            += moflex.o
OBJS-$(CONFIG_MOV_DEMUXER)               += mov.o mov_chan.o mov_esds.o \
                                            qtpalette.o replaygain.o dovi_isom.o
OBJS-$(CONFIG_MOV_MUXER)                 += movenc.o av1.o avc.o hevc.o vpcc.o \
                                            movenchint.o mov_chan.o rtp.o \
                                            movenccenc.o movenc_ttml.o rawutils.o \
                                            dovi_isom.o
OBJS-$(CONFIG_MP2_MUXER)                 += rawenc.o
OBJS-$(CONFIG_MP3_DEMUXER)               += mp3dec.o replaygain.o
OBJS-$(CONFIG_MP3_MUXER)                 += mp3enc.o rawenc.o id3v2enc.o
OBJS-$(CONFIG_MPC_DEMUXER)               += mpc.o apetag.o img2.o
OBJS-$(CONFIG_MPC8_DEMUXER)              += mpc8.o apetag.o img2.o
OBJS-$(CONFIG_MPEG1SYSTEM_MUXER)         += mpegenc.o
OBJS-$(CONFIG_MPEG1VCD_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEG1VIDEO_MUXER)          += rawenc.o
OBJS-$(CONFIG_MPEG2DVD_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEG2SVCD_MUXER)           += mpegenc.o
OBJS-$(CONFIG_MPEG2VIDEO_MUXER)          += rawenc.o
OBJS-$(CONFIG_MPEG2VOB_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEGPS_DEMUXER)            += mpeg.o
OBJS-$(CONFIG_MPEGTS_DEMUXER)            += mpegts.o
OBJS-$(CONFIG_MPEGTS_MUXER)              += mpegtsenc.o
OBJS-$(CONFIG_MPEGVIDEO_DEMUXER)         += mpegvideodec.o rawdec.o
OBJS-$(CONFIG_MPJPEG_DEMUXER)            += mpjpegdec.o
OBJS-$(CONFIG_MPJPEG_MUXER)              += mpjpeg.o
OBJS-$(CONFIG_MPL2_DEMUXER)              += mpl2dec.o subtitles.o
OBJS-$(CONFIG_MSF_DEMUXER)               += msf.o
OBJS-$(CONFIG_MPSUB_DEMUXER)             += mpsubdec.o subtitles.o
OBJS-$(CONFIG_MSNWC_TCP_DEMUXER)         += msnwc_tcp.o
OBJS-$(CONFIG_MSP_DEMUXER)               += mspdec.o
OBJS-$(CONFIG_MTAF_DEMUXER)              += mtaf.o
OBJS-$(CONFIG_MTV_DEMUXER)               += mtv.o
OBJS-$(CONFIG_MUSX_DEMUXER)              += musx.o
OBJS-$(CONFIG_MV_DEMUXER)                += mvdec.o
OBJS-$(CONFIG_MVI_DEMUXER)               += mvi.o
OBJS-$(CONFIG_MXF_DEMUXER)               += mxfdec.o mxf.o avlanguage.o
OBJS-$(CONFIG_MXF_MUXER)                 += mxfenc.o mxf.o avc.o
OBJS-$(CONFIG_MXG_DEMUXER)               += mxg.o
OBJS-$(CONFIG_NC_DEMUXER)                += ncdec.o
OBJS-$(CONFIG_NISTSPHERE_DEMUXER)        += nistspheredec.o pcm.o
OBJS-$(CONFIG_NSP_DEMUXER)               += nspdec.o pcm.o
OBJS-$(CONFIG_NSV_DEMUXER)               += nsvdec.o
OBJS-$(CONFIG_NULL_MUXER)                += nullenc.o
OBJS-$(CONFIG_NUT_DEMUXER)               += nutdec.o nut.o
OBJS-$(CONFIG_NUT_MUXER)                 += nutenc.o nut.o
OBJS-$(CONFIG_NUV_DEMUXER)               += nuv.o
OBJS-$(CONFIG_AV1_DEMUXER)               += av1dec.o
OBJS-$(CONFIG_OBU_DEMUXER)               += av1dec.o
OBJS-$(CONFIG_OBU_MUXER)                 += rawenc.o
OBJS-$(CONFIG_OGG_DEMUXER)               += oggdec.o         \
                                            oggparsecelt.o   \
                                            oggparsedirac.o  \
                                            oggparseflac.o   \
                                            oggparseogm.o    \
                                            oggparseopus.o   \
                                            oggparseskeleton.o \
                                            oggparsespeex.o  \
                                            oggparsetheora.o \
                                            oggparsevorbis.o \
                                            oggparsevp8.o    \
                                            replaygain.o     \
                                            vorbiscomment.o  \
                                            flac_picture.o
OBJS-$(CONFIG_OGA_MUXER)                 += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_OGG_MUXER)                 += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_OGV_MUXER)                 += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_OMA_DEMUXER)               += omadec.o pcm.o oma.o
OBJS-$(CONFIG_OMA_MUXER)                 += omaenc.o rawenc.o oma.o id3v2enc.o
OBJS-$(CONFIG_OPUS_MUXER)                += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_PAF_DEMUXER)               += paf.o
OBJS-$(CONFIG_PCM_ALAW_DEMUXER)          += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_ALAW_MUXER)            += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F32BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_F32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F32LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_F32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F64BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_F64BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F64LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_F64LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_MULAW_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_MULAW_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S16BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S16BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S16LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S16LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S24BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S24BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S24LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S24LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S32BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S32LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S8_DEMUXER)            += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_S8_MUXER)              += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U16BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U16BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U16LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U16LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U24BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U24BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U24LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U24LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U32BE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U32LE_DEMUXER)         += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U8_DEMUXER)            += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_U8_MUXER)              += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_VIDC_DEMUXER)          += pcmdec.o pcm.o
OBJS-$(CONFIG_PCM_VIDC_MUXER)            += pcmenc.o rawenc.o
OBJS-$(CONFIG_PJS_DEMUXER)               += pjsdec.o subtitles.o
OBJS-$(CONFIG_PMP_DEMUXER)               += pmpdec.o
OBJS-$(CONFIG_PP_BNK_DEMUXER)            += pp_bnk.o
OBJS-$(CONFIG_PVA_DEMUXER)               += pva.o
OBJS-$(CONFIG_PVF_DEMUXER)               += pvfdec.o pcm.o
OBJS-$(CONFIG_QCP_DEMUXER)               += qcp.o
OBJS-$(CONFIG_R3D_DEMUXER)               += r3d.o
OBJS-$(CONFIG_RAWVIDEO_DEMUXER)          += rawvideodec.o
OBJS-$(CONFIG_RAWVIDEO_MUXER)            += rawenc.o
OBJS-$(CONFIG_REALTEXT_DEMUXER)          += realtextdec.o subtitles.o
OBJS-$(CONFIG_REDSPARK_DEMUXER)          += redspark.o
OBJS-$(CONFIG_RL2_DEMUXER)               += rl2.o
OBJS-$(CONFIG_RM_DEMUXER)                += rmdec.o rm.o rmsipr.o
OBJS-$(CONFIG_RM_MUXER)                  += rmenc.o rm.o
OBJS-$(CONFIG_ROQ_DEMUXER)               += idroqdec.o
OBJS-$(CONFIG_ROQ_MUXER)                 += idroqenc.o rawenc.o
OBJS-$(CONFIG_RSD_DEMUXER)               += rsd.o
OBJS-$(CONFIG_RPL_DEMUXER)               += rpl.o
OBJS-$(CONFIG_RSO_DEMUXER)               += rsodec.o rso.o pcm.o
OBJS-$(CONFIG_RSO_MUXER)                 += rsoenc.o rso.o rawenc.o
OBJS-$(CONFIG_RTP_MPEGTS_MUXER)          += rtpenc_mpegts.o
OBJS-$(CONFIG_RTP_MUXER)                 += rtp.o         \
                                            rtpenc_aac.o     \
                                            rtpenc_latm.o    \
                                            rtpenc_amr.o     \
                                            rtpenc_h261.o    \
                                            rtpenc_h263.o    \
                                            rtpenc_h263_rfc2190.o \
                                            rtpenc_h264_hevc.o    \
                                            rtpenc_jpeg.o \
                                            rtpenc_mpv.o     \
                                            rtpenc.o      \
                                            rtpenc_rfc4175.o    \
                                            rtpenc_vc2hq.o              \
                                            rtpenc_vp8.o  \
                                            rtpenc_vp9.o                \
                                            rtpenc_xiph.o \
                                            avc.o hevc.o
OBJS-$(CONFIG_RTSP_DEMUXER)              += rtsp.o rtspdec.o httpauth.o \
                                            urldecode.o
OBJS-$(CONFIG_RTSP_MUXER)                += rtsp.o rtspenc.o httpauth.o \
                                            urldecode.o
OBJS-$(CONFIG_S337M_DEMUXER)             += s337m.o spdif.o
OBJS-$(CONFIG_SAMI_DEMUXER)              += samidec.o subtitles.o
OBJS-$(CONFIG_SAP_DEMUXER)               += sapdec.o
OBJS-$(CONFIG_SAP_MUXER)                 += sapenc.o
OBJS-$(CONFIG_SBC_DEMUXER)               += sbcdec.o rawdec.o
OBJS-$(CONFIG_SBC_MUXER)                 += rawenc.o
OBJS-$(CONFIG_SBG_DEMUXER)               += sbgdec.o
OBJS-$(CONFIG_SCC_DEMUXER)               += sccdec.o subtitles.o
OBJS-$(CONFIG_SCC_MUXER)                 += sccenc.o
OBJS-$(CONFIG_SCD_DEMUXER)               += scd.o
OBJS-$(CONFIG_SDP_DEMUXER)               += rtsp.o
OBJS-$(CONFIG_SDR2_DEMUXER)              += sdr2.o
OBJS-$(CONFIG_SDS_DEMUXER)               += sdsdec.o
OBJS-$(CONFIG_SDX_DEMUXER)               += sdxdec.o pcm.o
OBJS-$(CONFIG_SEGAFILM_DEMUXER)          += segafilm.o
OBJS-$(CONFIG_SEGAFILM_MUXER)            += segafilmenc.o
OBJS-$(CONFIG_SEGMENT_MUXER)             += segment.o
OBJS-$(CONFIG_SER_DEMUXER)               += serdec.o
OBJS-$(CONFIG_SGA_DEMUXER)               += sga.o
OBJS-$(CONFIG_SHORTEN_DEMUXER)           += shortendec.o rawdec.o
OBJS-$(CONFIG_SIFF_DEMUXER)              += siff.o
OBJS-$(CONFIG_SIMBIOSIS_IMX_DEMUXER)     += imx.o
OBJS-$(CONFIG_SLN_DEMUXER)               += pcmdec.o pcm.o
OBJS-$(CONFIG_SMACKER_DEMUXER)           += smacker.o
OBJS-$(CONFIG_SMJPEG_DEMUXER)            += smjpegdec.o smjpeg.o
OBJS-$(CONFIG_SMJPEG_MUXER)              += smjpegenc.o smjpeg.o
OBJS-$(CONFIG_SMOOTHSTREAMING_MUXER)     += smoothstreamingenc.o
OBJS-$(CONFIG_SMUSH_DEMUXER)             += smush.o
OBJS-$(CONFIG_SOL_DEMUXER)               += sol.o pcm.o
OBJS-$(CONFIG_SOX_DEMUXER)               += soxdec.o pcm.o
OBJS-$(CONFIG_SOX_MUXER)                 += soxenc.o rawenc.o
OBJS-$(CONFIG_SPDIF_DEMUXER)             += spdif.o spdifdec.o
OBJS-$(CONFIG_SPDIF_MUXER)               += spdif.o spdifenc.o
OBJS-$(CONFIG_SPEEX_MUXER)               += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_SRT_DEMUXER)               += srtdec.o subtitles.o
OBJS-$(CONFIG_SRT_MUXER)                 += srtenc.o
OBJS-$(CONFIG_STL_DEMUXER)               += stldec.o subtitles.o
OBJS-$(CONFIG_STR_DEMUXER)               += psxstr.o
OBJS-$(CONFIG_STREAMHASH_MUXER)          += hashenc.o
OBJS-$(CONFIG_STREAM_SEGMENT_MUXER)      += segment.o
OBJS-$(CONFIG_SUBVIEWER1_DEMUXER)        += subviewer1dec.o subtitles.o
OBJS-$(CONFIG_SUBVIEWER_DEMUXER)         += subviewerdec.o subtitles.o
OBJS-$(CONFIG_SUP_DEMUXER)               += supdec.o
OBJS-$(CONFIG_SUP_MUXER)                 += supenc.o
OBJS-$(CONFIG_SVAG_DEMUXER)              += svag.o
OBJS-$(CONFIG_SVS_DEMUXER)               += svs.o
OBJS-$(CONFIG_SWF_DEMUXER)               += swfdec.o swf.o
OBJS-$(CONFIG_SWF_MUXER)                 += swfenc.o swf.o
OBJS-$(CONFIG_TAK_DEMUXER)               += takdec.o apetag.o img2.o rawdec.o
OBJS-$(CONFIG_TEDCAPTIONS_DEMUXER)       += tedcaptionsdec.o subtitles.o
OBJS-$(CONFIG_TEE_MUXER)                 += tee.o tee_common.o
OBJS-$(CONFIG_THP_DEMUXER)               += thp.o
OBJS-$(CONFIG_THREEDOSTR_DEMUXER)        += 3dostr.o
OBJS-$(CONFIG_TIERTEXSEQ_DEMUXER)        += tiertexseq.o
OBJS-$(CONFIG_MKVTIMESTAMP_V2_MUXER)     += mkvtimestamp_v2.o
OBJS-$(CONFIG_TMV_DEMUXER)               += tmv.o
OBJS-$(CONFIG_TRUEHD_DEMUXER)            += rawdec.o mlpdec.o
OBJS-$(CONFIG_TRUEHD_MUXER)              += rawenc.o
OBJS-$(CONFIG_TTA_DEMUXER)               += tta.o apetag.o img2.o
OBJS-$(CONFIG_TTA_MUXER)                 += ttaenc.o apetag.o img2.o
OBJS-$(CONFIG_TTML_MUXER)                += ttmlenc.o
OBJS-$(CONFIG_TTY_DEMUXER)               += tty.o sauce.o
OBJS-$(CONFIG_TY_DEMUXER)                += ty.o
OBJS-$(CONFIG_TXD_DEMUXER)               += txd.o
OBJS-$(CONFIG_UNCODEDFRAMECRC_MUXER)     += uncodedframecrcenc.o framehash.o
OBJS-$(CONFIG_V210_DEMUXER)              += rawvideodec.o
OBJS-$(CONFIG_V210X_DEMUXER)             += rawvideodec.o
OBJS-$(CONFIG_VAG_DEMUXER)               += vag.o
OBJS-$(CONFIG_VC1_DEMUXER)               += rawdec.o vc1dec.o
OBJS-$(CONFIG_VC1_MUXER)                 += rawenc.o
OBJS-$(CONFIG_VC1T_DEMUXER)              += vc1test.o
OBJS-$(CONFIG_VC1T_MUXER)                += vc1testenc.o
OBJS-$(CONFIG_VIVIDAS_DEMUXER)           += vividas.o
OBJS-$(CONFIG_VIVO_DEMUXER)              += vivo.o
OBJS-$(CONFIG_VMD_DEMUXER)               += sierravmd.o
OBJS-$(CONFIG_VOBSUB_DEMUXER)            += subtitles.o # mpeg demuxer is in the dependencies
OBJS-$(CONFIG_VOC_DEMUXER)               += vocdec.o voc_packet.o voc.o
OBJS-$(CONFIG_VOC_MUXER)                 += vocenc.o voc.o
OBJS-$(CONFIG_VPK_DEMUXER)               += vpk.o
OBJS-$(CONFIG_VPLAYER_DEMUXER)           += vplayerdec.o subtitles.o
OBJS-$(CONFIG_VQF_DEMUXER)               += vqf.o
OBJS-$(CONFIG_W64_DEMUXER)               += wavdec.o w64.o pcm.o
OBJS-$(CONFIG_W64_MUXER)                 += wavenc.o w64.o
OBJS-$(CONFIG_WAV_DEMUXER)               += wavdec.o pcm.o
OBJS-$(CONFIG_WAV_MUXER)                 += wavenc.o
OBJS-$(CONFIG_WC3_DEMUXER)               += wc3movie.o
OBJS-$(CONFIG_WEBM_MUXER)                += matroskaenc.o matroska.o \
                                            av1.o avlanguage.o
OBJS-$(CONFIG_WEBM_DASH_MANIFEST_MUXER)  += webmdashenc.o
OBJS-$(CONFIG_WEBM_CHUNK_MUXER)          += webm_chunk.o
OBJS-$(CONFIG_WEBP_MUXER)                += webpenc.o
OBJS-$(CONFIG_WEBVTT_DEMUXER)            += webvttdec.o subtitles.o
OBJS-$(CONFIG_WEBVTT_MUXER)              += webvttenc.o
OBJS-$(CONFIG_WSAUD_DEMUXER)             += westwood_aud.o
OBJS-$(CONFIG_WSAUD_MUXER)               += westwood_audenc.o
OBJS-$(CONFIG_WSD_DEMUXER)               += wsddec.o rawdec.o
OBJS-$(CONFIG_WSVQA_DEMUXER)             += westwood_vqa.o
OBJS-$(CONFIG_WTV_DEMUXER)               += wtvdec.o wtv_common.o \
                                            asf.o
OBJS-$(CONFIG_WTV_MUXER)                 += wtvenc.o wtv_common.o \
                                            asf.o
OBJS-$(CONFIG_WV_DEMUXER)                += wvdec.o wv.o apetag.o img2.o
OBJS-$(CONFIG_WVE_DEMUXER)               += wvedec.o pcm.o
OBJS-$(CONFIG_WV_MUXER)                  += wvenc.o wv.o apetag.o img2.o
OBJS-$(CONFIG_XA_DEMUXER)                += xa.o
OBJS-$(CONFIG_XBIN_DEMUXER)              += bintext.o sauce.o
OBJS-$(CONFIG_XMV_DEMUXER)               += xmv.o
OBJS-$(CONFIG_XVAG_DEMUXER)              += xvag.o
OBJS-$(CONFIG_XWMA_DEMUXER)              += xwma.o
OBJS-$(CONFIG_YOP_DEMUXER)               += yop.o
OBJS-$(CONFIG_YUV4MPEGPIPE_DEMUXER)      += yuv4mpegdec.o
OBJS-$(CONFIG_YUV4MPEGPIPE_MUXER)        += yuv4mpegenc.o

# external library muxers/demuxers
OBJS-$(CONFIG_AVISYNTH_DEMUXER)          += avisynth.o
OBJS-$(CONFIG_CHROMAPRINT_MUXER)         += chromaprint.o
OBJS-$(CONFIG_LIBGME_DEMUXER)            += libgme.o
OBJS-$(CONFIG_LIBMODPLUG_DEMUXER)        += libmodplug.o
OBJS-$(CONFIG_LIBOPENMPT_DEMUXER)        += libopenmpt.o
OBJS-$(CONFIG_VAPOURSYNTH_DEMUXER)       += vapoursynth.o

# protocols I/O
OBJS-$(CONFIG_ASYNC_PROTOCOL)            += async.o
OBJS-$(CONFIG_APPLEHTTP_PROTOCOL)        += hlsproto.o
OBJS-$(CONFIG_BLURAY_PROTOCOL)           += bluray.o
OBJS-$(CONFIG_CACHE_PROTOCOL)            += cache.o
OBJS-$(CONFIG_CONCAT_PROTOCOL)           += concat.o
OBJS-$(CONFIG_CONCATF_PROTOCOL)          += concat.o
OBJS-$(CONFIG_CRYPTO_PROTOCOL)           += crypto.o
OBJS-$(CONFIG_DATA_PROTOCOL)             += data_uri.o
OBJS-$(CONFIG_FFRTMPCRYPT_PROTOCOL)      += rtmpcrypt.o rtmpdigest.o rtmpdh.o
OBJS-$(CONFIG_FFRTMPHTTP_PROTOCOL)       += rtmphttp.o
OBJS-$(CONFIG_FILE_PROTOCOL)             += file.o
OBJS-$(CONFIG_FTP_PROTOCOL)              += ftp.o urldecode.o
OBJS-$(CONFIG_GOPHER_PROTOCOL)           += gopher.o
OBJS-$(CONFIG_GOPHERS_PROTOCOL)          += gopher.o
OBJS-$(CONFIG_HLS_PROTOCOL)              += hlsproto.o
OBJS-$(CONFIG_HTTP_PROTOCOL)             += http.o httpauth.o urldecode.o
OBJS-$(CONFIG_HTTPPROXY_PROTOCOL)        += http.o httpauth.o urldecode.o
OBJS-$(CONFIG_HTTPS_PROTOCOL)            += http.o httpauth.o urldecode.o
OBJS-$(CONFIG_ICECAST_PROTOCOL)          += icecast.o
OBJS-$(CONFIG_MD5_PROTOCOL)              += md5proto.o
OBJS-$(CONFIG_MMSH_PROTOCOL)             += mmsh.o mms.o asf_tags.o
OBJS-$(CONFIG_MMST_PROTOCOL)             += mmst.o mms.o asf_tags.o
OBJS-$(CONFIG_PIPE_PROTOCOL)             += file.o
OBJS-$(CONFIG_PROMPEG_PROTOCOL)          += prompeg.o
OBJS-$(CONFIG_RTMP_PROTOCOL)             += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTMPE_PROTOCOL)            += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTMPS_PROTOCOL)            += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTMPT_PROTOCOL)            += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTMPTE_PROTOCOL)           += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTMPTS_PROTOCOL)           += rtmpproto.o rtmpdigest.o rtmppkt.o
OBJS-$(CONFIG_RTP_PROTOCOL)              += rtpproto.o ip.o
OBJS-$(CONFIG_SCTP_PROTOCOL)             += sctp.o
OBJS-$(CONFIG_SRTP_PROTOCOL)             += srtpproto.o srtp.o
OBJS-$(CONFIG_SUBFILE_PROTOCOL)          += subfile.o
OBJS-$(CONFIG_TEE_PROTOCOL)              += teeproto.o tee_common.o
OBJS-$(CONFIG_TCP_PROTOCOL)              += tcp.o
OBJS-$(CONFIG_IPFS_PROTOCOL)             += ipfsgateway.o
OBJS-$(CONFIG_IPNS_PROTOCOL)             += ipfsgateway.o
TLS-OBJS-$(CONFIG_GNUTLS)                += tls_gnutls.o
TLS-OBJS-$(CONFIG_LIBTLS)                += tls_libtls.o
TLS-OBJS-$(CONFIG_MBEDTLS)               += tls_mbedtls.o
TLS-OBJS-$(CONFIG_OPENSSL)               += tls_openssl.o
TLS-OBJS-$(CONFIG_SECURETRANSPORT)       += tls_securetransport.o
TLS-OBJS-$(CONFIG_SCHANNEL)              += tls_schannel.o
OBJS-$(CONFIG_TLS_PROTOCOL)              += tls.o $(TLS-OBJS-yes)
OBJS-$(CONFIG_UDP_PROTOCOL)              += udp.o ip.o
OBJS-$(CONFIG_UDPLITE_PROTOCOL)          += udp.o ip.o
OBJS-$(CONFIG_UNIX_PROTOCOL)             += unix.o

# external library protocols
OBJS-$(CONFIG_LIBAMQP_PROTOCOL)          += libamqp.o urldecode.o
OBJS-$(CONFIG_LIBRIST_PROTOCOL)          += librist.o
OBJS-$(CONFIG_LIBRTMP_PROTOCOL)          += librtmp.o
OBJS-$(CONFIG_LIBRTMPE_PROTOCOL)         += librtmp.o
OBJS-$(CONFIG_LIBRTMPS_PROTOCOL)         += librtmp.o
OBJS-$(CONFIG_LIBRTMPT_PROTOCOL)         += librtmp.o
OBJS-$(CONFIG_LIBRTMPTE_PROTOCOL)        += librtmp.o
OBJS-$(CONFIG_LIBSMBCLIENT_PROTOCOL)     += libsmbclient.o
OBJS-$(CONFIG_LIBSRT_PROTOCOL)           += libsrt.o
OBJS-$(CONFIG_LIBSSH_PROTOCOL)           += libssh.o
OBJS-$(CONFIG_LIBZMQ_PROTOCOL)           += libzmq.o

# Objects duplicated from other libraries for shared builds
SHLIBOBJS                                += log2_tab.o to_upper4.o
SHLIBOBJS-$(CONFIG_ISO_MEDIA)            += mpegaudiotabs.o
SHLIBOBJS-$(CONFIG_FLV_MUXER)            += mpeg4audio_sample_rates.o
SHLIBOBJS-$(CONFIG_HLS_DEMUXER)          += ac3_channel_layout_tab.o
SHLIBOBJS-$(CONFIG_MATROSKA_DEMUXER)     += mpeg4audio_sample_rates.o
SHLIBOBJS-$(CONFIG_MOV_DEMUXER)          += ac3_channel_layout_tab.o
SHLIBOBJS-$(CONFIG_MP3_MUXER)            += mpegaudiotabs.o
SHLIBOBJS-$(CONFIG_MXF_MUXER)            += golomb_tab.o
SHLIBOBJS-$(CONFIG_NUT_MUXER)            += mpegaudiotabs.o
SHLIBOBJS-$(CONFIG_RTPDEC)               += jpegtables.o
SHLIBOBJS-$(CONFIG_RTP_MUXER)            += golomb_tab.o jpegtables.o \
                                            mpeg4audio_sample_rates.o
SHLIBOBJS-$(CONFIG_SPDIF_MUXER)          += dca_sample_rate_tab.o

# libavdevice dependencies
OBJS-$(CONFIG_IEC61883_INDEV)            += dv.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)             += avformatres.o

SKIPHEADERS-$(CONFIG_IMF_DEMUXER)        += imf.h
SKIPHEADERS-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += rtmpdh.h
SKIPHEADERS-$(CONFIG_NETWORK)            += network.h rtsp.h

TESTPROGS = seek                                                        \
            url                                                         \
            seek_utils
#           async                                                       \

FIFO-MUXER-TESTPROGS-$(CONFIG_NETWORK)   += fifo_muxer
TESTPROGS-$(CONFIG_FIFO_MUXER)           += $(FIFO-MUXER-TESTPROGS-yes)
TESTPROGS-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += rtmpdh
TESTPROGS-$(CONFIG_MOV_MUXER)            += movenc
TESTPROGS-$(CONFIG_NETWORK)              += noproxy
TESTPROGS-$(CONFIG_SRTP)                 += srtp
TESTPROGS-$(CONFIG_IMF_DEMUXER)          += imf

TOOLS     = aviocat                                                     \
            ismindex                                                    \
            pktdumper                                                   \
            probetest                                                   \
            seek_print                                                  \
            sidxindex                                                   \
            venc_data_dump
