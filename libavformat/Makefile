include $(SUBDIR)../config.mak

NAME = avformat
FFLIBS = avcodec avutil

HEADERS = avformat.h avio.h version.h

OBJS = allformats.o         \
       cutils.o             \
       id3v1.o              \
       id3v2.o              \
       metadata.o           \
       options.o            \
       os_support.o         \
       sdp.o                \
       seek.o               \
       utils.o              \

OBJS-$(CONFIG_NETWORK)                   += network.o

# muxers/demuxers
OBJS-$(CONFIG_A64_MUXER)                 += a64.o
OBJS-$(CONFIG_AAC_DEMUXER)               += aacdec.o rawdec.o
OBJS-$(CONFIG_AC3_DEMUXER)               += ac3dec.o rawdec.o
OBJS-$(CONFIG_AC3_MUXER)                 += rawenc.o
OBJS-$(CONFIG_ACT_DEMUXER)               += act.o
OBJS-$(CONFIG_ADF_DEMUXER)               += bintext.o sauce.o
OBJS-$(CONFIG_ADTS_MUXER)                += adtsenc.o
OBJS-$(CONFIG_AEA_DEMUXER)               += aea.o pcm.o
OBJS-$(CONFIG_AIFF_DEMUXER)              += aiffdec.o riff.o pcm.o isom.o
OBJS-$(CONFIG_AIFF_MUXER)                += aiffenc.o riff.o isom.o
OBJS-$(CONFIG_AMR_DEMUXER)               += amr.o
OBJS-$(CONFIG_AMR_MUXER)                 += amr.o
OBJS-$(CONFIG_ANM_DEMUXER)               += anm.o
OBJS-$(CONFIG_APC_DEMUXER)               += apc.o
OBJS-$(CONFIG_APE_DEMUXER)               += ape.o apetag.o
OBJS-$(CONFIG_APPLEHTTP_DEMUXER)         += applehttp.o
OBJS-$(CONFIG_ASF_DEMUXER)               += asfdec.o asf.o asfcrypt.o \
                                            riff.o avlanguage.o
