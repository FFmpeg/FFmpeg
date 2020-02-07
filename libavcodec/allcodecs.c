/*
 * Provide registration of all codecs, parsers and bitstream filters for libavcodec.
 * Copyright (c) 2002 Fabrice Bellard
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Provide registration of all codecs, parsers and bitstream filters for libavcodec.
 */

#include "config.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "version.h"

extern AVCodec ff_a64multi_encoder;
extern AVCodec ff_a64multi5_encoder;
extern AVCodec ff_aasc_decoder;
extern AVCodec ff_aic_decoder;
extern AVCodec ff_alias_pix_encoder;
extern AVCodec ff_alias_pix_decoder;
extern AVCodec ff_agm_decoder;
extern AVCodec ff_amv_encoder;
extern AVCodec ff_amv_decoder;
extern AVCodec ff_anm_decoder;
extern AVCodec ff_ansi_decoder;
extern AVCodec ff_apng_encoder;
extern AVCodec ff_apng_decoder;
extern AVCodec ff_arbc_decoder;
extern AVCodec ff_asv1_encoder;
extern AVCodec ff_asv1_decoder;
extern AVCodec ff_asv2_encoder;
extern AVCodec ff_asv2_decoder;
extern AVCodec ff_aura_decoder;
extern AVCodec ff_aura2_decoder;
extern AVCodec ff_avrp_encoder;
extern AVCodec ff_avrp_decoder;
extern AVCodec ff_avrn_decoder;
extern AVCodec ff_avs_decoder;
extern AVCodec ff_avui_encoder;
extern AVCodec ff_avui_decoder;
extern AVCodec ff_ayuv_encoder;
extern AVCodec ff_ayuv_decoder;
extern AVCodec ff_bethsoftvid_decoder;
extern AVCodec ff_bfi_decoder;
extern AVCodec ff_bink_decoder;
extern AVCodec ff_bitpacked_decoder;
extern AVCodec ff_bmp_encoder;
extern AVCodec ff_bmp_decoder;
extern AVCodec ff_bmv_video_decoder;
extern AVCodec ff_brender_pix_decoder;
extern AVCodec ff_c93_decoder;
extern AVCodec ff_cavs_decoder;
extern AVCodec ff_cdgraphics_decoder;
extern AVCodec ff_cdtoons_decoder;
extern AVCodec ff_cdxl_decoder;
extern AVCodec ff_cfhd_decoder;
extern AVCodec ff_cinepak_encoder;
extern AVCodec ff_cinepak_decoder;
extern AVCodec ff_clearvideo_decoder;
extern AVCodec ff_cljr_encoder;
extern AVCodec ff_cljr_decoder;
extern AVCodec ff_cllc_decoder;
extern AVCodec ff_comfortnoise_encoder;
extern AVCodec ff_comfortnoise_decoder;
extern AVCodec ff_cpia_decoder;
extern AVCodec ff_cscd_decoder;
extern AVCodec ff_cyuv_decoder;
extern AVCodec ff_dds_decoder;
extern AVCodec ff_dfa_decoder;
extern AVCodec ff_dirac_decoder;
extern AVCodec ff_dnxhd_encoder;
extern AVCodec ff_dnxhd_decoder;
extern AVCodec ff_dpx_encoder;
extern AVCodec ff_dpx_decoder;
extern AVCodec ff_dsicinvideo_decoder;
extern AVCodec ff_dvaudio_decoder;
extern AVCodec ff_dvvideo_encoder;
extern AVCodec ff_dvvideo_decoder;
extern AVCodec ff_dxa_decoder;
extern AVCodec ff_dxtory_decoder;
extern AVCodec ff_dxv_decoder;
extern AVCodec ff_eacmv_decoder;
extern AVCodec ff_eamad_decoder;
extern AVCodec ff_eatgq_decoder;
extern AVCodec ff_eatgv_decoder;
extern AVCodec ff_eatqi_decoder;
extern AVCodec ff_eightbps_decoder;
extern AVCodec ff_eightsvx_exp_decoder;
extern AVCodec ff_eightsvx_fib_decoder;
extern AVCodec ff_escape124_decoder;
extern AVCodec ff_escape130_decoder;
extern AVCodec ff_exr_decoder;
extern AVCodec ff_ffv1_encoder;
extern AVCodec ff_ffv1_decoder;
extern AVCodec ff_ffvhuff_encoder;
extern AVCodec ff_ffvhuff_decoder;
extern AVCodec ff_fic_decoder;
extern AVCodec ff_fits_encoder;
extern AVCodec ff_fits_decoder;
extern AVCodec ff_flashsv_encoder;
extern AVCodec ff_flashsv_decoder;
extern AVCodec ff_flashsv2_encoder;
extern AVCodec ff_flashsv2_decoder;
extern AVCodec ff_flic_decoder;
extern AVCodec ff_flv_encoder;
extern AVCodec ff_flv_decoder;
extern AVCodec ff_fmvc_decoder;
extern AVCodec ff_fourxm_decoder;
extern AVCodec ff_fraps_decoder;
extern AVCodec ff_frwu_decoder;
extern AVCodec ff_g2m_decoder;
extern AVCodec ff_gdv_decoder;
extern AVCodec ff_gif_encoder;
extern AVCodec ff_gif_decoder;
extern AVCodec ff_h261_encoder;
extern AVCodec ff_h261_decoder;
extern AVCodec ff_h263_encoder;
extern AVCodec ff_h263_decoder;
extern AVCodec ff_h263i_decoder;
extern AVCodec ff_h263p_encoder;
extern AVCodec ff_h263p_decoder;
extern AVCodec ff_h263_v4l2m2m_decoder;
extern AVCodec ff_h264_decoder;
extern AVCodec ff_h264_crystalhd_decoder;
extern AVCodec ff_h264_v4l2m2m_decoder;
extern AVCodec ff_h264_mediacodec_decoder;
extern AVCodec ff_h264_mmal_decoder;
extern AVCodec ff_h264_qsv_decoder;
extern AVCodec ff_h264_rkmpp_decoder;
extern AVCodec ff_hap_encoder;
extern AVCodec ff_hap_decoder;
extern AVCodec ff_hevc_decoder;
extern AVCodec ff_hevc_qsv_decoder;
extern AVCodec ff_hevc_rkmpp_decoder;
extern AVCodec ff_hevc_v4l2m2m_decoder;
extern AVCodec ff_hnm4_video_decoder;
extern AVCodec ff_hq_hqa_decoder;
extern AVCodec ff_hqx_decoder;
extern AVCodec ff_huffyuv_encoder;
extern AVCodec ff_huffyuv_decoder;
extern AVCodec ff_hymt_decoder;
extern AVCodec ff_idcin_decoder;
extern AVCodec ff_iff_ilbm_decoder;
extern AVCodec ff_imm4_decoder;
extern AVCodec ff_imm5_decoder;
extern AVCodec ff_indeo2_decoder;
extern AVCodec ff_indeo3_decoder;
extern AVCodec ff_indeo4_decoder;
extern AVCodec ff_indeo5_decoder;
extern AVCodec ff_interplay_video_decoder;
extern AVCodec ff_jpeg2000_encoder;
extern AVCodec ff_jpeg2000_decoder;
extern AVCodec ff_jpegls_encoder;
extern AVCodec ff_jpegls_decoder;
extern AVCodec ff_jv_decoder;
extern AVCodec ff_kgv1_decoder;
extern AVCodec ff_kmvc_decoder;
extern AVCodec ff_lagarith_decoder;
extern AVCodec ff_ljpeg_encoder;
extern AVCodec ff_loco_decoder;
extern AVCodec ff_lscr_decoder;
extern AVCodec ff_m101_decoder;
extern AVCodec ff_magicyuv_encoder;
extern AVCodec ff_magicyuv_decoder;
extern AVCodec ff_mdec_decoder;
extern AVCodec ff_mimic_decoder;
extern AVCodec ff_mjpeg_encoder;
extern AVCodec ff_mjpeg_decoder;
extern AVCodec ff_mjpegb_decoder;
extern AVCodec ff_mmvideo_decoder;
extern AVCodec ff_motionpixels_decoder;
extern AVCodec ff_mpeg1video_encoder;
extern AVCodec ff_mpeg1video_decoder;
extern AVCodec ff_mpeg2video_encoder;
extern AVCodec ff_mpeg2video_decoder;
extern AVCodec ff_mpeg4_encoder;
extern AVCodec ff_mpeg4_decoder;
extern AVCodec ff_mpeg4_crystalhd_decoder;
extern AVCodec ff_mpeg4_v4l2m2m_decoder;
extern AVCodec ff_mpeg4_mmal_decoder;
extern AVCodec ff_mpegvideo_decoder;
extern AVCodec ff_mpeg1_v4l2m2m_decoder;
extern AVCodec ff_mpeg2_mmal_decoder;
extern AVCodec ff_mpeg2_crystalhd_decoder;
extern AVCodec ff_mpeg2_v4l2m2m_decoder;
extern AVCodec ff_mpeg2_qsv_decoder;
extern AVCodec ff_mpeg2_mediacodec_decoder;
extern AVCodec ff_msa1_decoder;
extern AVCodec ff_mscc_decoder;
extern AVCodec ff_msmpeg4v1_decoder;
extern AVCodec ff_msmpeg4v2_encoder;
extern AVCodec ff_msmpeg4v2_decoder;
extern AVCodec ff_msmpeg4v3_encoder;
extern AVCodec ff_msmpeg4v3_decoder;
extern AVCodec ff_msmpeg4_crystalhd_decoder;
extern AVCodec ff_msrle_decoder;
extern AVCodec ff_mss1_decoder;
extern AVCodec ff_mss2_decoder;
extern AVCodec ff_msvideo1_encoder;
extern AVCodec ff_msvideo1_decoder;
extern AVCodec ff_mszh_decoder;
extern AVCodec ff_mts2_decoder;
extern AVCodec ff_mv30_decoder;
extern AVCodec ff_mvc1_decoder;
extern AVCodec ff_mvc2_decoder;
extern AVCodec ff_mvdv_decoder;
extern AVCodec ff_mvha_decoder;
extern AVCodec ff_mwsc_decoder;
extern AVCodec ff_mxpeg_decoder;
extern AVCodec ff_notchlc_decoder;
extern AVCodec ff_nuv_decoder;
extern AVCodec ff_paf_video_decoder;
extern AVCodec ff_pam_encoder;
extern AVCodec ff_pam_decoder;
extern AVCodec ff_pbm_encoder;
extern AVCodec ff_pbm_decoder;
extern AVCodec ff_pcx_encoder;
extern AVCodec ff_pcx_decoder;
extern AVCodec ff_pfm_decoder;
extern AVCodec ff_pgm_encoder;
extern AVCodec ff_pgm_decoder;
extern AVCodec ff_pgmyuv_encoder;
extern AVCodec ff_pgmyuv_decoder;
extern AVCodec ff_pictor_decoder;
extern AVCodec ff_pixlet_decoder;
extern AVCodec ff_png_encoder;
extern AVCodec ff_png_decoder;
extern AVCodec ff_ppm_encoder;
extern AVCodec ff_ppm_decoder;
extern AVCodec ff_prores_encoder;
extern AVCodec ff_prores_decoder;
extern AVCodec ff_prores_aw_encoder;
extern AVCodec ff_prores_ks_encoder;
extern AVCodec ff_prosumer_decoder;
extern AVCodec ff_psd_decoder;
extern AVCodec ff_ptx_decoder;
extern AVCodec ff_qdraw_decoder;
extern AVCodec ff_qpeg_decoder;
extern AVCodec ff_qtrle_encoder;
extern AVCodec ff_qtrle_decoder;
extern AVCodec ff_r10k_encoder;
extern AVCodec ff_r10k_decoder;
extern AVCodec ff_r210_encoder;
extern AVCodec ff_r210_decoder;
extern AVCodec ff_rasc_decoder;
extern AVCodec ff_rawvideo_encoder;
extern AVCodec ff_rawvideo_decoder;
extern AVCodec ff_rl2_decoder;
extern AVCodec ff_roq_encoder;
extern AVCodec ff_roq_decoder;
extern AVCodec ff_rpza_decoder;
extern AVCodec ff_rscc_decoder;
extern AVCodec ff_rv10_encoder;
extern AVCodec ff_rv10_decoder;
extern AVCodec ff_rv20_encoder;
extern AVCodec ff_rv20_decoder;
extern AVCodec ff_rv30_decoder;
extern AVCodec ff_rv40_decoder;
extern AVCodec ff_s302m_encoder;
extern AVCodec ff_s302m_decoder;
extern AVCodec ff_sanm_decoder;
extern AVCodec ff_scpr_decoder;
extern AVCodec ff_screenpresso_decoder;
extern AVCodec ff_sgi_encoder;
extern AVCodec ff_sgi_decoder;
extern AVCodec ff_sgirle_decoder;
extern AVCodec ff_sheervideo_decoder;
extern AVCodec ff_smacker_decoder;
extern AVCodec ff_smc_decoder;
extern AVCodec ff_smvjpeg_decoder;
extern AVCodec ff_snow_encoder;
extern AVCodec ff_snow_decoder;
extern AVCodec ff_sp5x_decoder;
extern AVCodec ff_speedhq_decoder;
extern AVCodec ff_srgc_decoder;
extern AVCodec ff_sunrast_encoder;
extern AVCodec ff_sunrast_decoder;
extern AVCodec ff_svq1_encoder;
extern AVCodec ff_svq1_decoder;
extern AVCodec ff_svq3_decoder;
extern AVCodec ff_targa_encoder;
extern AVCodec ff_targa_decoder;
extern AVCodec ff_targa_y216_decoder;
extern AVCodec ff_tdsc_decoder;
extern AVCodec ff_theora_decoder;
extern AVCodec ff_thp_decoder;
extern AVCodec ff_tiertexseqvideo_decoder;
extern AVCodec ff_tiff_encoder;
extern AVCodec ff_tiff_decoder;
extern AVCodec ff_tmv_decoder;
extern AVCodec ff_truemotion1_decoder;
extern AVCodec ff_truemotion2_decoder;
extern AVCodec ff_truemotion2rt_decoder;
extern AVCodec ff_tscc_decoder;
extern AVCodec ff_tscc2_decoder;
extern AVCodec ff_txd_decoder;
extern AVCodec ff_ulti_decoder;
extern AVCodec ff_utvideo_encoder;
extern AVCodec ff_utvideo_decoder;
extern AVCodec ff_v210_encoder;
extern AVCodec ff_v210_decoder;
extern AVCodec ff_v210x_decoder;
extern AVCodec ff_v308_encoder;
extern AVCodec ff_v308_decoder;
extern AVCodec ff_v408_encoder;
extern AVCodec ff_v408_decoder;
extern AVCodec ff_v410_encoder;
extern AVCodec ff_v410_decoder;
extern AVCodec ff_vb_decoder;
extern AVCodec ff_vble_decoder;
extern AVCodec ff_vc1_decoder;
extern AVCodec ff_vc1_crystalhd_decoder;
extern AVCodec ff_vc1image_decoder;
extern AVCodec ff_vc1_mmal_decoder;
extern AVCodec ff_vc1_qsv_decoder;
extern AVCodec ff_vc1_v4l2m2m_decoder;
extern AVCodec ff_vc2_encoder;
extern AVCodec ff_vcr1_decoder;
extern AVCodec ff_vmdvideo_decoder;
extern AVCodec ff_vmnc_decoder;
extern AVCodec ff_vp3_decoder;
extern AVCodec ff_vp4_decoder;
extern AVCodec ff_vp5_decoder;
extern AVCodec ff_vp6_decoder;
extern AVCodec ff_vp6a_decoder;
extern AVCodec ff_vp6f_decoder;
extern AVCodec ff_vp7_decoder;
extern AVCodec ff_vp8_decoder;
extern AVCodec ff_vp8_rkmpp_decoder;
extern AVCodec ff_vp8_v4l2m2m_decoder;
extern AVCodec ff_vp9_decoder;
extern AVCodec ff_vp9_rkmpp_decoder;
extern AVCodec ff_vp9_v4l2m2m_decoder;
extern AVCodec ff_vqa_decoder;
extern AVCodec ff_webp_decoder;
extern AVCodec ff_wcmv_decoder;
extern AVCodec ff_wrapped_avframe_encoder;
extern AVCodec ff_wrapped_avframe_decoder;
extern AVCodec ff_wmv1_encoder;
extern AVCodec ff_wmv1_decoder;
extern AVCodec ff_wmv2_encoder;
extern AVCodec ff_wmv2_decoder;
extern AVCodec ff_wmv3_decoder;
extern AVCodec ff_wmv3_crystalhd_decoder;
extern AVCodec ff_wmv3image_decoder;
extern AVCodec ff_wnv1_decoder;
extern AVCodec ff_xan_wc3_decoder;
extern AVCodec ff_xan_wc4_decoder;
extern AVCodec ff_xbm_encoder;
extern AVCodec ff_xbm_decoder;
extern AVCodec ff_xface_encoder;
extern AVCodec ff_xface_decoder;
extern AVCodec ff_xl_decoder;
extern AVCodec ff_xpm_decoder;
extern AVCodec ff_xwd_encoder;
extern AVCodec ff_xwd_decoder;
extern AVCodec ff_y41p_encoder;
extern AVCodec ff_y41p_decoder;
extern AVCodec ff_ylc_decoder;
extern AVCodec ff_yop_decoder;
extern AVCodec ff_yuv4_encoder;
extern AVCodec ff_yuv4_decoder;
extern AVCodec ff_zero12v_decoder;
extern AVCodec ff_zerocodec_decoder;
extern AVCodec ff_zlib_encoder;
extern AVCodec ff_zlib_decoder;
extern AVCodec ff_zmbv_encoder;
extern AVCodec ff_zmbv_decoder;

