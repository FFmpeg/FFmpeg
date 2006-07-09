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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#ifdef CONFIG_FLAC_ENCODER
    register_avcodec(&flac_encoder);
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
#ifdef CONFIG_JPEGLS_ENCODER
    register_avcodec(&jpegls_encoder);
#endif //CONFIG_JPEGLS_ENCODER
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
#ifdef CONFIG_RAWVIDEO_ENCODER
    register_avcodec(&rawvideo_encoder);
#endif //CONFIG_RAWVIDEO_ENCODER
#endif /* CONFIG_ENCODERS */

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
#ifdef CONFIG_VC1_DECODER
    register_avcodec(&vc1_decoder);
#endif //CONFIG_VC1_DECODER
/* Reenable when it stops crashing on every file, causing bug report spam.
#ifdef CONFIG_WMV3_DECODER
    register_avcodec(&wmv3_decoder);
#endif //CONFIG_WMV3_DECODER
*/
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
#ifdef CONFIG_CSCD_DECODER
    register_avcodec(&cscd_decoder);
#endif //CONFIG_CSCD_DECODER
#ifdef CONFIG_NUV_DECODER
    register_avcodec(&nuv_decoder);
#endif //CONFIG_NUV_DECODER
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
#ifdef CONFIG_KMVC_DECODER
    register_avcodec(&kmvc_decoder);
#endif //CONFIG_KMVC_DECODER
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
#ifdef CONFIG_TRUEMOTION2_DECODER
    register_avcodec(&truemotion2_decoder);
#endif //CONFIG_TRUEMOTION2_DECODER
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
#ifdef CONFIG_ZMBV_DECODER
    register_avcodec(&zmbv_decoder);
#endif //CONFIG_ZMBV_DECODER
#ifdef CONFIG_SMACKER_DECODER
    register_avcodec(&smacker_decoder);
#endif //CONFIG_SMACKER_DECODER
#ifdef CONFIG_SMACKAUD_DECODER
    register_avcodec(&smackaud_decoder);
#endif //CONFIG_SMACKAUD_DECODER
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
#ifdef CONFIG_QDM2_DECODER
    register_avcodec(&qdm2_decoder);
#endif //CONFIG_QDM2_DECODER
#ifdef CONFIG_COOK_DECODER
    register_avcodec(&cook_decoder);
#endif //CONFIG_COOK_DECODER
#ifdef CONFIG_TRUESPEECH_DECODER
    register_avcodec(&truespeech_decoder);
#endif //CONFIG_TRUESPEECH_DECODER
#ifdef CONFIG_TTA_DECODER
    register_avcodec(&tta_decoder);
#endif //CONFIG_TTA_DECODER
#ifdef CONFIG_AVS_DECODER
    register_avcodec(&avs_decoder);
#endif //CONFIG_AVS_DECODER
#ifdef CONFIG_CAVS_DECODER
    register_avcodec(&cavs_decoder);
#endif //CONFIG_CAVS_DECODER
#ifdef CONFIG_RAWVIDEO_DECODER
    register_avcodec(&rawvideo_decoder);
#endif //CONFIG_RAWVIDEO_DECODER
#ifdef CONFIG_FLASHSV_DECODER
    register_avcodec(&flashsv_decoder);
#endif //CONFIG_FLASHSV_DECODER
#endif /* CONFIG_DECODERS */

#if defined(AMR_NB) || defined(AMR_NB_FIXED)
#ifdef CONFIG_AMR_NB_DECODER
    register_avcodec(&amr_nb_decoder);
#endif //CONFIG_AMR_NB_DECODER
#ifdef CONFIG_ENCODERS
#ifdef CONFIG_AMR_NB_ENCODER
    register_avcodec(&amr_nb_encoder);
#endif //CONFIG_AMR_NB_ENCODER
#endif //CONFIG_ENCODERS
#endif /* AMR_NB || AMR_NB_FIXED */

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

#ifdef CONFIG_BMP_DECODER
    register_avcodec(&bmp_decoder);
#endif

#if CONFIG_MMVIDEO_DECODER
    register_avcodec(&mmvideo_decoder);
#endif //CONFIG_MMVIDEO_DECODER

    /* pcm codecs */
#ifdef CONFIG_PCM_S32LE_DECODER
    register_avcodec(&pcm_s32le_decoder);
#endif
#ifdef CONFIG_PCM_S32LE_ENCODER
    register_avcodec(&pcm_s32le_encoder);
#endif
#ifdef CONFIG_PCM_S32BE_DECODER
    register_avcodec(&pcm_s32be_decoder);
#endif
#ifdef CONFIG_PCM_S32BE_ENCODER
    register_avcodec(&pcm_s32be_encoder);
#endif
#ifdef CONFIG_PCM_U32LE_DECODER
    register_avcodec(&pcm_u32le_decoder);
#endif
#ifdef CONFIG_PCM_U32LE_ENCODER
    register_avcodec(&pcm_u32le_encoder);
#endif
#ifdef CONFIG_PCM_U32BE_DECODER
    register_avcodec(&pcm_u32be_decoder);