OBJS-$(CONFIG_ASF_MUXER)                 += asfenc.o asf.o riff.o
OBJS-$(CONFIG_ASS_DEMUXER)               += assdec.o
OBJS-$(CONFIG_ASS_MUXER)                 += assenc.o
OBJS-$(CONFIG_AU_DEMUXER)                += au.o pcm.o
OBJS-$(CONFIG_AU_MUXER)                  += au.o
OBJS-$(CONFIG_AVI_DEMUXER)               += avidec.o riff.o avi.o
OBJS-$(CONFIG_AVI_MUXER)                 += avienc.o riff.o avi.o
OBJS-$(CONFIG_AVISYNTH)                  += avisynth.o
OBJS-$(CONFIG_AVM2_MUXER)                += swfenc.o
OBJS-$(CONFIG_AVS_DEMUXER)               += avs.o vocdec.o voc.o
OBJS-$(CONFIG_BETHSOFTVID_DEMUXER)       += bethsoftvid.o
OBJS-$(CONFIG_BFI_DEMUXER)               += bfi.o
OBJS-$(CONFIG_BINK_DEMUXER)              += bink.o
OBJS-$(CONFIG_BINTEXT_DEMUXER)           += bintext.o sauce.o
OBJS-$(CONFIG_BIT_DEMUXER)               += bit.o
OBJS-$(CONFIG_BIT_MUXER)                 += bit.o
OBJS-$(CONFIG_BMV_DEMUXER)               += bmv.o
OBJS-$(CONFIG_C93_DEMUXER)               += c93.o vocdec.o voc.o
OBJS-$(CONFIG_CAF_DEMUXER)               += cafdec.o caf.o mov.o riff.o isom.o
OBJS-$(CONFIG_CAF_MUXER)                 += cafenc.o caf.o riff.o isom.o
OBJS-$(CONFIG_CAVSVIDEO_DEMUXER)         += cavsvideodec.o rawdec.o
OBJS-$(CONFIG_CAVSVIDEO_MUXER)           += rawenc.o
OBJS-$(CONFIG_CDG_DEMUXER)               += cdg.o
OBJS-$(CONFIG_CRC_MUXER)                 += crcenc.o
OBJS-$(CONFIG_DAUD_DEMUXER)              += daud.o
OBJS-$(CONFIG_DAUD_MUXER)                += daud.o
OBJS-$(CONFIG_DFA_DEMUXER)               += dfa.o
OBJS-$(CONFIG_DIRAC_DEMUXER)             += diracdec.o rawdec.o
OBJS-$(CONFIG_DIRAC_MUXER)               += rawenc.o
OBJS-$(CONFIG_DNXHD_DEMUXER)             += dnxhddec.o rawdec.o
OBJS-$(CONFIG_DNXHD_MUXER)               += rawenc.o
OBJS-$(CONFIG_DSICIN_DEMUXER)            += dsicin.o
OBJS-$(CONFIG_DTS_DEMUXER)               += dtsdec.o rawdec.o
OBJS-$(CONFIG_DTS_MUXER)                 += rawenc.o
OBJS-$(CONFIG_DV_DEMUXER)                += dv.o
OBJS-$(CONFIG_DV_MUXER)                  += dvenc.o
OBJS-$(CONFIG_DXA_DEMUXER)               += dxa.o riff.o
OBJS-$(CONFIG_EA_CDATA_DEMUXER)          += eacdata.o
OBJS-$(CONFIG_EA_DEMUXER)                += electronicarts.o
OBJS-$(CONFIG_EAC3_DEMUXER)              += ac3dec.o rawdec.o
OBJS-$(CONFIG_EAC3_MUXER)                += rawenc.o
OBJS-$(CONFIG_FFM_DEMUXER)               += ffmdec.o
OBJS-$(CONFIG_FFM_MUXER)                 += ffmenc.o
OBJS-$(CONFIG_FFMETADATA_DEMUXER)        += ffmetadec.o
OBJS-$(CONFIG_FFMETADATA_MUXER)          += ffmetaenc.o
OBJS-$(CONFIG_FILMSTRIP_DEMUXER)         += filmstripdec.o
OBJS-$(CONFIG_FILMSTRIP_MUXER)           += filmstripenc.o
OBJS-$(CONFIG_FLAC_DEMUXER)              += flacdec.o rawdec.o \
                                            oggparsevorbis.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_FLAC_MUXER)                += flacenc.o flacenc_header.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_FLIC_DEMUXER)              += flic.o
OBJS-$(CONFIG_FLV_DEMUXER)               += flvdec.o
OBJS-$(CONFIG_FLV_MUXER)                 += flvenc.o avc.o
OBJS-$(CONFIG_FOURXM_DEMUXER)            += 4xm.o
OBJS-$(CONFIG_FRAMECRC_MUXER)            += framecrcenc.o
OBJS-$(CONFIG_FRAMEMD5_MUXER)            += md5enc.o
OBJS-$(CONFIG_GIF_MUXER)                 += gif.o
OBJS-$(CONFIG_GSM_DEMUXER)               += gsmdec.o
OBJS-$(CONFIG_GXF_DEMUXER)               += gxf.o
OBJS-$(CONFIG_GXF_MUXER)                 += gxfenc.o audiointerleave.o
OBJS-$(CONFIG_G722_DEMUXER)              += rawdec.o
OBJS-$(CONFIG_G722_MUXER)                += rawenc.o
OBJS-$(CONFIG_G723_1_DEMUXER)            += g723_1.o
OBJS-$(CONFIG_G723_1_MUXER)              += rawenc.o
OBJS-$(CONFIG_G729_DEMUXER)              += g729dec.o
OBJS-$(CONFIG_H261_DEMUXER)              += h261dec.o rawdec.o
OBJS-$(CONFIG_H261_MUXER)                += rawenc.o
OBJS-$(CONFIG_H263_DEMUXER)              += h263dec.o rawdec.o
OBJS-$(CONFIG_H263_MUXER)                += rawenc.o
OBJS-$(CONFIG_H264_DEMUXER)              += h264dec.o rawdec.o
OBJS-$(CONFIG_H264_MUXER)                += rawenc.o
OBJS-$(CONFIG_IDCIN_DEMUXER)             += idcin.o
OBJS-$(CONFIG_IDF_DEMUXER)               += bintext.o
OBJS-$(CONFIG_IFF_DEMUXER)               += iff.o
OBJS-$(CONFIG_IMAGE2_DEMUXER)            += img2.o
OBJS-$(CONFIG_IMAGE2_MUXER)              += img2.o
OBJS-$(CONFIG_IMAGE2PIPE_DEMUXER)        += img2.o
OBJS-$(CONFIG_IMAGE2PIPE_MUXER)          += img2.o
OBJS-$(CONFIG_INGENIENT_DEMUXER)         += ingenientdec.o rawdec.o
OBJS-$(CONFIG_IPMOVIE_DEMUXER)           += ipmovie.o
OBJS-$(CONFIG_ISS_DEMUXER)               += iss.o
OBJS-$(CONFIG_IV8_DEMUXER)               += iv8.o
OBJS-$(CONFIG_IVF_DEMUXER)               += ivfdec.o riff.o
OBJS-$(CONFIG_IVF_MUXER)                 += ivfenc.o
OBJS-$(CONFIG_JV_DEMUXER)                += jvdec.o
OBJS-$(CONFIG_LATM_DEMUXER)              += rawdec.o
OBJS-$(CONFIG_LATM_MUXER)                += latmenc.o
OBJS-$(CONFIG_LMLM4_DEMUXER)             += lmlm4.o
OBJS-$(CONFIG_LOAS_DEMUXER)              += loasdec.o
OBJS-$(CONFIG_LXF_DEMUXER)               += lxfdec.o
OBJS-$(CONFIG_M4V_DEMUXER)               += m4vdec.o rawdec.o
OBJS-$(CONFIG_M4V_MUXER)                 += rawenc.o
OBJS-$(CONFIG_MATROSKA_DEMUXER)          += matroskadec.o matroska.o \
                                            riff.o isom.o rmdec.o rm.o
