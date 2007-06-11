/*
 * copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef ALLCODECS_H
#define ALLCODECS_H

#include "avcodec.h"

extern AVCodec ac3_encoder;
extern AVCodec asv1_encoder;
extern AVCodec asv2_encoder;
extern AVCodec bmp_encoder;
extern AVCodec dvvideo_encoder;
extern AVCodec ffv1_encoder;
extern AVCodec ffvhuff_encoder;
extern AVCodec flac_encoder;
extern AVCodec flashsv_encoder;
extern AVCodec flv_encoder;
extern AVCodec gif_encoder;
extern AVCodec h261_encoder;
extern AVCodec h263_encoder;
extern AVCodec h263p_encoder;
extern AVCodec h264_encoder;
extern AVCodec huffyuv_encoder;
extern AVCodec jpegls_encoder;
extern AVCodec ljpeg_encoder;
extern AVCodec mdec_encoder;
extern AVCodec mjpeg_encoder;
extern AVCodec mp2_encoder;
extern AVCodec mpeg1video_encoder;
extern AVCodec mpeg2video_encoder;
extern AVCodec mpeg4_encoder;
extern AVCodec msmpeg4v1_encoder;
extern AVCodec msmpeg4v2_encoder;
extern AVCodec msmpeg4v3_encoder;
extern AVCodec pam_encoder;
extern AVCodec pbm_encoder;
extern AVCodec pgm_encoder;
extern AVCodec pgmyuv_encoder;
extern AVCodec png_encoder;
extern AVCodec ppm_encoder;
extern AVCodec roq_dpcm_encoder;
extern AVCodec rv10_encoder;
extern AVCodec rv20_encoder;
extern AVCodec sgi_encoder;
extern AVCodec snow_encoder;
extern AVCodec sonic_encoder;
extern AVCodec sonic_ls_encoder;
extern AVCodec svq1_encoder;
extern AVCodec targa_encoder;
extern AVCodec tiff_encoder;
extern AVCodec vcr1_encoder;
extern AVCodec vorbis_encoder;
extern AVCodec wmav1_encoder;
extern AVCodec wmav2_encoder;
extern AVCodec wmv1_encoder;
extern AVCodec wmv2_encoder;
extern AVCodec zmbv_encoder;

extern AVCodec aasc_decoder;
extern AVCodec alac_decoder;
extern AVCodec asv1_decoder;
extern AVCodec asv2_decoder;
extern AVCodec atrac3_decoder;
extern AVCodec avs_decoder;
extern AVCodec bethsoftvid_decoder;
extern AVCodec bmp_decoder;
extern AVCodec c93_decoder;
extern AVCodec cavs_decoder;
extern AVCodec cinepak_decoder;
extern AVCodec cljr_decoder;
extern AVCodec cook_decoder;
extern AVCodec cscd_decoder;
extern AVCodec cyuv_decoder;
extern AVCodec dca_decoder;
extern AVCodec dnxhd_decoder;
extern AVCodec dsicinaudio_decoder;
extern AVCodec dsicinvideo_decoder;
extern AVCodec dvvideo_decoder;
extern AVCodec dxa_decoder;
extern AVCodec eightbps_decoder;
extern AVCodec ffv1_decoder;
extern AVCodec ffvhuff_decoder;
extern AVCodec flac_decoder;
extern AVCodec flashsv_decoder;
extern AVCodec flic_decoder;
extern AVCodec flv_decoder;
extern AVCodec fourxm_decoder;
extern AVCodec fraps_decoder;
extern AVCodec gif_decoder;
extern AVCodec h261_decoder;
extern AVCodec h263_decoder;
extern AVCodec h263i_decoder;
extern AVCodec h264_decoder;
extern AVCodec huffyuv_decoder;
extern AVCodec idcin_decoder;
extern AVCodec imc_decoder;
extern AVCodec indeo2_decoder;
extern AVCodec indeo3_decoder;
extern AVCodec interplay_dpcm_decoder;
extern AVCodec interplay_video_decoder;
extern AVCodec jpegls_decoder;
extern AVCodec kmvc_decoder;
extern AVCodec loco_decoder;
extern AVCodec mace3_decoder;
extern AVCodec mace6_decoder;
extern AVCodec mdec_decoder;
extern AVCodec mjpeg_decoder;
extern AVCodec mjpegb_decoder;
extern AVCodec mmvideo_decoder;
extern AVCodec mp2_decoder;
extern AVCodec mp3_decoder;
extern AVCodec mp3adu_decoder;
extern AVCodec mp3on4_decoder;
extern AVCodec mpc7_decoder;
extern AVCodec mpeg1video_decoder;
extern AVCodec mpeg2video_decoder;
extern AVCodec mpeg4_decoder;
extern AVCodec mpeg_xvmc_decoder;
extern AVCodec mpegvideo_decoder;
extern AVCodec msmpeg4v1_decoder;
extern AVCodec msmpeg4v2_decoder;
extern AVCodec msmpeg4v3_decoder;
extern AVCodec msrle_decoder;
extern AVCodec msvideo1_decoder;
extern AVCodec mszh_decoder;
extern AVCodec nuv_decoder;
extern AVCodec png_decoder;
extern AVCodec ptx_decoder;
extern AVCodec qdm2_decoder;
extern AVCodec qdraw_decoder;
extern AVCodec qpeg_decoder;
extern AVCodec qtrle_decoder;
extern AVCodec ra_144_decoder;
extern AVCodec ra_288_decoder;
extern AVCodec roq_decoder;
extern AVCodec roq_dpcm_decoder;
extern AVCodec rpza_decoder;
extern AVCodec rv10_decoder;
extern AVCodec rv20_decoder;
extern AVCodec rv30_decoder;
extern AVCodec rv40_decoder;
extern AVCodec sgi_decoder;
extern AVCodec shorten_decoder;
extern AVCodec smackaud_decoder;
extern AVCodec smacker_decoder;
extern AVCodec smc_decoder;
extern AVCodec snow_decoder;
extern AVCodec sol_dpcm_decoder;
extern AVCodec sonic_decoder;
extern AVCodec sp5x_decoder;
extern AVCodec svq1_decoder;
extern AVCodec svq3_decoder;
extern AVCodec targa_decoder;
extern AVCodec theora_decoder;
extern AVCodec thp_decoder;
extern AVCodec tiertexseqvideo_decoder;
extern AVCodec tiff_decoder;
extern AVCodec truemotion1_decoder;
extern AVCodec truemotion2_decoder;
extern AVCodec truespeech_decoder;
extern AVCodec tscc_decoder;
extern AVCodec tta_decoder;
extern AVCodec txd_decoder;
extern AVCodec ulti_decoder;
extern AVCodec vc1_decoder;
extern AVCodec vcr1_decoder;
extern AVCodec vmdaudio_decoder;
extern AVCodec vmdvideo_decoder;
extern AVCodec vmnc_decoder;
extern AVCodec vorbis_decoder;
extern AVCodec vp3_decoder;
extern AVCodec vp5_decoder;
extern AVCodec vp6_decoder;
extern AVCodec vp6f_decoder;
extern AVCodec vqa_decoder;
extern AVCodec wavpack_decoder;
extern AVCodec wmav1_decoder;
extern AVCodec wmav2_decoder;
extern AVCodec wmv1_decoder;
extern AVCodec wmv2_decoder;
extern AVCodec wmv3_decoder;
extern AVCodec wnv1_decoder;
extern AVCodec ws_snd1_decoder;
extern AVCodec xan_dpcm_decoder;
extern AVCodec xan_wc3_decoder;
extern AVCodec xl_decoder;
extern AVCodec zmbv_decoder;

/* PCM codecs */
#define PCM_CODEC(id, name) \
extern AVCodec name ## _decoder; \
extern AVCodec name ## _encoder