/* audio codecs */
extern AVCodec ff_aac_encoder;
extern AVCodec ff_aac_decoder;
extern AVCodec ff_aac_fixed_decoder;
extern AVCodec ff_aac_latm_decoder;
extern AVCodec ff_ac3_encoder;
extern AVCodec ff_ac3_decoder;
extern AVCodec ff_ac3_fixed_encoder;
extern AVCodec ff_ac3_fixed_decoder;
extern AVCodec ff_acelp_kelvin_decoder;
extern AVCodec ff_alac_encoder;
extern AVCodec ff_alac_decoder;
extern AVCodec ff_als_decoder;
extern AVCodec ff_amrnb_decoder;
extern AVCodec ff_amrwb_decoder;
extern AVCodec ff_ape_decoder;
extern AVCodec ff_aptx_encoder;
extern AVCodec ff_aptx_decoder;
extern AVCodec ff_aptx_hd_encoder;
extern AVCodec ff_aptx_hd_decoder;
extern AVCodec ff_atrac1_decoder;
extern AVCodec ff_atrac3_decoder;
extern AVCodec ff_atrac3al_decoder;
extern AVCodec ff_atrac3p_decoder;
extern AVCodec ff_atrac3pal_decoder;
extern AVCodec ff_atrac9_decoder;
extern AVCodec ff_binkaudio_dct_decoder;
extern AVCodec ff_binkaudio_rdft_decoder;
extern AVCodec ff_bmv_audio_decoder;
extern AVCodec ff_cook_decoder;
extern AVCodec ff_dca_encoder;
extern AVCodec ff_dca_decoder;
extern AVCodec ff_dolby_e_decoder;
extern AVCodec ff_dsd_lsbf_decoder;
extern AVCodec ff_dsd_msbf_decoder;
extern AVCodec ff_dsd_lsbf_planar_decoder;
extern AVCodec ff_dsd_msbf_planar_decoder;
extern AVCodec ff_dsicinaudio_decoder;
extern AVCodec ff_dss_sp_decoder;
extern AVCodec ff_dst_decoder;
extern AVCodec ff_eac3_encoder;
extern AVCodec ff_eac3_decoder;
extern AVCodec ff_evrc_decoder;
extern AVCodec ff_ffwavesynth_decoder;
extern AVCodec ff_flac_encoder;
extern AVCodec ff_flac_decoder;
extern AVCodec ff_g723_1_encoder;
extern AVCodec ff_g723_1_decoder;
extern AVCodec ff_g729_decoder;
extern AVCodec ff_gsm_decoder;
extern AVCodec ff_gsm_ms_decoder;
extern AVCodec ff_hca_decoder;
extern AVCodec ff_hcom_decoder;
extern AVCodec ff_iac_decoder;
extern AVCodec ff_ilbc_decoder;
extern AVCodec ff_imc_decoder;
extern AVCodec ff_interplay_acm_decoder;
extern AVCodec ff_mace3_decoder;
extern AVCodec ff_mace6_decoder;
extern AVCodec ff_metasound_decoder;
extern AVCodec ff_mlp_encoder;
extern AVCodec ff_mlp_decoder;
extern AVCodec ff_mp1_decoder;
extern AVCodec ff_mp1float_decoder;
extern AVCodec ff_mp2_encoder;
extern AVCodec ff_mp2_decoder;
extern AVCodec ff_mp2float_decoder;
extern AVCodec ff_mp2fixed_encoder;
extern AVCodec ff_mp3float_decoder;
extern AVCodec ff_mp3_decoder;
extern AVCodec ff_mp3adufloat_decoder;
extern AVCodec ff_mp3adu_decoder;
extern AVCodec ff_mp3on4float_decoder;
extern AVCodec ff_mp3on4_decoder;
extern AVCodec ff_mpc7_decoder;
extern AVCodec ff_mpc8_decoder;
extern AVCodec ff_nellymoser_encoder;
extern AVCodec ff_nellymoser_decoder;
extern AVCodec ff_on2avc_decoder;
extern AVCodec ff_opus_encoder;
extern AVCodec ff_opus_decoder;
extern AVCodec ff_paf_audio_decoder;
extern AVCodec ff_qcelp_decoder;
extern AVCodec ff_qdm2_decoder;
extern AVCodec ff_qdmc_decoder;
extern AVCodec ff_ra_144_encoder;
extern AVCodec ff_ra_144_decoder;
extern AVCodec ff_ra_288_decoder;
extern AVCodec ff_ralf_decoder;
extern AVCodec ff_sbc_encoder;
extern AVCodec ff_sbc_decoder;
extern AVCodec ff_shorten_decoder;
extern AVCodec ff_sipr_decoder;
extern AVCodec ff_siren_decoder;
extern AVCodec ff_smackaud_decoder;
extern AVCodec ff_sonic_encoder;
extern AVCodec ff_sonic_decoder;
extern AVCodec ff_sonic_ls_encoder;
extern AVCodec ff_tak_decoder;
extern AVCodec ff_truehd_encoder;
extern AVCodec ff_truehd_decoder;
extern AVCodec ff_truespeech_decoder;
extern AVCodec ff_tta_encoder;
extern AVCodec ff_tta_decoder;
extern AVCodec ff_twinvq_decoder;
extern AVCodec ff_vmdaudio_decoder;
extern AVCodec ff_vorbis_encoder;
extern AVCodec ff_vorbis_decoder;
extern AVCodec ff_wavpack_encoder;
extern AVCodec ff_wavpack_decoder;
extern AVCodec ff_wmalossless_decoder;
extern AVCodec ff_wmapro_decoder;
extern AVCodec ff_wmav1_encoder;
extern AVCodec ff_wmav1_decoder;
extern AVCodec ff_wmav2_encoder;
extern AVCodec ff_wmav2_decoder;
extern AVCodec ff_wmavoice_decoder;
extern AVCodec ff_ws_snd1_decoder;
extern AVCodec ff_xma1_decoder;
extern AVCodec ff_xma2_decoder;