OBJS-$(CONFIG_MATROSKA_MUXER)            += matroskaenc.o matroska.o \
                                            riff.o isom.o avc.o \
                                            flacenc_header.o avlanguage.o
OBJS-$(CONFIG_MD5_MUXER)                 += md5enc.o
OBJS-$(CONFIG_MICRODVD_DEMUXER)          += microdvddec.o
OBJS-$(CONFIG_MICRODVD_MUXER)            += microdvdenc.o rawenc.o
OBJS-$(CONFIG_MJPEG_DEMUXER)             += rawdec.o
OBJS-$(CONFIG_MJPEG_MUXER)               += rawenc.o
OBJS-$(CONFIG_MLP_DEMUXER)               += rawdec.o
OBJS-$(CONFIG_MLP_MUXER)                 += rawenc.o
OBJS-$(CONFIG_MM_DEMUXER)                += mm.o
OBJS-$(CONFIG_MMF_DEMUXER)               += mmf.o pcm.o
OBJS-$(CONFIG_MMF_MUXER)                 += mmf.o riff.o
OBJS-$(CONFIG_MOV_DEMUXER)               += mov.o riff.o isom.o
OBJS-$(CONFIG_MOV_MUXER)                 += movenc.o riff.o isom.o avc.o \
                                            movenchint.o rtpenc_chain.o
