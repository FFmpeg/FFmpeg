/*
 * Utils for libavcodec
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file allcodecs.c
 * Utils for libavcodec.
 */

#include "avcodec.h"

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * simple call to register all the codecs. 
 */
void avcodec_register_all(void)
{
    static int inited = 0;
    
    if (inited != 0)
	return;
    inited = 1;

    /* encoders */
#ifdef CONFIG_ENCODERS
#ifdef CONFIG_AC3_ENCODER
    register_avcodec(&ac3_encoder);
#endif //CONFIG_AC3_ENCODER
#ifdef CONFIG_MP2_ENCODER
    register_avcodec(&mp2_encoder);
#endif //CONFIG_MP2_ENCODER
#ifdef CONFIG_MP3LAME
#ifdef CONFIG_MP3LAME_ENCODER
    register_avcodec(&mp3lame_encoder);
#endif //CONFIG_MP3LAME_ENCODER
#endif
#ifdef CONFIG_LIBVORBIS
#ifdef CONFIG_OGGVORBIS_ENCODER
    register_avcodec(&oggvorbis_encoder);
#endif //CONFIG_OGGVORBIS_ENCODER
#if (defined CONFIG_OGGVORBIS_DECODER && !defined CONFIG_VORBIS_DECODER)
    register_avcodec(&oggvorbis_decoder);
#endif //CONFIG_OGGVORBIS_DECODER
#endif
#ifdef CONFIG_LIBTHEORA
#ifdef CONFIG_OGGTHEORA_ENCODER
//    register_avcodec(&oggtheora_encoder);
#endif //CONFIG_OGGTHEORA_ENCODER
#ifdef CONFIG_OGGTHEORA_DECODER
    register_avcodec(&oggtheora_decoder);
#endif //CONFIG_OGGTHEORA_DECODER
#endif
#ifdef CONFIG_FAAC
#ifdef CONFIG_FAAC_ENCODER
    register_avcodec(&faac_encoder);
#endif //CONFIG_FAAC_ENCODER
#endif
#ifdef CONFIG_XVID
#ifdef CONFIG_XVID_ENCODER
    register_avcodec(&xvid_encoder);
#endif //CONFIG_XVID_ENCODER
#endif
#ifdef CONFIG_MPEG1VIDEO_ENCODER
    register_avcodec(&mpeg1video_encoder);
#endif //CONFIG_MPEG1VIDEO_ENCODER
#ifdef CONFIG_H264_ENCODER
//    register_avcodec(&h264_encoder);
#endif //CONFIG_H264_ENCODER
#ifdef CONFIG_MPEG2VIDEO_ENCODER
    register_avcodec(&mpeg2video_encoder);
#endif //CONFIG_MPEG2VIDEO_ENCODER
#ifdef CONFIG_H261_ENCODER
    register_avcodec(&h261_encoder);
#endif //CONFIG_H261_ENCODER
#ifdef CONFIG_H263_ENCODER
    register_avcodec(&h263_encoder);
#endif //CONFIG_H263_ENCODER
#ifdef CONFIG_H263P_ENCODER
    register_avcodec(&h263p_encoder);
#endif //CONFIG_H263P_ENCODER
#ifdef CONFIG_FLV_ENCODER
    register_avcodec(&flv_encoder);
#endif //CONFIG_FLV_ENCODER
#ifdef CONFIG_RV10_ENCODER
    register_avcodec(&rv10_encoder);
#endif //CONFIG_RV10_ENCODER
#ifdef CONFIG_RV20_ENCODER
    register_avcodec(&rv20_encoder);
#endif //CONFIG_RV20_ENCODER
#ifdef CONFIG_MPEG4_ENCODER
    register_avcodec(&mpeg4_encoder);
#endif //CONFIG_MPEG4_ENCODER
#ifdef CONFIG_MSMPEG4V1_ENCODER
    register_avcodec(&msmpeg4v1_encoder);
#endif //CONFIG_MSMPEG4V1_ENCODER
#ifdef CONFIG_MSMPEG4V2_ENCODER
    register_avcodec(&msmpeg4v2_encoder);
#endif //CONFIG_MSMPEG4V2_ENCODER
#ifdef CONFIG_MSMPEG4V3_ENCODER
    register_avcodec(&msmpeg4v3_encoder);
#endif //CONFIG_MSMPEG4V3_ENCODER
#ifdef CONFIG_WMV1_ENCODER
    register_avcodec(&wmv1_encoder);
#endif //CONFIG_WMV1_ENCODER
#ifdef CONFIG_WMV2_ENCODER
    register_avcodec(&wmv2_encoder);
#endif //CONFIG_WMV2_ENCODER
#ifdef CONFIG_SVQ1_ENCODER
    register_avcodec(&svq1_encoder);
#endif //CONFIG_SVQ1_ENCODER
#ifdef CONFIG_MJPEG_ENCODER
    register_avcodec(&mjpeg_encoder);
#endif //CONFIG_MJPEG_ENCODER
#ifdef CONFIG_LJPEG_ENCODER
    register_avcodec(&ljpeg_encoder);
#endif //CONFIG_LJPEG_ENCODER
#ifdef CONFIG_ZLIB
#ifdef CONFIG_PNG_ENCODER
    register_avcodec(&png_encoder);
#endif //CONFIG_PNG_ENCODER
#endif
#ifdef CONFIG_PPM_ENCODER
    register_avcodec(&ppm_encoder);
#endif //CONFIG_PPM_ENCODER
#ifdef CONFIG_PGM_ENCODER
    register_avcodec(&pgm_encoder);
#endif //CONFIG_PGM_ENCODER
#ifdef CONFIG_PGMYUV_ENCODER
    register_avcodec(&pgmyuv_encoder);
#endif //CONFIG_PGMYUV_ENCODER
#ifdef CONFIG_PBM_ENCODER
    register_avcodec(&pbm_encoder);
#endif //CONFIG_PBM_ENCODER
#ifdef CONFIG_PAM_ENCODER
    register_avcodec(&pam_encoder);
#endif //CONFIG_PAM_ENCODER
#ifdef CONFIG_HUFFYUV_ENCODER
    register_avcodec(&huffyuv_encoder);
#endif //CONFIG_HUFFYUV_ENCODER
#ifdef CONFIG_FFVHUFF_ENCODER
    register_avcodec(&ffvhuff_encoder);
#endif //CONFIG_FFVHUFF_ENCODER
#ifdef CONFIG_ASV1_ENCODER
    register_avcodec(&asv1_encoder);
#endif //CONFIG_ASV1_ENCODER
#ifdef CONFIG_ASV2_ENCODER
    register_avcodec(&asv2_encoder);
#endif //CONFIG_ASV2_ENCODER
#ifdef CONFIG_FFV1_ENCODER
    register_avcodec(&ffv1_encoder);
#endif //CONFIG_FFV1_ENCODER
#ifdef CONFIG_SNOW_ENCODER
    register_avcodec(&snow_encoder);
#endif //CONFIG_SNOW_ENCODER
#ifdef CONFIG_ZLIB_ENCODER
    register_avcodec(&zlib_encoder);
#endif //CONFIG_ZLIB_ENCODER
#ifdef CONFIG_DVVIDEO_ENCODER
    register_avcodec(&dvvideo_encoder);
#endif //CONFIG_DVVIDEO_ENCODER
#ifdef CONFIG_SONIC_ENCODER
    register_avcodec(&sonic_encoder);
#endif //CONFIG_SONIC_ENCODER
#ifdef CONFIG_SONIC_LS_ENCODER
    register_avcodec(&sonic_ls_encoder);
#endif //CONFIG_SONIC_LS_ENCODER
#ifdef CONFIG_X264
#ifdef CONFIG_X264_ENCODER
    register_avcodec(&x264_encoder);
#endif //CONFIG_X264_ENCODER
#endif
#ifdef CONFIG_LIBGSM
    register_avcodec(&libgsm_encoder);
#endif //CONFIG_LIBGSM
#endif /* CONFIG_ENCODERS */
#ifdef CONFIG_RAWVIDEO_ENCODER
    register_avcodec(&rawvideo_encoder);
#endif //CONFIG_RAWVIDEO_ENCODER
#ifdef CONFIG_RAWVIDEO_DECODER
    register_avcodec(&rawvideo_decoder);
#endif //CONFIG_RAWVIDEO_DECODER

    /* decoders */
#ifdef CONFIG_DECODERS
#ifdef CONFIG_H263_DECODER
    register_avcodec(&h263_decoder);
#endif //CONFIG_H263_DECODER
#ifdef CONFIG_H261_DECODER
    register_avcodec(&h261_decoder);
#endif //CONFIG_H261_DECODER
#ifdef CONFIG_MPEG4_DECODER
    register_avcodec(&mpeg4_decoder);
#endif //CONFIG_MPEG4_DECODER
#ifdef CONFIG_MSMPEG4V1_DECODER
    register_avcodec(&msmpeg4v1_decoder);
#endif //CONFIG_MSMPEG4V1_DECODER
#ifdef CONFIG_MSMPEG4V2_DECODER
    register_avcodec(&msmpeg4v2_decoder);
#endif //CONFIG_MSMPEG4V2_DECODER
#ifdef CONFIG_MSMPEG4V3_DECODER
    register_avcodec(&msmpeg4v3_decoder);
#endif //CONFIG_MSMPEG4V3_DECODER
#ifdef CONFIG_WMV1_DECODER
    register_avcodec(&wmv1_decoder);
#endif //CONFIG_WMV1_DECODER
#ifdef CONFIG_WMV2_DECODER
    register_avcodec(&wmv2_decoder);
#endif //CONFIG_WMV2_DECODER
#ifdef CONFIG_VC9_DECODER
    register_avcodec(&vc9_decoder);
#endif //CONFIG_VC9_DECODER
#ifdef CONFIG_WMV3_DECODER
    register_avcodec(&wmv3_decoder);
#endif //CONFIG_WMV3_DECODER
#ifdef CONFIG_H263I_DECODER
    register_avcodec(&h263i_decoder);
#endif //CONFIG_H263I_DECODER
#ifdef CONFIG_FLV_DECODER
    register_avcodec(&flv_decoder);
#endif //CONFIG_FLV_DECODER
#ifdef CONFIG_RV10_DECODER
    register_avcodec(&rv10_decoder);
#endif //CONFIG_RV10_DECODER
#ifdef CONFIG_RV20_DECODER
    register_avcodec(&rv20_decoder);
#endif //CONFIG_RV20_DECODER
#ifdef CONFIG_SVQ1_DECODER
    register_avcodec(&svq1_decoder);
#endif //CONFIG_SVQ1_DECODER
#ifdef CONFIG_SVQ3_DECODER
    register_avcodec(&svq3_decoder);
#endif //CONFIG_SVQ3_DECODER
#ifdef CONFIG_WMAV1_DECODER
    register_avcodec(&wmav1_decoder);
#endif //CONFIG_WMAV1_DECODER
#ifdef CONFIG_WMAV2_DECODER
    register_avcodec(&wmav2_decoder);
#endif //CONFIG_WMAV2_DECODER
#ifdef CONFIG_INDEO2_DECODER
    register_avcodec(&indeo2_decoder);
#endif //CONFIG_INDEO2_DECODER
#ifdef CONFIG_INDEO3_DECODER
    register_avcodec(&indeo3_decoder);
#endif //CONFIG_INDEO3_DECODER
#ifdef CONFIG_TSCC_DECODER
    register_avcodec(&tscc_decoder);
#endif //CONFIG_TSCC_DECODER
#ifdef CONFIG_ULTI_DECODER
    register_avcodec(&ulti_decoder);
#endif //CONFIG_ULTI_DECODER
#ifdef CONFIG_QDRAW_DECODER
    register_avcodec(&qdraw_decoder);
#endif //CONFIG_QDRAW_DECODER
#ifdef CONFIG_XL_DECODER
    register_avcodec(&xl_decoder);
#endif //CONFIG_XL_DECODER
#ifdef CONFIG_QPEG_DECODER
    register_avcodec(&qpeg_decoder);
#endif //CONFIG_QPEG_DECODER
#ifdef CONFIG_LOCO_DECODER
    register_avcodec(&loco_decoder);
#endif //CONFIG_LOCO_DECODER
#ifdef CONFIG_WNV1_DECODER
    register_avcodec(&wnv1_decoder);
#endif //CONFIG_WNV1_DECODER
#ifdef CONFIG_AASC_DECODER
    register_avcodec(&aasc_decoder);
#endif //CONFIG_AASC_DECODER
#ifdef CONFIG_FRAPS_DECODER
    register_avcodec(&fraps_decoder);
#endif //CONFIG_FRAPS_DECODER
#ifdef CONFIG_FAAD
#ifdef CONFIG_AAC_DECODER
    register_avcodec(&aac_decoder);
#endif //CONFIG_AAC_DECODER
#ifdef CONFIG_MPEG4AAC_DECODER
    register_avcodec(&mpeg4aac_decoder);
#endif //CONFIG_MPEG4AAC_DECODER
#endif
#ifdef CONFIG_MPEG1VIDEO_DECODER
    register_avcodec(&mpeg1video_decoder);
#endif //CONFIG_MPEG1VIDEO_DECODER
#ifdef CONFIG_MPEG2VIDEO_DECODER
    register_avcodec(&mpeg2video_decoder);
#endif //CONFIG_MPEG2VIDEO_DECODER
#ifdef CONFIG_MPEGVIDEO_DECODER
    register_avcodec(&mpegvideo_decoder);
#endif //CONFIG_MPEGVIDEO_DECODER
#ifdef HAVE_XVMC
#ifdef CONFIG_MPEG_XVMC_DECODER
    register_avcodec(&mpeg_xvmc_decoder);
#endif //CONFIG_MPEG_XVMC_DECODER
#endif
#ifdef CONFIG_DVVIDEO_DECODER
    register_avcodec(&dvvideo_decoder);
#endif //CONFIG_DVVIDEO_DECODER
#ifdef CONFIG_MJPEG_DECODER
    register_avcodec(&mjpeg_decoder);
#endif //CONFIG_MJPEG_DECODER
#ifdef CONFIG_MJPEGB_DECODER
    register_avcodec(&mjpegb_decoder);
#endif //CONFIG_MJPEGB_DECODER
#ifdef CONFIG_SP5X_DECODER
    register_avcodec(&sp5x_decoder);
#endif //CONFIG_SP5X_DECODER
#ifdef CONFIG_ZLIB
#ifdef CONFIG_PNG_DECODER
    register_avcodec(&png_decoder);
#endif //CONFIG_PNG_DECODER
#endif
#ifdef CONFIG_MP2_DECODER
    register_avcodec(&mp2_decoder);
#endif //CONFIG_MP2_DECODER
#ifdef CONFIG_MP3_DECODER
    register_avcodec(&mp3_decoder);
#endif //CONFIG_MP3_DECODER
#ifdef CONFIG_MP3ADU_DECODER
    register_avcodec(&mp3adu_decoder);
#endif //CONFIG_MP3ADU_DECODER
#ifdef CONFIG_MP3ON4_DECODER
    register_avcodec(&mp3on4_decoder);
#endif //CONFIG_MP3ON4_DECODER
#ifdef CONFIG_MACE3_DECODER
    register_avcodec(&mace3_decoder);
#endif //CONFIG_MACE3_DECODER
#ifdef CONFIG_MACE6_DECODER
    register_avcodec(&mace6_decoder);
#endif //CONFIG_MACE6_DECODER
#ifdef CONFIG_HUFFYUV_DECODER
    register_avcodec(&huffyuv_decoder);
#endif //CONFIG_HUFFYUV_DECODER
#ifdef CONFIG_FFVHUFF_DECODER
    register_avcodec(&ffvhuff_decoder);
#endif //CONFIG_FFVHUFF_DECODER
#ifdef CONFIG_FFV1_DECODER
    register_avcodec(&ffv1_decoder);
#endif //CONFIG_FFV1_DECODER
#ifdef CONFIG_SNOW_DECODER
    register_avcodec(&snow_decoder);
#endif //CONFIG_SNOW_DECODER
#ifdef CONFIG_CYUV_DECODER
    register_avcodec(&cyuv_decoder);
#endif //CONFIG_CYUV_DECODER
#ifdef CONFIG_H264_DECODER
    register_avcodec(&h264_decoder);
#endif //CONFIG_H264_DECODER
#ifdef CONFIG_VP3_DECODER
    register_avcodec(&vp3_decoder);
#endif //CONFIG_VP3_DECODER
#if (defined CONFIG_THEORA_DECODER && !defined CONFIG_LIBTHEORA)
    register_avcodec(&theora_decoder);
#endif //CONFIG_THEORA_DECODER
#ifdef CONFIG_ASV1_DECODER
    register_avcodec(&asv1_decoder);
#endif //CONFIG_ASV1_DECODER
#ifdef CONFIG_ASV2_DECODER
    register_avcodec(&asv2_decoder);
#endif //CONFIG_ASV2_DECODER
#ifdef CONFIG_VCR1_DECODER
    register_avcodec(&vcr1_decoder);
#endif //CONFIG_VCR1_DECODER
#ifdef CONFIG_CLJR_DECODER
    register_avcodec(&cljr_decoder);
#endif //CONFIG_CLJR_DECODER
#ifdef CONFIG_FOURXM_DECODER
    register_avcodec(&fourxm_decoder);
#endif //CONFIG_FOURXM_DECODER
#ifdef CONFIG_MDEC_DECODER
    register_avcodec(&mdec_decoder);
#endif //CONFIG_MDEC_DECODER
#ifdef CONFIG_ROQ_DECODER
    register_avcodec(&roq_decoder);
#endif //CONFIG_ROQ_DECODER
#ifdef CONFIG_INTERPLAY_VIDEO_DECODER
    register_avcodec(&interplay_video_decoder);
#endif //CONFIG_INTERPLAY_VIDEO_DECODER
#ifdef CONFIG_XAN_WC3_DECODER
    register_avcodec(&xan_wc3_decoder);
#endif //CONFIG_XAN_WC3_DECODER
#ifdef CONFIG_RPZA_DECODER
    register_avcodec(&rpza_decoder);
#endif //CONFIG_RPZA_DECODER
#ifdef CONFIG_CINEPAK_DECODER
    register_avcodec(&cinepak_decoder);
#endif //CONFIG_CINEPAK_DECODER
#ifdef CONFIG_MSRLE_DECODER
    register_avcodec(&msrle_decoder);
#endif //CONFIG_MSRLE_DECODER
#ifdef CONFIG_MSVIDEO1_DECODER
    register_avcodec(&msvideo1_decoder);
#endif //CONFIG_MSVIDEO1_DECODER
#ifdef CONFIG_VQA_DECODER
    register_avcodec(&vqa_decoder);
#endif //CONFIG_VQA_DECODER
#ifdef CONFIG_IDCIN_DECODER
    register_avcodec(&idcin_decoder);
#endif //CONFIG_IDCIN_DECODER
#ifdef CONFIG_EIGHTBPS_DECODER
    register_avcodec(&eightbps_decoder);
#endif //CONFIG_EIGHTBPS_DECODER
#ifdef CONFIG_SMC_DECODER
    register_avcodec(&smc_decoder);
#endif //CONFIG_SMC_DECODER
#ifdef CONFIG_FLIC_DECODER
    register_avcodec(&flic_decoder);
#endif //CONFIG_FLIC_DECODER
#ifdef CONFIG_TRUEMOTION1_DECODER
    register_avcodec(&truemotion1_decoder);
#endif //CONFIG_TRUEMOTION1_DECODER
#ifdef CONFIG_VMDVIDEO_DECODER
    register_avcodec(&vmdvideo_decoder);
#endif //CONFIG_VMDVIDEO_DECODER
#ifdef CONFIG_VMDAUDIO_DECODER
    register_avcodec(&vmdaudio_decoder);
#endif //CONFIG_VMDAUDIO_DECODER
#ifdef CONFIG_MSZH_DECODER
    register_avcodec(&mszh_decoder);
#endif //CONFIG_MSZH_DECODER
#ifdef CONFIG_ZLIB_DECODER
    register_avcodec(&zlib_decoder);
#endif //CONFIG_ZLIB_DECODER
#ifdef CONFIG_SONIC_DECODER
    register_avcodec(&sonic_decoder);
#endif //CONFIG_SONIC_DECODER
#ifdef CONFIG_AC3
#ifdef CONFIG_AC3_DECODER
    register_avcodec(&ac3_decoder);
#endif //CONFIG_AC3_DECODER
#endif
#ifdef CONFIG_DTS
#ifdef CONFIG_DTS_DECODER
    register_avcodec(&dts_decoder);
#endif //CONFIG_DTS_DECODER
#endif
#ifdef CONFIG_RA_144_DECODER
    register_avcodec(&ra_144_decoder);
#endif //CONFIG_RA_144_DECODER
#ifdef CONFIG_RA_288_DECODER
    register_avcodec(&ra_288_decoder);
#endif //CONFIG_RA_288_DECODER
#ifdef CONFIG_ROQ_DPCM_DECODER
    register_avcodec(&roq_dpcm_decoder);
#endif //CONFIG_ROQ_DPCM_DECODER
#ifdef CONFIG_INTERPLAY_DPCM_DECODER
    register_avcodec(&interplay_dpcm_decoder);
#endif //CONFIG_INTERPLAY_DPCM_DECODER
#ifdef CONFIG_XAN_DPCM_DECODER
    register_avcodec(&xan_dpcm_decoder);
#endif //CONFIG_XAN_DPCM_DECODER
#ifdef CONFIG_SOL_DPCM_DECODER
    register_avcodec(&sol_dpcm_decoder);
#endif //CONFIG_SOL_DPCM_DECODER
#ifdef CONFIG_QTRLE_DECODER
    register_avcodec(&qtrle_decoder);
#endif //CONFIG_QTRLE_DECODER
#ifdef CONFIG_FLAC_DECODER
    register_avcodec(&flac_decoder);
#endif //CONFIG_FLAC_DECODER
#ifdef CONFIG_SHORTEN_DECODER
    register_avcodec(&shorten_decoder);
#endif //CONFIG_SHORTEN_DECODER
#ifdef CONFIG_ALAC_DECODER
    register_avcodec(&alac_decoder);
#endif //CONFIG_ALAC_DECODER
#ifdef CONFIG_WS_SND1_DECODER
    register_avcodec(&ws_snd1_decoder);
#endif //CONFIG_WS_SND1_DECODER
#ifdef CONFIG_VORBIS_DECODER
    register_avcodec(&vorbis_decoder);
#endif
#ifdef CONFIG_LIBGSM
    register_avcodec(&libgsm_decoder);
#endif //CONFIG_LIBGSM
#endif /* CONFIG_DECODERS */

#ifdef AMR_NB
#ifdef CONFIG_AMR_NB_DECODER
    register_avcodec(&amr_nb_decoder);
#endif //CONFIG_AMR_NB_DECODER
#ifdef CONFIG_ENCODERS
#ifdef CONFIG_AMR_NB_ENCODER
    register_avcodec(&amr_nb_encoder);
#endif //CONFIG_AMR_NB_ENCODER
#endif //CONFIG_ENCODERS
#endif /* AMR_NB */

#ifdef AMR_WB
#ifdef CONFIG_AMR_WB_DECODER
    register_avcodec(&amr_wb_decoder);
#endif //CONFIG_AMR_WB_DECODER
#ifdef CONFIG_ENCODERS
#ifdef CONFIG_AMR_WB_ENCODER
    register_avcodec(&amr_wb_encoder);
#endif //CONFIG_AMR_WB_ENCODER
#endif //CONFIG_ENCODERS
#endif /* AMR_WB */

    /* pcm codecs */

#ifdef CONFIG_ENCODERS
#define PCM_CODEC(id, name) \
    register_avcodec(& name ## _encoder); \
    register_avcodec(& name ## _decoder); \

#else
#define PCM_CODEC(id, name) \
    register_avcodec(& name ## _decoder);
#endif

PCM_CODEC(CODEC_ID_PCM_S32LE, pcm_s32le);
PCM_CODEC(CODEC_ID_PCM_S32BE, pcm_s32be);
PCM_CODEC(CODEC_ID_PCM_U32LE, pcm_u32le);
PCM_CODEC(CODEC_ID_PCM_U32BE, pcm_u32be);
PCM_CODEC(CODEC_ID_PCM_S24LE, pcm_s24le);
PCM_CODEC(CODEC_ID_PCM_S24BE, pcm_s24be);
PCM_CODEC(CODEC_ID_PCM_U24LE, pcm_u24le);
PCM_CODEC(CODEC_ID_PCM_U24BE, pcm_u24be);
PCM_CODEC(CODEC_ID_PCM_S24DAUD, pcm_s24daud);
PCM_CODEC(CODEC_ID_PCM_S16LE, pcm_s16le);
PCM_CODEC(CODEC_ID_PCM_S16BE, pcm_s16be);
PCM_CODEC(CODEC_ID_PCM_U16LE, pcm_u16le);
PCM_CODEC(CODEC_ID_PCM_U16BE, pcm_u16be);
PCM_CODEC(CODEC_ID_PCM_S8, pcm_s8);
PCM_CODEC(CODEC_ID_PCM_U8, pcm_u8);
PCM_CODEC(CODEC_ID_PCM_ALAW, pcm_alaw);
PCM_CODEC(CODEC_ID_PCM_MULAW, pcm_mulaw);

    /* adpcm codecs */
PCM_CODEC(CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WS, adpcm_ima_ws);
PCM_CODEC(CODEC_ID_ADPCM_IMA_SMJPEG, adpcm_ima_smjpeg);
PCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);
PCM_CODEC(CODEC_ID_ADPCM_4XM, adpcm_4xm);
PCM_CODEC(CODEC_ID_ADPCM_XA, adpcm_xa);
PCM_CODEC(CODEC_ID_ADPCM_ADX, adpcm_adx);
PCM_CODEC(CODEC_ID_ADPCM_EA, adpcm_ea);
PCM_CODEC(CODEC_ID_ADPCM_G726, adpcm_g726);
PCM_CODEC(CODEC_ID_ADPCM_CT, adpcm_ct);
PCM_CODEC(CODEC_ID_ADPCM_SWF, adpcm_swf);
PCM_CODEC(CODEC_ID_ADPCM_YAMAHA, adpcm_yamaha);

#undef PCM_CODEC

    /* subtitles */ 
    register_avcodec(&dvdsub_decoder);
    register_avcodec(&dvbsub_encoder);
    register_avcodec(&dvbsub_decoder);

    /* parsers */ 
    av_register_codec_parser(&mpegvideo_parser);
    av_register_codec_parser(&mpeg4video_parser);
#if defined(CONFIG_H261_DECODER) || defined(CONFIG_H261_ENCODER)
    av_register_codec_parser(&h261_parser);
#endif
    av_register_codec_parser(&h263_parser);
#ifdef CONFIG_H264_DECODER
    av_register_codec_parser(&h264_parser);
#endif
    av_register_codec_parser(&mjpeg_parser);
    av_register_codec_parser(&pnm_parser);

    av_register_codec_parser(&mpegaudio_parser);
#ifdef CONFIG_AC3
    av_register_codec_parser(&ac3_parser);
#endif
    av_register_codec_parser(&dvdsub_parser);
    av_register_codec_parser(&dvbsub_parser);
}