#endif
#ifdef CONFIG_PCM_U32BE_ENCODER
    register_avcodec(&pcm_u32be_encoder);
#endif
#ifdef CONFIG_PCM_S24LE_DECODER
    register_avcodec(&pcm_s24le_decoder);
#endif
#ifdef CONFIG_PCM_S24LE_ENCODER
    register_avcodec(&pcm_s24le_encoder);
#endif
#ifdef CONFIG_PCM_S24BE_DECODER
    register_avcodec(&pcm_s24be_decoder);
#endif
#ifdef CONFIG_PCM_S24BE_ENCODER
    register_avcodec(&pcm_s24be_encoder);
#endif
#ifdef CONFIG_PCM_U24LE_DECODER
    register_avcodec(&pcm_u24le_decoder);
#endif
#ifdef CONFIG_PCM_U24LE_ENCODER
    register_avcodec(&pcm_u24le_encoder);
#endif
#ifdef CONFIG_PCM_U24BE_DECODER
    register_avcodec(&pcm_u24be_decoder);
#endif
#ifdef CONFIG_PCM_U24BE_ENCODER
    register_avcodec(&pcm_u24be_encoder);
#endif
#ifdef CONFIG_PCM_S24DAUD_DECODER
    register_avcodec(&pcm_s24daud_decoder);
#endif
#ifdef CONFIG_PCM_S24DAUD_ENCODER
    register_avcodec(&pcm_s24daud_encoder);
#endif
#ifdef CONFIG_PCM_S16LE_DECODER
    register_avcodec(&pcm_s16le_decoder);
#endif
#ifdef CONFIG_PCM_S16LE_ENCODER
    register_avcodec(&pcm_s16le_encoder);
#endif
#ifdef CONFIG_PCM_S16BE_DECODER
    register_avcodec(&pcm_s16be_decoder);
#endif
#ifdef CONFIG_PCM_S16BE_ENCODER
    register_avcodec(&pcm_s16be_encoder);
#endif
#ifdef CONFIG_PCM_U16LE_DECODER
    register_avcodec(&pcm_u16le_decoder);
#endif
#ifdef CONFIG_PCM_U16LE_ENCODER
    register_avcodec(&pcm_u16le_encoder);
#endif
#ifdef CONFIG_PCM_U16BE_DECODER
    register_avcodec(&pcm_u16be_decoder);
#endif
#ifdef CONFIG_PCM_U16BE_ENCODER
    register_avcodec(&pcm_u16be_encoder);
#endif
#ifdef CONFIG_PCM_S8_DECODER
    register_avcodec(&pcm_s8_decoder);
#endif
#ifdef CONFIG_PCM_S8_ENCODER
    register_avcodec(&pcm_s8_encoder);
#endif
#ifdef CONFIG_PCM_U8_DECODER
    register_avcodec(&pcm_u8_decoder);
#endif
#ifdef CONFIG_PCM_U8_ENCODER
    register_avcodec(&pcm_u8_encoder);
#endif
#ifdef CONFIG_PCM_ALAW_DECODER
    register_avcodec(&pcm_alaw_decoder);
#endif
#ifdef CONFIG_PCM_ALAW_ENCODER
    register_avcodec(&pcm_alaw_encoder);
#endif
#ifdef CONFIG_PCM_MULAW_DECODER
    register_avcodec(&pcm_mulaw_decoder);
#endif
#ifdef CONFIG_PCM_MULAW_ENCODER
    register_avcodec(&pcm_mulaw_encoder);
#endif

   /* adpcm codecs */