OBJS-$(CONFIG_MP2_MUXER)                 += mp3enc.o rawenc.o
OBJS-$(CONFIG_MP3_DEMUXER)               += mp3dec.o
OBJS-$(CONFIG_MP3_MUXER)                 += mp3enc.o rawenc.o id3v2enc.o
OBJS-$(CONFIG_MPC_DEMUXER)               += mpc.o apetag.o
OBJS-$(CONFIG_MPC8_DEMUXER)              += mpc8.o
OBJS-$(CONFIG_MPEG1SYSTEM_MUXER)         += mpegenc.o
OBJS-$(CONFIG_MPEG1VCD_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEG2DVD_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEG2VOB_MUXER)            += mpegenc.o
OBJS-$(CONFIG_MPEG2SVCD_MUXER)           += mpegenc.o
OBJS-$(CONFIG_MPEG1VIDEO_MUXER)          += rawenc.o
OBJS-$(CONFIG_MPEG2VIDEO_MUXER)          += rawenc.o
OBJS-$(CONFIG_MPEGPS_DEMUXER)            += mpeg.o
OBJS-$(CONFIG_MPEGTS_DEMUXER)            += mpegts.o isom.o
OBJS-$(CONFIG_MPEGTS_MUXER)              += mpegtsenc.o adtsenc.o
OBJS-$(CONFIG_MPEGVIDEO_DEMUXER)         += mpegvideodec.o rawdec.o
OBJS-$(CONFIG_MPJPEG_MUXER)              += mpjpeg.o
OBJS-$(CONFIG_MSNWC_TCP_DEMUXER)         += msnwc_tcp.o
OBJS-$(CONFIG_MTV_DEMUXER)               += mtv.o
OBJS-$(CONFIG_MVI_DEMUXER)               += mvi.o
OBJS-$(CONFIG_MXF_DEMUXER)               += mxfdec.o mxf.o
OBJS-$(CONFIG_MXF_MUXER)                 += mxfenc.o mxf.o audiointerleave.o
OBJS-$(CONFIG_MXG_DEMUXER)               += mxg.o
OBJS-$(CONFIG_NC_DEMUXER)                += ncdec.o
OBJS-$(CONFIG_NSV_DEMUXER)               += nsvdec.o
OBJS-$(CONFIG_NULL_MUXER)                += nullenc.o
OBJS-$(CONFIG_NUT_DEMUXER)               += nutdec.o nut.o riff.o
OBJS-$(CONFIG_NUT_MUXER)                 += nutenc.o nut.o riff.o
OBJS-$(CONFIG_NUV_DEMUXER)               += nuv.o riff.o
OBJS-$(CONFIG_OGG_DEMUXER)               += oggdec.o         \
                                            oggparsecelt.o   \
                                            oggparsedirac.o  \
                                            oggparseflac.o   \
                                            oggparseogm.o    \
                                            oggparseskeleton.o \
                                            oggparsespeex.o  \
                                            oggparsetheora.o \
                                            oggparsevorbis.o \
                                            riff.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_OGG_MUXER)                 += oggenc.o \
                                            vorbiscomment.o
OBJS-$(CONFIG_OMA_DEMUXER)               += oma.o pcm.o
OBJS-$(CONFIG_PCM_ALAW_DEMUXER)          += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_ALAW_MUXER)            += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F32BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_F32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F32LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_F32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F64BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_F64BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_F64LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_F64LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_MULAW_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_MULAW_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S16BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S16BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S16LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S16LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S24BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S24BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S24LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S24LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S32BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S32LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_S8_DEMUXER)            += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_S8_MUXER)              += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U16BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U16BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U16LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U16LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U24BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U24BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U24LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U24LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U32BE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U32BE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U32LE_DEMUXER)         += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U32LE_MUXER)           += pcmenc.o rawenc.o
OBJS-$(CONFIG_PCM_U8_DEMUXER)            += pcmdec.o pcm.o rawdec.o
OBJS-$(CONFIG_PCM_U8_MUXER)              += pcmenc.o rawenc.o
OBJS-$(CONFIG_PMP_DEMUXER)               += pmpdec.o
OBJS-$(CONFIG_PVA_DEMUXER)               += pva.o
OBJS-$(CONFIG_QCP_DEMUXER)               += qcp.o
OBJS-$(CONFIG_R3D_DEMUXER)               += r3d.o
OBJS-$(CONFIG_RAWVIDEO_DEMUXER)          += rawvideodec.o rawdec.o
OBJS-$(CONFIG_RAWVIDEO_MUXER)            += rawenc.o
OBJS-$(CONFIG_RL2_DEMUXER)               += rl2.o
OBJS-$(CONFIG_RM_DEMUXER)                += rmdec.o rm.o
OBJS-$(CONFIG_RM_MUXER)                  += rmenc.o rm.o
OBJS-$(CONFIG_ROQ_DEMUXER)               += idroqdec.o
OBJS-$(CONFIG_ROQ_MUXER)                 += idroqenc.o rawenc.o
OBJS-$(CONFIG_RSO_DEMUXER)               += rsodec.o rso.o pcm.o
OBJS-$(CONFIG_RSO_MUXER)                 += rsoenc.o rso.o
OBJS-$(CONFIG_RPL_DEMUXER)               += rpl.o
OBJS-$(CONFIG_RTP_MUXER)                 += rtp.o         \
                                            rtpenc_aac.o     \
                                            rtpenc_latm.o    \
                                            rtpenc_amr.o     \
                                            rtpenc_h263.o    \
                                            rtpenc_mpv.o     \
                                            rtpenc.o      \
                                            rtpenc_h264.o \
                                            rtpenc_vp8.o  \
                                            rtpenc_xiph.o \
                                            avc.o