PCM_CODEC(CODEC_ID_PCM_ALAW,    pcm_alaw);
PCM_CODEC(CODEC_ID_PCM_MULAW,   pcm_mulaw);
PCM_CODEC(CODEC_ID_PCM_S8,      pcm_s8);
PCM_CODEC(CODEC_ID_PCM_S16BE,   pcm_s16be);
PCM_CODEC(CODEC_ID_PCM_S16LE,   pcm_s16le);
PCM_CODEC(CODEC_ID_PCM_S24BE,   pcm_s24be);
PCM_CODEC(CODEC_ID_PCM_S24DAUD, pcm_s24daud);
PCM_CODEC(CODEC_ID_PCM_S24LE,   pcm_s24le);
PCM_CODEC(CODEC_ID_PCM_S32BE,   pcm_s32be);
PCM_CODEC(CODEC_ID_PCM_S32LE,   pcm_s32le);
PCM_CODEC(CODEC_ID_PCM_U8,      pcm_u8);
PCM_CODEC(CODEC_ID_PCM_U16BE,   pcm_u16be);
PCM_CODEC(CODEC_ID_PCM_U16LE,   pcm_u16le);
PCM_CODEC(CODEC_ID_PCM_U24BE,   pcm_u24be);
PCM_CODEC(CODEC_ID_PCM_U24LE,   pcm_u24le);
PCM_CODEC(CODEC_ID_PCM_U32BE,   pcm_u32be);
PCM_CODEC(CODEC_ID_PCM_U32LE,   pcm_u32le);