/* PCM codecs */
extern AVCodec ff_pcm_alaw_encoder;
extern AVCodec ff_pcm_alaw_decoder;
extern AVCodec ff_pcm_bluray_decoder;
extern AVCodec ff_pcm_dvd_encoder;
extern AVCodec ff_pcm_dvd_decoder;
extern AVCodec ff_pcm_f16le_decoder;
extern AVCodec ff_pcm_f24le_decoder;
extern AVCodec ff_pcm_f32be_encoder;
extern AVCodec ff_pcm_f32be_decoder;
extern AVCodec ff_pcm_f32le_encoder;
extern AVCodec ff_pcm_f32le_decoder;
extern AVCodec ff_pcm_f64be_encoder;
extern AVCodec ff_pcm_f64be_decoder;
extern AVCodec ff_pcm_f64le_encoder;
extern AVCodec ff_pcm_f64le_decoder;
extern AVCodec ff_pcm_lxf_decoder;
extern AVCodec ff_pcm_mulaw_encoder;
extern AVCodec ff_pcm_mulaw_decoder;
extern AVCodec ff_pcm_s8_encoder;
extern AVCodec ff_pcm_s8_decoder;
extern AVCodec ff_pcm_s8_planar_encoder;
extern AVCodec ff_pcm_s8_planar_decoder;
extern AVCodec ff_pcm_s16be_encoder;
extern AVCodec ff_pcm_s16be_decoder;
extern AVCodec ff_pcm_s16be_planar_encoder;
extern AVCodec ff_pcm_s16be_planar_decoder;
extern AVCodec ff_pcm_s16le_encoder;
extern AVCodec ff_pcm_s16le_decoder;
extern AVCodec ff_pcm_s16le_planar_encoder;
extern AVCodec ff_pcm_s16le_planar_decoder;
extern AVCodec ff_pcm_s24be_encoder;
extern AVCodec ff_pcm_s24be_decoder;
extern AVCodec ff_pcm_s24daud_encoder;
extern AVCodec ff_pcm_s24daud_decoder;
extern AVCodec ff_pcm_s24le_encoder;
extern AVCodec ff_pcm_s24le_decoder;
extern AVCodec ff_pcm_s24le_planar_encoder;
extern AVCodec ff_pcm_s24le_planar_decoder;
extern AVCodec ff_pcm_s32be_encoder;
extern AVCodec ff_pcm_s32be_decoder;
extern AVCodec ff_pcm_s32le_encoder;
extern AVCodec ff_pcm_s32le_decoder;
extern AVCodec ff_pcm_s32le_planar_encoder;
extern AVCodec ff_pcm_s32le_planar_decoder;
extern AVCodec ff_pcm_s64be_encoder;
extern AVCodec ff_pcm_s64be_decoder;
extern AVCodec ff_pcm_s64le_encoder;
extern AVCodec ff_pcm_s64le_decoder;
extern AVCodec ff_pcm_u8_encoder;
extern AVCodec ff_pcm_u8_decoder;
extern AVCodec ff_pcm_u16be_encoder;
extern AVCodec ff_pcm_u16be_decoder;
extern AVCodec ff_pcm_u16le_encoder;
extern AVCodec ff_pcm_u16le_decoder;
extern AVCodec ff_pcm_u24be_encoder;
extern AVCodec ff_pcm_u24be_decoder;
extern AVCodec ff_pcm_u24le_encoder;
extern AVCodec ff_pcm_u24le_decoder;
extern AVCodec ff_pcm_u32be_encoder;
extern AVCodec ff_pcm_u32be_decoder;
extern AVCodec ff_pcm_u32le_encoder;
extern AVCodec ff_pcm_u32le_decoder;
extern AVCodec ff_pcm_vidc_encoder;
extern AVCodec ff_pcm_vidc_decoder;