OBJS-$(CONFIG_RTPDEC)                    += rdt.o         \
                                            rtp.o         \
                                            rtpdec.o      \
                                            rtpdec_amr.o  \
                                            rtpdec_asf.o  \
                                            rtpdec_g726.o \
                                            rtpdec_h263.o \
                                            rtpdec_h264.o \
                                            rtpdec_latm.o \
                                            rtpdec_mpeg4.o \
                                            rtpdec_qcelp.o \
                                            rtpdec_qdm2.o \
                                            rtpdec_qt.o   \
                                            rtpdec_svq3.o \
                                            rtpdec_vp8.o  \
                                            rtpdec_xiph.o
OBJS-$(CONFIG_RTSP_DEMUXER)              += rtsp.o rtspdec.o httpauth.o
OBJS-$(CONFIG_RTSP_MUXER)                += rtsp.o rtspenc.o httpauth.o \
                                            rtpenc_chain.o
OBJS-$(CONFIG_SAP_DEMUXER)               += sapdec.o
OBJS-$(CONFIG_SAP_MUXER)                 += sapenc.o rtpenc_chain.o
OBJS-$(CONFIG_SDP_DEMUXER)               += rtsp.o
OBJS-$(CONFIG_SEGAFILM_DEMUXER)          += segafilm.o
OBJS-$(CONFIG_SEGMENT_MUXER)          	 += segment.o
OBJS-$(CONFIG_SHORTEN_DEMUXER)           += rawdec.o
OBJS-$(CONFIG_SIFF_DEMUXER)              += siff.o
OBJS-$(CONFIG_SMACKER_DEMUXER)           += smacker.o
OBJS-$(CONFIG_SOL_DEMUXER)               += sol.o pcm.o
OBJS-$(CONFIG_SOX_DEMUXER)               += soxdec.o pcm.o
OBJS-$(CONFIG_SOX_MUXER)                 += soxenc.o
OBJS-$(CONFIG_SPDIF_DEMUXER)             += spdif.o spdifdec.o
OBJS-$(CONFIG_SPDIF_MUXER)               += spdif.o spdifenc.o
OBJS-$(CONFIG_SRT_DEMUXER)               += srtdec.o
OBJS-$(CONFIG_SRT_MUXER)                 += rawenc.o
OBJS-$(CONFIG_STR_DEMUXER)               += psxstr.o
OBJS-$(CONFIG_SWF_DEMUXER)               += swfdec.o
OBJS-$(CONFIG_SWF_MUXER)                 += swfenc.o
OBJS-$(CONFIG_THP_DEMUXER)               += thp.o
OBJS-$(CONFIG_TIERTEXSEQ_DEMUXER)        += tiertexseq.o
OBJS-$(CONFIG_MKVTIMESTAMP_V2_MUXER)     += mkvtimestamp_v2.o
OBJS-$(CONFIG_TMV_DEMUXER)               += tmv.o
OBJS-$(CONFIG_TRUEHD_DEMUXER)            += rawdec.o
OBJS-$(CONFIG_TRUEHD_MUXER)              += rawenc.o
OBJS-$(CONFIG_TTA_DEMUXER)               += tta.o
OBJS-$(CONFIG_TTY_DEMUXER)               += tty.o sauce.o
OBJS-$(CONFIG_TXD_DEMUXER)               += txd.o
OBJS-$(CONFIG_VC1_DEMUXER)               += rawdec.o
OBJS-$(CONFIG_VC1T_DEMUXER)              += vc1test.o
OBJS-$(CONFIG_VC1T_MUXER)                += vc1testenc.o
OBJS-$(CONFIG_VMD_DEMUXER)               += sierravmd.o
OBJS-$(CONFIG_VOC_DEMUXER)               += vocdec.o voc.o
OBJS-$(CONFIG_VOC_MUXER)                 += vocenc.o voc.o
OBJS-$(CONFIG_VQF_DEMUXER)               += vqf.o
OBJS-$(CONFIG_W64_DEMUXER)               += wav.o riff.o pcm.o
OBJS-$(CONFIG_WAV_DEMUXER)               += wav.o riff.o pcm.o
OBJS-$(CONFIG_WAV_MUXER)                 += wav.o riff.o
OBJS-$(CONFIG_WC3_DEMUXER)               += wc3movie.o
OBJS-$(CONFIG_WEBM_MUXER)                += matroskaenc.o matroska.o \
                                            riff.o isom.o avc.o \
                                            flacenc_header.o avlanguage.o