/* ADPCM codecs */

PCM_CODEC(CODEC_ID_ADPCM_4XM,     adpcm_4xm);
PCM_CODEC(CODEC_ID_ADPCM_ADX,     adpcm_adx);
PCM_CODEC(CODEC_ID_ADPCM_CT,      adpcm_ct);
PCM_CODEC(CODEC_ID_ADPCM_EA,      adpcm_ea);
PCM_CODEC(CODEC_ID_ADPCM_G726,    adpcm_g726);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4);
PCM_CODEC(CODEC_ID_ADPCM_IMA_QT,  adpcm_ima_qt);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WS,  adpcm_ima_ws);
PCM_CODEC(CODEC_ID_ADPCM_MS,      adpcm_ms);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_2, adpcm_sbpro_2);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_3, adpcm_sbpro_3);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_4, adpcm_sbpro_4);
PCM_CODEC(CODEC_ID_ADPCM_SMJPEG,  adpcm_ima_smjpeg);
PCM_CODEC(CODEC_ID_ADPCM_SWF,     adpcm_swf);
PCM_CODEC(CODEC_ID_ADPCM_THP,     adpcm_thp);
PCM_CODEC(CODEC_ID_ADPCM_XA,      adpcm_xa);
PCM_CODEC(CODEC_ID_ADPCM_YAMAHA,  adpcm_yamaha);

#undef PCM_CODEC

/* dummy raw video codec */
extern AVCodec rawvideo_decoder;
extern AVCodec rawvideo_encoder;

/* the following codecs use external libs */
extern AVCodec liba52_decoder;
extern AVCodec libamr_nb_decoder;
extern AVCodec libamr_nb_encoder;
extern AVCodec libamr_wb_decoder;
extern AVCodec libamr_wb_encoder;
extern AVCodec libfaac_encoder;
extern AVCodec libfaad_decoder;
extern AVCodec libgsm_decoder;
extern AVCodec libgsm_encoder;
extern AVCodec libgsm_ms_decoder;
extern AVCodec libgsm_ms_encoder;
extern AVCodec libmp3lame_encoder;
extern AVCodec libtheora_encoder;
extern AVCodec libvorbis_decoder;
extern AVCodec libvorbis_encoder;
extern AVCodec libx264_encoder;
extern AVCodec libxvid_encoder;
extern AVCodec mpeg4aac_decoder;
extern AVCodec zlib_decoder;
extern AVCodec zlib_encoder;

/* subtitles */
extern AVCodec dvbsub_decoder;
extern AVCodec dvbsub_encoder;
extern AVCodec dvdsub_decoder;
extern AVCodec dvdsub_encoder;

#endif /* ALLCODECS_H */