/* DPCM codecs */
extern AVCodec ff_derf_dpcm_decoder;
extern AVCodec ff_gremlin_dpcm_decoder;
extern AVCodec ff_interplay_dpcm_decoder;
extern AVCodec ff_roq_dpcm_encoder;
extern AVCodec ff_roq_dpcm_decoder;
extern AVCodec ff_sdx2_dpcm_decoder;
extern AVCodec ff_sol_dpcm_decoder;
extern AVCodec ff_xan_dpcm_decoder;

/* ADPCM codecs */
extern AVCodec ff_adpcm_4xm_decoder;
extern AVCodec ff_adpcm_adx_encoder;
extern AVCodec ff_adpcm_adx_decoder;
extern AVCodec ff_adpcm_afc_decoder;
extern AVCodec ff_adpcm_agm_decoder;
extern AVCodec ff_adpcm_aica_decoder;
extern AVCodec ff_adpcm_argo_decoder;
extern AVCodec ff_adpcm_ct_decoder;
extern AVCodec ff_adpcm_dtk_decoder;
extern AVCodec ff_adpcm_ea_decoder;
extern AVCodec ff_adpcm_ea_maxis_xa_decoder;
extern AVCodec ff_adpcm_ea_r1_decoder;
extern AVCodec ff_adpcm_ea_r2_decoder;
extern AVCodec ff_adpcm_ea_r3_decoder;
extern AVCodec ff_adpcm_ea_xas_decoder;
extern AVCodec ff_adpcm_g722_encoder;
extern AVCodec ff_adpcm_g722_decoder;
extern AVCodec ff_adpcm_g726_encoder;
extern AVCodec ff_adpcm_g726_decoder;
extern AVCodec ff_adpcm_g726le_encoder;
extern AVCodec ff_adpcm_g726le_decoder;
extern AVCodec ff_adpcm_ima_amv_decoder;
extern AVCodec ff_adpcm_ima_alp_decoder;
extern AVCodec ff_adpcm_ima_apc_decoder;
extern AVCodec ff_adpcm_ima_apm_decoder;
extern AVCodec ff_adpcm_ima_cunning_decoder;
extern AVCodec ff_adpcm_ima_dat4_decoder;
extern AVCodec ff_adpcm_ima_dk3_decoder;
extern AVCodec ff_adpcm_ima_dk4_decoder;
extern AVCodec ff_adpcm_ima_ea_eacs_decoder;
extern AVCodec ff_adpcm_ima_ea_sead_decoder;
extern AVCodec ff_adpcm_ima_iss_decoder;
extern AVCodec ff_adpcm_ima_mtf_decoder;
extern AVCodec ff_adpcm_ima_oki_decoder;
extern AVCodec ff_adpcm_ima_qt_encoder;
extern AVCodec ff_adpcm_ima_qt_decoder;
extern AVCodec ff_adpcm_ima_rad_decoder;
extern AVCodec ff_adpcm_ima_ssi_decoder;
extern AVCodec ff_adpcm_ima_ssi_encoder;
extern AVCodec ff_adpcm_ima_smjpeg_decoder;
extern AVCodec ff_adpcm_ima_wav_encoder;
extern AVCodec ff_adpcm_ima_wav_decoder;
extern AVCodec ff_adpcm_ima_ws_decoder;
extern AVCodec ff_adpcm_ms_encoder;
extern AVCodec ff_adpcm_ms_decoder;
extern AVCodec ff_adpcm_mtaf_decoder;
extern AVCodec ff_adpcm_psx_decoder;
extern AVCodec ff_adpcm_sbpro_2_decoder;
extern AVCodec ff_adpcm_sbpro_3_decoder;
extern AVCodec ff_adpcm_sbpro_4_decoder;
extern AVCodec ff_adpcm_swf_encoder;
extern AVCodec ff_adpcm_swf_decoder;
extern AVCodec ff_adpcm_thp_decoder;
extern AVCodec ff_adpcm_thp_le_decoder;
extern AVCodec ff_adpcm_vima_decoder;
extern AVCodec ff_adpcm_xa_decoder;
extern AVCodec ff_adpcm_yamaha_encoder;
extern AVCodec ff_adpcm_yamaha_decoder;
extern AVCodec ff_adpcm_zork_decoder;