OBJS-$(CONFIG_WSAUD_DEMUXER)             += westwood.o
OBJS-$(CONFIG_WSVQA_DEMUXER)             += westwood.o
OBJS-$(CONFIG_WTV_DEMUXER)               += wtvdec.o wtv.o asfdec.o asf.o asfcrypt.o \
                                            avlanguage.o mpegts.o isom.o riff.o
OBJS-$(CONFIG_WTV_MUXER)                 += wtvenc.o wtv.o asf.o asfenc.o riff.o
OBJS-$(CONFIG_WV_DEMUXER)                += wv.o apetag.o
OBJS-$(CONFIG_XA_DEMUXER)                += xa.o
OBJS-$(CONFIG_XBIN_DEMUXER)              += bintext.o sauce.o
OBJS-$(CONFIG_XMV_DEMUXER)               += xmv.o
OBJS-$(CONFIG_XWMA_DEMUXER)              += xwma.o riff.o
OBJS-$(CONFIG_YOP_DEMUXER)               += yop.o
OBJS-$(CONFIG_YUV4MPEGPIPE_MUXER)        += yuv4mpeg.o
OBJS-$(CONFIG_YUV4MPEGPIPE_DEMUXER)      += yuv4mpeg.o

# external libraries
OBJS-$(CONFIG_LIBMODPLUG_DEMUXER)        += libmodplug.o
OBJS-$(CONFIG_LIBNUT_DEMUXER)            += libnut.o riff.o
OBJS-$(CONFIG_LIBNUT_MUXER)              += libnut.o riff.o

# protocols I/O
OBJS+= avio.o aviobuf.o

OBJS-$(CONFIG_APPLEHTTP_PROTOCOL)        += applehttpproto.o
OBJS-$(CONFIG_CACHE_PROTOCOL)            += cache.o
OBJS-$(CONFIG_CONCAT_PROTOCOL)           += concat.o
OBJS-$(CONFIG_CRYPTO_PROTOCOL)           += crypto.o
OBJS-$(CONFIG_FILE_PROTOCOL)             += file.o
OBJS-$(CONFIG_GOPHER_PROTOCOL)           += gopher.o
OBJS-$(CONFIG_HTTP_PROTOCOL)             += http.o httpauth.o
OBJS-$(CONFIG_HTTPPROXY_PROTOCOL)        += http.o httpauth.o
OBJS-$(CONFIG_HTTPS_PROTOCOL)            += http.o httpauth.o
OBJS-$(CONFIG_MMSH_PROTOCOL)             += mmsh.o mms.o asf.o
OBJS-$(CONFIG_MMST_PROTOCOL)             += mmst.o mms.o asf.o
OBJS-$(CONFIG_MD5_PROTOCOL)              += md5proto.o
OBJS-$(CONFIG_PIPE_PROTOCOL)             += file.o

# external or internal rtmp
RTMP-OBJS-$(CONFIG_LIBRTMP)               = librtmp.o
RTMP-OBJS-$(!CONFIG_LIBRTMP)              = rtmpproto.o rtmppkt.o
OBJS-$(CONFIG_RTMP_PROTOCOL)             += $(RTMP-OBJS-yes)

OBJS-$(CONFIG_RTP_PROTOCOL)              += rtpproto.o
OBJS-$(CONFIG_TCP_PROTOCOL)              += tcp.o
OBJS-$(CONFIG_TLS_PROTOCOL)              += tls.o
OBJS-$(CONFIG_UDP_PROTOCOL)              += udp.o


TESTPROGS = seek
TOOLS     = pktdumper probetest

include $(SRC_PATH)/subdir.mak