#ifdef CONFIG_ADPCM_IMA_QT_DECODER
    register_avcodec(&adpcm_ima_qt_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_QT_ENCODER
    register_avcodec(&adpcm_ima_qt_encoder);
#endif
#ifdef CONFIG_ADPCM_IMA_WAV_DECODER
    register_avcodec(&adpcm_ima_wav_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_WAV_ENCODER
    register_avcodec(&adpcm_ima_wav_encoder);
#endif
#ifdef CONFIG_ADPCM_IMA_DK3_DECODER
    register_avcodec(&adpcm_ima_dk3_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_DK3_ENCODER
    register_avcodec(&adpcm_ima_dk3_encoder);
#endif
#ifdef CONFIG_ADPCM_IMA_DK4_DECODER
    register_avcodec(&adpcm_ima_dk4_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_DK4_ENCODER
    register_avcodec(&adpcm_ima_dk4_encoder);
#endif
#ifdef CONFIG_ADPCM_IMA_WS_DECODER
    register_avcodec(&adpcm_ima_ws_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_WS_ENCODER
    register_avcodec(&adpcm_ima_ws_encoder);
#endif
#ifdef CONFIG_ADPCM_IMA_SMJPEG_DECODER
    register_avcodec(&adpcm_ima_smjpeg_decoder);
#endif
#ifdef CONFIG_ADPCM_IMA_SMJPEG_ENCODER
    register_avcodec(&adpcm_ima_smjpeg_encoder);
#endif
#ifdef CONFIG_ADPCM_MS_DECODER
    register_avcodec(&adpcm_ms_decoder);
#endif
#ifdef CONFIG_ADPCM_MS_ENCODER
    register_avcodec(&adpcm_ms_encoder);
#endif
#ifdef CONFIG_ADPCM_4XM_DECODER
    register_avcodec(&adpcm_4xm_decoder);
#endif
#ifdef CONFIG_ADPCM_4XM_ENCODER
    register_avcodec(&adpcm_4xm_encoder);
#endif
#ifdef CONFIG_ADPCM_XA_DECODER
    register_avcodec(&adpcm_xa_decoder);
#endif
#ifdef CONFIG_ADPCM_XA_ENCODER
    register_avcodec(&adpcm_xa_encoder);
#endif
#ifdef CONFIG_ADPCM_ADX_DECODER
    register_avcodec(&adpcm_adx_decoder);
#endif
#ifdef CONFIG_ADPCM_ADX_ENCODER
    register_avcodec(&adpcm_adx_encoder);
#endif
#ifdef CONFIG_ADPCM_EA_DECODER
    register_avcodec(&adpcm_ea_decoder);
#endif
#ifdef CONFIG_ADPCM_EA_ENCODER
    register_avcodec(&adpcm_ea_encoder);
#endif
#ifdef CONFIG_ADPCM_G726_DECODER
    register_avcodec(&adpcm_g726_decoder);
#endif
#ifdef CONFIG_ADPCM_G726_ENCODER
    register_avcodec(&adpcm_g726_encoder);
#endif
#ifdef CONFIG_ADPCM_CT_DECODER
    register_avcodec(&adpcm_ct_decoder);
#endif
#ifdef CONFIG_ADPCM_CT_ENCODER
    register_avcodec(&adpcm_ct_encoder);
#endif
#ifdef CONFIG_ADPCM_SWF_DECODER
    register_avcodec(&adpcm_swf_decoder);
#endif
#ifdef CONFIG_ADPCM_SWF_ENCODER
    register_avcodec(&adpcm_swf_encoder);
#endif
#ifdef CONFIG_ADPCM_YAMAHA_DECODER
    register_avcodec(&adpcm_yamaha_decoder);
#endif
#ifdef CONFIG_ADPCM_YAMAHA_ENCODER
    register_avcodec(&adpcm_yamaha_encoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_4_DECODER
    register_avcodec(&adpcm_sbpro_4_decoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_4_ENCODER
    register_avcodec(&adpcm_sbpro_4_encoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_3_DECODER
    register_avcodec(&adpcm_sbpro_3_decoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_3_ENCODER
    register_avcodec(&adpcm_sbpro_3_encoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_2_DECODER
    register_avcodec(&adpcm_sbpro_2_decoder);
#endif
#ifdef CONFIG_ADPCM_SBPRO_2_ENCODER
    register_avcodec(&adpcm_sbpro_2_encoder);
#endif

    /* subtitles */
#ifdef CONFIG_DVDSUB_DECODER
    register_avcodec(&dvdsub_decoder);
#endif
#ifdef CONFIG_DVDSUB_ENCODER
    register_avcodec(&dvdsub_encoder);
#endif

#ifdef CONFIG_DVBSUB_DECODER
    register_avcodec(&dvbsub_decoder);
#endif
#ifdef CONFIG_DVBSUB_ENCODER
    register_avcodec(&dvbsub_encoder);
#endif

    /* parsers */
#ifdef CONFIG_MPEGVIDEO_PARSER
    av_register_codec_parser(&mpegvideo_parser);
#endif
#ifdef CONFIG_MPEG4VIDEO_PARSER
    av_register_codec_parser(&mpeg4video_parser);
#endif
#ifdef CONFIG_CAVSVIDEO_PARSER
    av_register_codec_parser(&cavsvideo_parser);
#endif
#ifdef CONFIG_H261_PARSER
    av_register_codec_parser(&h261_parser);
#endif
#ifdef CONFIG_H263_PARSER
    av_register_codec_parser(&h263_parser);
#endif
#ifdef CONFIG_H264_PARSER
    av_register_codec_parser(&h264_parser);
#endif
#ifdef CONFIG_MJPEG_PARSER
    av_register_codec_parser(&mjpeg_parser);
#endif
#ifdef CONFIG_PNM_PARSER
    av_register_codec_parser(&pnm_parser);
#endif
#ifdef CONFIG_MPEGAUDIO_PARSER
    av_register_codec_parser(&mpegaudio_parser);
#endif
#ifdef CONFIG_AC3_PARSER
    av_register_codec_parser(&ac3_parser);
#endif
#ifdef CONFIG_DVDSUB_PARSER
    av_register_codec_parser(&dvdsub_parser);
#endif
#ifdef CONFIG_DVBSUB_PARSER
    av_register_codec_parser(&dvbsub_parser);
#endif
#ifdef CONFIG_AAC_PARSER
    av_register_codec_parser(&aac_parser);
#endif

    av_register_bitstream_filter(&dump_extradata_bsf);
    av_register_bitstream_filter(&remove_extradata_bsf);
    av_register_bitstream_filter(&noise_bsf);
}