/* subtitles */
extern AVCodec ff_ssa_encoder;
extern AVCodec ff_ssa_decoder;
extern AVCodec ff_ass_encoder;
extern AVCodec ff_ass_decoder;
extern AVCodec ff_ccaption_decoder;
extern AVCodec ff_dvbsub_encoder;
extern AVCodec ff_dvbsub_decoder;
extern AVCodec ff_dvdsub_encoder;
extern AVCodec ff_dvdsub_decoder;
extern AVCodec ff_jacosub_decoder;
extern AVCodec ff_microdvd_decoder;
extern AVCodec ff_movtext_encoder;
extern AVCodec ff_movtext_decoder;
extern AVCodec ff_mpl2_decoder;
extern AVCodec ff_pgssub_decoder;
extern AVCodec ff_pjs_decoder;
extern AVCodec ff_realtext_decoder;
extern AVCodec ff_sami_decoder;
extern AVCodec ff_srt_encoder;
extern AVCodec ff_srt_decoder;
extern AVCodec ff_stl_decoder;
extern AVCodec ff_subrip_encoder;
extern AVCodec ff_subrip_decoder;
extern AVCodec ff_subviewer_decoder;
extern AVCodec ff_subviewer1_decoder;
extern AVCodec ff_text_encoder;
extern AVCodec ff_text_decoder;
extern AVCodec ff_vplayer_decoder;
extern AVCodec ff_webvtt_encoder;
extern AVCodec ff_webvtt_decoder;
extern AVCodec ff_xsub_encoder;
extern AVCodec ff_xsub_decoder;

/* external libraries */
extern AVCodec ff_aac_at_encoder;
extern AVCodec ff_aac_at_decoder;
extern AVCodec ff_aac_mf_encoder;
extern AVCodec ff_ac3_at_decoder;
extern AVCodec ff_ac3_mf_encoder;
extern AVCodec ff_adpcm_ima_qt_at_decoder;
extern AVCodec ff_alac_at_encoder;
extern AVCodec ff_alac_at_decoder;
extern AVCodec ff_amr_nb_at_decoder;
extern AVCodec ff_eac3_at_decoder;
extern AVCodec ff_gsm_ms_at_decoder;
extern AVCodec ff_ilbc_at_encoder;
extern AVCodec ff_ilbc_at_decoder;
extern AVCodec ff_mp1_at_decoder;
extern AVCodec ff_mp2_at_decoder;
extern AVCodec ff_mp3_at_decoder;
extern AVCodec ff_mp3_mf_encoder;
extern AVCodec ff_pcm_alaw_at_encoder;
extern AVCodec ff_pcm_alaw_at_decoder;
extern AVCodec ff_pcm_mulaw_at_encoder;
extern AVCodec ff_pcm_mulaw_at_decoder;
extern AVCodec ff_qdmc_at_decoder;
extern AVCodec ff_qdm2_at_decoder;
extern AVCodec ff_libaom_av1_encoder;
extern AVCodec ff_libaribb24_decoder;
extern AVCodec ff_libcelt_decoder;
extern AVCodec ff_libcodec2_encoder;
extern AVCodec ff_libcodec2_decoder;
extern AVCodec ff_libdav1d_decoder;
extern AVCodec ff_libdavs2_decoder;
extern AVCodec ff_libfdk_aac_encoder;
extern AVCodec ff_libfdk_aac_decoder;
extern AVCodec ff_libgsm_encoder;
extern AVCodec ff_libgsm_decoder;
extern AVCodec ff_libgsm_ms_encoder;
extern AVCodec ff_libgsm_ms_decoder;
extern AVCodec ff_libilbc_encoder;
extern AVCodec ff_libilbc_decoder;
extern AVCodec ff_libmp3lame_encoder;
extern AVCodec ff_libopencore_amrnb_encoder;
extern AVCodec ff_libopencore_amrnb_decoder;
extern AVCodec ff_libopencore_amrwb_decoder;
extern AVCodec ff_libopenjpeg_encoder;
extern AVCodec ff_libopenjpeg_decoder;
extern AVCodec ff_libopus_encoder;
extern AVCodec ff_libopus_decoder;
extern AVCodec ff_librav1e_encoder;
extern AVCodec ff_librsvg_decoder;
extern AVCodec ff_libshine_encoder;
extern AVCodec ff_libspeex_encoder;
extern AVCodec ff_libspeex_decoder;
extern AVCodec ff_libtheora_encoder;
extern AVCodec ff_libtwolame_encoder;
extern AVCodec ff_libvo_amrwbenc_encoder;
extern AVCodec ff_libvorbis_encoder;
extern AVCodec ff_libvorbis_decoder;
extern AVCodec ff_libvpx_vp8_encoder;
extern AVCodec ff_libvpx_vp8_decoder;
extern AVCodec ff_libvpx_vp9_encoder;
extern AVCodec ff_libvpx_vp9_decoder;
extern AVCodec ff_libwavpack_encoder;
/* preferred over libwebp */
extern AVCodec ff_libwebp_anim_encoder;
extern AVCodec ff_libwebp_encoder;
extern AVCodec ff_libx262_encoder;
extern AVCodec ff_libx264_encoder;
extern AVCodec ff_libx264rgb_encoder;
extern AVCodec ff_libx265_encoder;
extern AVCodec ff_libxavs_encoder;
extern AVCodec ff_libxavs2_encoder;
extern AVCodec ff_libxvid_encoder;
extern AVCodec ff_libzvbi_teletext_decoder;

/* text */
extern AVCodec ff_bintext_decoder;
extern AVCodec ff_xbin_decoder;
extern AVCodec ff_idf_decoder;

/* external libraries, that shouldn't be used by default if one of the
 * above is available */
extern AVCodec ff_h263_v4l2m2m_encoder;
extern AVCodec ff_libaom_av1_decoder;
extern AVCodec ff_libopenh264_encoder;
extern AVCodec ff_libopenh264_decoder;
extern AVCodec ff_h264_amf_encoder;
extern AVCodec ff_h264_cuvid_decoder;
extern AVCodec ff_h264_mf_encoder;
extern AVCodec ff_h264_nvenc_encoder;
extern AVCodec ff_h264_omx_encoder;
extern AVCodec ff_h264_qsv_encoder;
extern AVCodec ff_h264_v4l2m2m_encoder;
extern AVCodec ff_h264_vaapi_encoder;
extern AVCodec ff_h264_videotoolbox_encoder;
#if FF_API_NVENC_OLD_NAME
extern AVCodec ff_nvenc_encoder;
extern AVCodec ff_nvenc_h264_encoder;
extern AVCodec ff_nvenc_hevc_encoder;
#endif
extern AVCodec ff_hevc_amf_encoder;
extern AVCodec ff_hevc_cuvid_decoder;
extern AVCodec ff_hevc_mediacodec_decoder;
extern AVCodec ff_hevc_mf_encoder;
extern AVCodec ff_hevc_nvenc_encoder;
extern AVCodec ff_hevc_qsv_encoder;
extern AVCodec ff_hevc_v4l2m2m_encoder;
extern AVCodec ff_hevc_vaapi_encoder;
extern AVCodec ff_hevc_videotoolbox_encoder;
extern AVCodec ff_libkvazaar_encoder;
extern AVCodec ff_mjpeg_cuvid_decoder;
extern AVCodec ff_mjpeg_qsv_encoder;
extern AVCodec ff_mjpeg_qsv_decoder;
extern AVCodec ff_mjpeg_vaapi_encoder;
extern AVCodec ff_mpeg1_cuvid_decoder;
extern AVCodec ff_mpeg2_cuvid_decoder;
extern AVCodec ff_mpeg2_qsv_encoder;
extern AVCodec ff_mpeg2_vaapi_encoder;
extern AVCodec ff_mpeg4_cuvid_decoder;
extern AVCodec ff_mpeg4_mediacodec_decoder;
extern AVCodec ff_mpeg4_omx_encoder;
extern AVCodec ff_mpeg4_v4l2m2m_encoder;
extern AVCodec ff_vc1_cuvid_decoder;
extern AVCodec ff_vp8_cuvid_decoder;
extern AVCodec ff_vp8_mediacodec_decoder;
extern AVCodec ff_vp8_qsv_decoder;
extern AVCodec ff_vp8_v4l2m2m_encoder;
extern AVCodec ff_vp8_vaapi_encoder;
extern AVCodec ff_vp9_cuvid_decoder;
extern AVCodec ff_vp9_mediacodec_decoder;
extern AVCodec ff_vp9_qsv_decoder;
extern AVCodec ff_vp9_vaapi_encoder;
extern AVCodec ff_vp9_qsv_encoder;

// The iterate API is not usable with ossfuzz due to the excessive size of binaries created
#if CONFIG_OSSFUZZ
AVCodec * codec_list[] = {
    NULL,
    NULL,
    NULL
};
#else
#include "libavcodec/codec_list.c"
#endif

static AVOnce av_codec_static_init = AV_ONCE_INIT;
static void av_codec_init_static(void)
{
    for (int i = 0; codec_list[i]; i++) {
        if (codec_list[i]->init_static_data)
            codec_list[i]->init_static_data((AVCodec*)codec_list[i]);
    }
}

const AVCodec *av_codec_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVCodec *c = codec_list[i];

    ff_thread_once(&av_codec_static_init, av_codec_init_static);

    if (c)
        *opaque = (void*)(i + 1);

    return c;
}

#if FF_API_NEXT
FF_DISABLE_DEPRECATION_WARNINGS
static AVOnce av_codec_next_init = AV_ONCE_INIT;

static void av_codec_init_next(void)
{
    AVCodec *prev = NULL, *p;
    void *i = 0;
    while ((p = (AVCodec*)av_codec_iterate(&i))) {
        if (prev)
            prev->next = p;
        prev = p;
    }
}



av_cold void avcodec_register(AVCodec *codec)
{
    ff_thread_once(&av_codec_next_init, av_codec_init_next);
}

AVCodec *av_codec_next(const AVCodec *c)
{
    ff_thread_once(&av_codec_next_init, av_codec_init_next);

    if (c)
        return c->next;
    else
        return (AVCodec*)codec_list[0];
}

void avcodec_register_all(void)
{
    ff_thread_once(&av_codec_next_init, av_codec_init_next);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

static enum AVCodecID remap_deprecated_codec_id(enum AVCodecID id)
{
    switch(id){
        //This is for future deprecatec codec ids, its empty since
        //last major bump but will fill up again over time, please don't remove it
        default                                         : return id;
    }
}

static AVCodec *find_codec(enum AVCodecID id, int (*x)(const AVCodec *))
{
    const AVCodec *p, *experimental = NULL;
    void *i = 0;

    id = remap_deprecated_codec_id(id);

    while ((p = av_codec_iterate(&i))) {
        if (!x(p))
            continue;
        if (p->id == id) {
            if (p->capabilities & AV_CODEC_CAP_EXPERIMENTAL && !experimental) {
                experimental = p;
            } else
                return (AVCodec*)p;
        }
    }

    return (AVCodec*)experimental;
}

AVCodec *avcodec_find_encoder(enum AVCodecID id)
{
    return find_codec(id, av_codec_is_encoder);
}

AVCodec *avcodec_find_decoder(enum AVCodecID id)
{
    return find_codec(id, av_codec_is_decoder);
}

static AVCodec *find_codec_by_name(const char *name, int (*x)(const AVCodec *))
{
    void *i = 0;
    const AVCodec *p;

    if (!name)
        return NULL;

    while ((p = av_codec_iterate(&i))) {
        if (!x(p))
            continue;
        if (strcmp(name, p->name) == 0)
            return (AVCodec*)p;
    }

    return NULL;
}

AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    return find_codec_by_name(name, av_codec_is_encoder);
}

AVCodec *avcodec_find_decoder_by_name(const char *name)
{
    return find_codec_by_name(name, av_codec_is_decoder);
}
