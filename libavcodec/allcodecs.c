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

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "config_components.h"
#include "libavutil/thread.h"
#include "codec.h"
#include "codec_id.h"
#include "codec_internal.h"

extern const FFCodec ff_a64multi_encoder;
extern const FFCodec ff_a64multi5_encoder;
extern const FFCodec ff_aasc_decoder;
extern const FFCodec ff_aic_decoder;
extern const FFCodec ff_alias_pix_encoder;
extern const FFCodec ff_alias_pix_decoder;
extern const FFCodec ff_agm_decoder;
extern const FFCodec ff_amv_encoder;
extern const FFCodec ff_amv_decoder;
extern const FFCodec ff_anm_decoder;
extern const FFCodec ff_ansi_decoder;
extern const FFCodec ff_apng_encoder;
extern const FFCodec ff_apng_decoder;
extern const FFCodec ff_arbc_decoder;
extern const FFCodec ff_argo_decoder;
extern const FFCodec ff_asv1_encoder;
extern const FFCodec ff_asv1_decoder;
extern const FFCodec ff_asv2_encoder;
extern const FFCodec ff_asv2_decoder;
extern const FFCodec ff_aura_decoder;
extern const FFCodec ff_aura2_decoder;
extern const FFCodec ff_avrp_encoder;
extern const FFCodec ff_avrp_decoder;
extern const FFCodec ff_avrn_decoder;
extern const FFCodec ff_avs_decoder;
extern const FFCodec ff_avui_encoder;
extern const FFCodec ff_avui_decoder;
extern const FFCodec ff_bethsoftvid_decoder;
extern const FFCodec ff_bfi_decoder;
extern const FFCodec ff_bink_decoder;
extern const FFCodec ff_bitpacked_decoder;
extern const FFCodec ff_bitpacked_encoder;
extern const FFCodec ff_bmp_encoder;
extern const FFCodec ff_bmp_decoder;
extern const FFCodec ff_bmv_video_decoder;
extern const FFCodec ff_brender_pix_decoder;
extern const FFCodec ff_c93_decoder;
extern const FFCodec ff_cavs_decoder;
extern const FFCodec ff_cdgraphics_decoder;
extern const FFCodec ff_cdtoons_decoder;
extern const FFCodec ff_cdxl_decoder;
extern const FFCodec ff_cfhd_encoder;
extern const FFCodec ff_cfhd_decoder;
extern const FFCodec ff_cinepak_encoder;
extern const FFCodec ff_cinepak_decoder;
extern const FFCodec ff_clearvideo_decoder;
extern const FFCodec ff_cljr_encoder;
extern const FFCodec ff_cljr_decoder;
extern const FFCodec ff_cllc_decoder;
extern const FFCodec ff_comfortnoise_encoder;
extern const FFCodec ff_comfortnoise_decoder;
extern const FFCodec ff_cpia_decoder;
extern const FFCodec ff_cri_decoder;
extern const FFCodec ff_cscd_decoder;
extern const FFCodec ff_cyuv_decoder;
extern const FFCodec ff_dds_decoder;
extern const FFCodec ff_dfa_decoder;
extern const FFCodec ff_dirac_decoder;
extern const FFCodec ff_dnxhd_encoder;
extern const FFCodec ff_dnxhd_decoder;
extern const FFCodec ff_dpx_encoder;
extern const FFCodec ff_dpx_decoder;
extern const FFCodec ff_dsicinvideo_decoder;
extern const FFCodec ff_dvaudio_decoder;
extern const FFCodec ff_dvvideo_encoder;
extern const FFCodec ff_dvvideo_decoder;
extern const FFCodec ff_dxa_decoder;
extern const FFCodec ff_dxtory_decoder;
extern const FFCodec ff_dxv_encoder;
extern const FFCodec ff_dxv_decoder;
extern const FFCodec ff_eacmv_decoder;
extern const FFCodec ff_eamad_decoder;
extern const FFCodec ff_eatgq_decoder;
extern const FFCodec ff_eatgv_decoder;
extern const FFCodec ff_eatqi_decoder;
extern const FFCodec ff_eightbps_decoder;
extern const FFCodec ff_eightsvx_exp_decoder;
extern const FFCodec ff_eightsvx_fib_decoder;
extern const FFCodec ff_escape124_decoder;
extern const FFCodec ff_escape130_decoder;
extern const FFCodec ff_exr_encoder;
extern const FFCodec ff_exr_decoder;
extern const FFCodec ff_ffv1_encoder;
extern const FFCodec ff_ffv1_decoder;
extern const FFCodec ff_ffvhuff_encoder;
extern const FFCodec ff_ffvhuff_decoder;
extern const FFCodec ff_fic_decoder;
extern const FFCodec ff_fits_encoder;
extern const FFCodec ff_fits_decoder;
extern const FFCodec ff_flashsv_encoder;
extern const FFCodec ff_flashsv_decoder;
extern const FFCodec ff_flashsv2_encoder;
extern const FFCodec ff_flashsv2_decoder;
extern const FFCodec ff_flic_decoder;
extern const FFCodec ff_flv_encoder;
extern const FFCodec ff_flv_decoder;
extern const FFCodec ff_fmvc_decoder;
extern const FFCodec ff_fourxm_decoder;
extern const FFCodec ff_fraps_decoder;
extern const FFCodec ff_frwu_decoder;
extern const FFCodec ff_g2m_decoder;
extern const FFCodec ff_gdv_decoder;
extern const FFCodec ff_gem_decoder;
extern const FFCodec ff_gif_encoder;
extern const FFCodec ff_gif_decoder;
extern const FFCodec ff_h261_encoder;
extern const FFCodec ff_h261_decoder;
extern const FFCodec ff_h263_encoder;
extern const FFCodec ff_h263_decoder;
extern const FFCodec ff_h263i_decoder;
extern const FFCodec ff_h263p_encoder;
extern const FFCodec ff_h263p_decoder;
extern const FFCodec ff_h263_v4l2m2m_decoder;
extern const FFCodec ff_h264_decoder;
extern const FFCodec ff_h264_v4l2m2m_decoder;
extern const FFCodec ff_h264_mediacodec_decoder;
extern const FFCodec ff_h264_mediacodec_encoder;
extern const FFCodec ff_h264_mmal_decoder;
extern const FFCodec ff_h264_qsv_decoder;
extern const FFCodec ff_h264_rkmpp_decoder;
extern const FFCodec ff_hap_encoder;
extern const FFCodec ff_hap_decoder;
extern const FFCodec ff_hevc_decoder;
extern const FFCodec ff_hevc_qsv_decoder;
extern const FFCodec ff_hevc_rkmpp_decoder;
extern const FFCodec ff_hevc_v4l2m2m_decoder;
extern const FFCodec ff_hnm4_video_decoder;
extern const FFCodec ff_hq_hqa_decoder;
extern const FFCodec ff_hqx_decoder;
extern const FFCodec ff_huffyuv_encoder;
extern const FFCodec ff_huffyuv_decoder;
extern const FFCodec ff_hymt_decoder;
extern const FFCodec ff_idcin_decoder;
extern const FFCodec ff_iff_ilbm_decoder;
extern const FFCodec ff_imm4_decoder;
extern const FFCodec ff_imm5_decoder;
extern const FFCodec ff_indeo2_decoder;
extern const FFCodec ff_indeo3_decoder;
extern const FFCodec ff_indeo4_decoder;
extern const FFCodec ff_indeo5_decoder;
extern const FFCodec ff_interplay_video_decoder;
extern const FFCodec ff_ipu_decoder;
extern const FFCodec ff_jpeg2000_encoder;
extern const FFCodec ff_jpeg2000_decoder;
extern const FFCodec ff_jpegls_encoder;
extern const FFCodec ff_jpegls_decoder;
extern const FFCodec ff_jv_decoder;
extern const FFCodec ff_kgv1_decoder;
extern const FFCodec ff_kmvc_decoder;
extern const FFCodec ff_lagarith_decoder;
extern const FFCodec ff_lead_decoder;
extern const FFCodec ff_ljpeg_encoder;
extern const FFCodec ff_loco_decoder;
extern const FFCodec ff_lscr_decoder;
extern const FFCodec ff_m101_decoder;
extern const FFCodec ff_magicyuv_encoder;
extern const FFCodec ff_magicyuv_decoder;
extern const FFCodec ff_mdec_decoder;
extern const FFCodec ff_media100_decoder;
extern const FFCodec ff_mimic_decoder;
extern const FFCodec ff_mjpeg_encoder;
extern const FFCodec ff_mjpeg_decoder;
extern const FFCodec ff_mjpegb_decoder;
extern const FFCodec ff_mmvideo_decoder;
extern const FFCodec ff_mobiclip_decoder;
extern const FFCodec ff_motionpixels_decoder;
extern const FFCodec ff_mpeg1video_encoder;
extern const FFCodec ff_mpeg1video_decoder;
extern const FFCodec ff_mpeg2video_encoder;
extern const FFCodec ff_mpeg2video_decoder;
extern const FFCodec ff_mpeg4_encoder;
extern const FFCodec ff_mpeg4_decoder;
extern const FFCodec ff_mpeg4_v4l2m2m_decoder;
extern const FFCodec ff_mpeg4_mmal_decoder;
extern const FFCodec ff_mpegvideo_decoder;
extern const FFCodec ff_mpeg1_v4l2m2m_decoder;
extern const FFCodec ff_mpeg2_mmal_decoder;
extern const FFCodec ff_mpeg2_v4l2m2m_decoder;
extern const FFCodec ff_mpeg2_qsv_decoder;
extern const FFCodec ff_mpeg2_mediacodec_decoder;
extern const FFCodec ff_msa1_decoder;
extern const FFCodec ff_mscc_decoder;
extern const FFCodec ff_msmpeg4v1_decoder;
extern const FFCodec ff_msmpeg4v2_encoder;
extern const FFCodec ff_msmpeg4v2_decoder;
extern const FFCodec ff_msmpeg4v3_encoder;
extern const FFCodec ff_msmpeg4v3_decoder;
extern const FFCodec ff_msp2_decoder;
extern const FFCodec ff_msrle_encoder;
extern const FFCodec ff_msrle_decoder;
extern const FFCodec ff_mss1_decoder;
extern const FFCodec ff_mss2_decoder;
extern const FFCodec ff_msvideo1_encoder;
extern const FFCodec ff_msvideo1_decoder;
extern const FFCodec ff_mszh_decoder;
extern const FFCodec ff_mts2_decoder;
extern const FFCodec ff_mv30_decoder;
extern const FFCodec ff_mvc1_decoder;
extern const FFCodec ff_mvc2_decoder;
extern const FFCodec ff_mvdv_decoder;
extern const FFCodec ff_mvha_decoder;
extern const FFCodec ff_mwsc_decoder;
extern const FFCodec ff_mxpeg_decoder;
extern const FFCodec ff_notchlc_decoder;
extern const FFCodec ff_nuv_decoder;
extern const FFCodec ff_paf_video_decoder;
extern const FFCodec ff_pam_encoder;
extern const FFCodec ff_pam_decoder;
extern const FFCodec ff_pbm_encoder;
extern const FFCodec ff_pbm_decoder;
extern const FFCodec ff_pcx_encoder;
extern const FFCodec ff_pcx_decoder;
extern const FFCodec ff_pdv_decoder;
extern const FFCodec ff_pfm_encoder;
extern const FFCodec ff_pfm_decoder;
extern const FFCodec ff_pgm_encoder;
extern const FFCodec ff_pgm_decoder;
extern const FFCodec ff_pgmyuv_encoder;
extern const FFCodec ff_pgmyuv_decoder;
extern const FFCodec ff_pgx_decoder;
extern const FFCodec ff_phm_encoder;
extern const FFCodec ff_phm_decoder;
extern const FFCodec ff_photocd_decoder;
extern const FFCodec ff_pictor_decoder;
extern const FFCodec ff_pixlet_decoder;
extern const FFCodec ff_png_encoder;
extern const FFCodec ff_png_decoder;
extern const FFCodec ff_ppm_encoder;
extern const FFCodec ff_ppm_decoder;
extern const FFCodec ff_prores_encoder;
extern const FFCodec ff_prores_decoder;
extern const FFCodec ff_prores_aw_encoder;
extern const FFCodec ff_prores_ks_encoder;
extern const FFCodec ff_prosumer_decoder;
extern const FFCodec ff_psd_decoder;
extern const FFCodec ff_ptx_decoder;
extern const FFCodec ff_qdraw_decoder;
extern const FFCodec ff_qoi_encoder;
extern const FFCodec ff_qoi_decoder;
extern const FFCodec ff_qpeg_decoder;
extern const FFCodec ff_qtrle_encoder;
extern const FFCodec ff_qtrle_decoder;
extern const FFCodec ff_r10k_encoder;
extern const FFCodec ff_r10k_decoder;
extern const FFCodec ff_r210_encoder;
extern const FFCodec ff_r210_decoder;
extern const FFCodec ff_rasc_decoder;
extern const FFCodec ff_rawvideo_encoder;
extern const FFCodec ff_rawvideo_decoder;
extern const FFCodec ff_rka_decoder;
extern const FFCodec ff_rl2_decoder;
extern const FFCodec ff_roq_encoder;
extern const FFCodec ff_roq_decoder;
extern const FFCodec ff_rpza_encoder;
extern const FFCodec ff_rpza_decoder;
extern const FFCodec ff_rscc_decoder;
extern const FFCodec ff_rtv1_decoder;
extern const FFCodec ff_rv10_encoder;
extern const FFCodec ff_rv10_decoder;
extern const FFCodec ff_rv20_encoder;
extern const FFCodec ff_rv20_decoder;
extern const FFCodec ff_rv30_decoder;
extern const FFCodec ff_rv40_decoder;
extern const FFCodec ff_s302m_encoder;
extern const FFCodec ff_s302m_decoder;
extern const FFCodec ff_sanm_decoder;
extern const FFCodec ff_scpr_decoder;
extern const FFCodec ff_screenpresso_decoder;
extern const FFCodec ff_sga_decoder;
extern const FFCodec ff_sgi_encoder;
extern const FFCodec ff_sgi_decoder;
extern const FFCodec ff_sgirle_decoder;
extern const FFCodec ff_sheervideo_decoder;
extern const FFCodec ff_simbiosis_imx_decoder;
extern const FFCodec ff_smacker_decoder;
extern const FFCodec ff_smc_encoder;
extern const FFCodec ff_smc_decoder;
extern const FFCodec ff_smvjpeg_decoder;
extern const FFCodec ff_snow_encoder;
extern const FFCodec ff_snow_decoder;
extern const FFCodec ff_sp5x_decoder;
extern const FFCodec ff_speedhq_decoder;
extern const FFCodec ff_speedhq_encoder;
extern const FFCodec ff_speex_decoder;
extern const FFCodec ff_srgc_decoder;
extern const FFCodec ff_sunrast_encoder;
extern const FFCodec ff_sunrast_decoder;
extern const FFCodec ff_svq1_encoder;
extern const FFCodec ff_svq1_decoder;
extern const FFCodec ff_svq3_decoder;
extern const FFCodec ff_targa_encoder;
extern const FFCodec ff_targa_decoder;
extern const FFCodec ff_targa_y216_decoder;
extern const FFCodec ff_tdsc_decoder;
extern const FFCodec ff_theora_decoder;
extern const FFCodec ff_thp_decoder;
extern const FFCodec ff_tiertexseqvideo_decoder;
extern const FFCodec ff_tiff_encoder;
extern const FFCodec ff_tiff_decoder;
extern const FFCodec ff_tmv_decoder;
extern const FFCodec ff_truemotion1_decoder;
extern const FFCodec ff_truemotion2_decoder;
extern const FFCodec ff_truemotion2rt_decoder;
extern const FFCodec ff_tscc_decoder;
extern const FFCodec ff_tscc2_decoder;
extern const FFCodec ff_txd_decoder;
extern const FFCodec ff_ulti_decoder;
extern const FFCodec ff_utvideo_encoder;
extern const FFCodec ff_utvideo_decoder;
extern const FFCodec ff_v210_encoder;
extern const FFCodec ff_v210_decoder;
extern const FFCodec ff_v210x_decoder;
extern const FFCodec ff_v308_encoder;
extern const FFCodec ff_v308_decoder;
extern const FFCodec ff_v408_encoder;
extern const FFCodec ff_v408_decoder;
extern const FFCodec ff_v410_encoder;
extern const FFCodec ff_v410_decoder;
extern const FFCodec ff_vb_decoder;
extern const FFCodec ff_vbn_encoder;
extern const FFCodec ff_vbn_decoder;
extern const FFCodec ff_vble_decoder;
extern const FFCodec ff_vc1_decoder;
extern const FFCodec ff_vc1image_decoder;
extern const FFCodec ff_vc1_mmal_decoder;
extern const FFCodec ff_vc1_qsv_decoder;
extern const FFCodec ff_vc1_v4l2m2m_decoder;
extern const FFCodec ff_vc2_encoder;
extern const FFCodec ff_vcr1_decoder;
extern const FFCodec ff_vmdvideo_decoder;
extern const FFCodec ff_vmix_decoder;
extern const FFCodec ff_vmnc_decoder;
extern const FFCodec ff_vp3_decoder;
extern const FFCodec ff_vp4_decoder;
extern const FFCodec ff_vp5_decoder;
extern const FFCodec ff_vp6_decoder;
extern const FFCodec ff_vp6a_decoder;
extern const FFCodec ff_vp6f_decoder;
extern const FFCodec ff_vp7_decoder;
extern const FFCodec ff_vp8_decoder;
extern const FFCodec ff_vp8_rkmpp_decoder;
extern const FFCodec ff_vp8_v4l2m2m_decoder;
extern const FFCodec ff_vp9_decoder;
extern const FFCodec ff_vp9_rkmpp_decoder;
extern const FFCodec ff_vp9_v4l2m2m_decoder;
extern const FFCodec ff_vqa_decoder;
extern const FFCodec ff_vqc_decoder;
extern const FFCodec ff_vvc_decoder;
extern const FFCodec ff_wbmp_decoder;
extern const FFCodec ff_wbmp_encoder;
extern const FFCodec ff_webp_decoder;
extern const FFCodec ff_wcmv_decoder;
extern const FFCodec ff_wrapped_avframe_encoder;
extern const FFCodec ff_wrapped_avframe_decoder;
extern const FFCodec ff_wmv1_encoder;
extern const FFCodec ff_wmv1_decoder;
extern const FFCodec ff_wmv2_encoder;
extern const FFCodec ff_wmv2_decoder;
extern const FFCodec ff_wmv3_decoder;
extern const FFCodec ff_wmv3image_decoder;
extern const FFCodec ff_wnv1_decoder;
extern const FFCodec ff_xan_wc3_decoder;
extern const FFCodec ff_xan_wc4_decoder;
extern const FFCodec ff_xbm_encoder;
extern const FFCodec ff_xbm_decoder;
extern const FFCodec ff_xface_encoder;
extern const FFCodec ff_xface_decoder;
extern const FFCodec ff_xl_decoder;
extern const FFCodec ff_xpm_decoder;
extern const FFCodec ff_xwd_encoder;
extern const FFCodec ff_xwd_decoder;
extern const FFCodec ff_y41p_encoder;
extern const FFCodec ff_y41p_decoder;
extern const FFCodec ff_ylc_decoder;
extern const FFCodec ff_yop_decoder;
extern const FFCodec ff_yuv4_encoder;
extern const FFCodec ff_yuv4_decoder;
extern const FFCodec ff_zero12v_decoder;
extern const FFCodec ff_zerocodec_decoder;
extern const FFCodec ff_zlib_encoder;
extern const FFCodec ff_zlib_decoder;
extern const FFCodec ff_zmbv_encoder;
extern const FFCodec ff_zmbv_decoder;

/* audio codecs */
extern const FFCodec ff_aac_encoder;
extern const FFCodec ff_aac_decoder;
extern const FFCodec ff_aac_fixed_decoder;
extern const FFCodec ff_aac_latm_decoder;
extern const FFCodec ff_ac3_encoder;
extern const FFCodec ff_ac3_decoder;
extern const FFCodec ff_ac3_fixed_encoder;
extern const FFCodec ff_ac3_fixed_decoder;
extern const FFCodec ff_acelp_kelvin_decoder;
extern const FFCodec ff_alac_encoder;
extern const FFCodec ff_alac_decoder;
extern const FFCodec ff_als_decoder;
extern const FFCodec ff_amrnb_decoder;
extern const FFCodec ff_amrwb_decoder;
extern const FFCodec ff_apac_decoder;
extern const FFCodec ff_ape_decoder;
extern const FFCodec ff_aptx_encoder;
extern const FFCodec ff_aptx_decoder;
extern const FFCodec ff_aptx_hd_encoder;
extern const FFCodec ff_aptx_hd_decoder;
extern const FFCodec ff_atrac1_decoder;
extern const FFCodec ff_atrac3_decoder;
extern const FFCodec ff_atrac3al_decoder;
extern const FFCodec ff_atrac3p_decoder;
extern const FFCodec ff_atrac3pal_decoder;
extern const FFCodec ff_atrac9_decoder;
extern const FFCodec ff_binkaudio_dct_decoder;
extern const FFCodec ff_binkaudio_rdft_decoder;
extern const FFCodec ff_bmv_audio_decoder;
extern const FFCodec ff_bonk_decoder;
extern const FFCodec ff_cook_decoder;
extern const FFCodec ff_dca_encoder;
extern const FFCodec ff_dca_decoder;
extern const FFCodec ff_dfpwm_encoder;
extern const FFCodec ff_dfpwm_decoder;
extern const FFCodec ff_dolby_e_decoder;
extern const FFCodec ff_dsd_lsbf_decoder;
extern const FFCodec ff_dsd_msbf_decoder;
extern const FFCodec ff_dsd_lsbf_planar_decoder;
extern const FFCodec ff_dsd_msbf_planar_decoder;
extern const FFCodec ff_dsicinaudio_decoder;
extern const FFCodec ff_dss_sp_decoder;
extern const FFCodec ff_dst_decoder;
extern const FFCodec ff_eac3_encoder;
extern const FFCodec ff_eac3_decoder;
extern const FFCodec ff_evrc_decoder;
extern const FFCodec ff_fastaudio_decoder;
extern const FFCodec ff_ffwavesynth_decoder;
extern const FFCodec ff_flac_encoder;
extern const FFCodec ff_flac_decoder;
extern const FFCodec ff_ftr_decoder;
extern const FFCodec ff_g723_1_encoder;
extern const FFCodec ff_g723_1_decoder;
extern const FFCodec ff_g729_decoder;
extern const FFCodec ff_gsm_decoder;
extern const FFCodec ff_gsm_ms_decoder;
extern const FFCodec ff_hca_decoder;
extern const FFCodec ff_hcom_decoder;
extern const FFCodec ff_hdr_encoder;
extern const FFCodec ff_hdr_decoder;
extern const FFCodec ff_iac_decoder;
extern const FFCodec ff_ilbc_decoder;
extern const FFCodec ff_imc_decoder;
extern const FFCodec ff_interplay_acm_decoder;
extern const FFCodec ff_mace3_decoder;
extern const FFCodec ff_mace6_decoder;
extern const FFCodec ff_metasound_decoder;
extern const FFCodec ff_misc4_decoder;
extern const FFCodec ff_mlp_encoder;
extern const FFCodec ff_mlp_decoder;
extern const FFCodec ff_mp1_decoder;
extern const FFCodec ff_mp1float_decoder;
extern const FFCodec ff_mp2_encoder;
extern const FFCodec ff_mp2_decoder;
extern const FFCodec ff_mp2float_decoder;
extern const FFCodec ff_mp2fixed_encoder;
extern const FFCodec ff_mp3float_decoder;
extern const FFCodec ff_mp3_decoder;
extern const FFCodec ff_mp3adufloat_decoder;
extern const FFCodec ff_mp3adu_decoder;
extern const FFCodec ff_mp3on4float_decoder;
extern const FFCodec ff_mp3on4_decoder;
extern const FFCodec ff_mpc7_decoder;
extern const FFCodec ff_mpc8_decoder;
extern const FFCodec ff_msnsiren_decoder;
extern const FFCodec ff_nellymoser_encoder;
extern const FFCodec ff_nellymoser_decoder;
extern const FFCodec ff_on2avc_decoder;
extern const FFCodec ff_opus_encoder;
extern const FFCodec ff_opus_decoder;
extern const FFCodec ff_osq_decoder;
extern const FFCodec ff_paf_audio_decoder;
extern const FFCodec ff_qcelp_decoder;
extern const FFCodec ff_qdm2_decoder;
extern const FFCodec ff_qdmc_decoder;
extern const FFCodec ff_qoa_decoder;
extern const FFCodec ff_ra_144_encoder;
extern const FFCodec ff_ra_144_decoder;
extern const FFCodec ff_ra_288_decoder;
extern const FFCodec ff_ralf_decoder;
extern const FFCodec ff_sbc_encoder;
extern const FFCodec ff_sbc_decoder;
extern const FFCodec ff_shorten_decoder;
extern const FFCodec ff_sipr_decoder;
extern const FFCodec ff_siren_decoder;
extern const FFCodec ff_smackaud_decoder;
extern const FFCodec ff_sonic_encoder;
extern const FFCodec ff_sonic_decoder;
extern const FFCodec ff_sonic_ls_encoder;
extern const FFCodec ff_tak_decoder;
extern const FFCodec ff_truehd_encoder;
extern const FFCodec ff_truehd_decoder;
extern const FFCodec ff_truespeech_decoder;
extern const FFCodec ff_tta_encoder;
extern const FFCodec ff_tta_decoder;
extern const FFCodec ff_twinvq_decoder;
extern const FFCodec ff_vmdaudio_decoder;
extern const FFCodec ff_vorbis_encoder;
extern const FFCodec ff_vorbis_decoder;
extern const FFCodec ff_wavarc_decoder;
extern const FFCodec ff_wavpack_encoder;
extern const FFCodec ff_wavpack_decoder;
extern const FFCodec ff_wmalossless_decoder;
extern const FFCodec ff_wmapro_decoder;
extern const FFCodec ff_wmav1_encoder;
extern const FFCodec ff_wmav1_decoder;
extern const FFCodec ff_wmav2_encoder;
extern const FFCodec ff_wmav2_decoder;
extern const FFCodec ff_wmavoice_decoder;
extern const FFCodec ff_ws_snd1_decoder;
extern const FFCodec ff_xma1_decoder;
extern const FFCodec ff_xma2_decoder;

/* PCM codecs */
extern const FFCodec ff_pcm_alaw_encoder;
extern const FFCodec ff_pcm_alaw_decoder;
extern const FFCodec ff_pcm_bluray_encoder;
extern const FFCodec ff_pcm_bluray_decoder;
extern const FFCodec ff_pcm_dvd_encoder;
extern const FFCodec ff_pcm_dvd_decoder;
extern const FFCodec ff_pcm_f16le_decoder;
extern const FFCodec ff_pcm_f24le_decoder;
extern const FFCodec ff_pcm_f32be_encoder;
extern const FFCodec ff_pcm_f32be_decoder;
extern const FFCodec ff_pcm_f32le_encoder;
extern const FFCodec ff_pcm_f32le_decoder;
extern const FFCodec ff_pcm_f64be_encoder;
extern const FFCodec ff_pcm_f64be_decoder;
extern const FFCodec ff_pcm_f64le_encoder;
extern const FFCodec ff_pcm_f64le_decoder;
extern const FFCodec ff_pcm_lxf_decoder;
extern const FFCodec ff_pcm_mulaw_encoder;
extern const FFCodec ff_pcm_mulaw_decoder;
extern const FFCodec ff_pcm_s8_encoder;
extern const FFCodec ff_pcm_s8_decoder;
extern const FFCodec ff_pcm_s8_planar_encoder;
extern const FFCodec ff_pcm_s8_planar_decoder;
extern const FFCodec ff_pcm_s16be_encoder;
extern const FFCodec ff_pcm_s16be_decoder;
extern const FFCodec ff_pcm_s16be_planar_encoder;
extern const FFCodec ff_pcm_s16be_planar_decoder;
extern const FFCodec ff_pcm_s16le_encoder;
extern const FFCodec ff_pcm_s16le_decoder;
extern const FFCodec ff_pcm_s16le_planar_encoder;
extern const FFCodec ff_pcm_s16le_planar_decoder;
extern const FFCodec ff_pcm_s24be_encoder;
extern const FFCodec ff_pcm_s24be_decoder;
extern const FFCodec ff_pcm_s24daud_encoder;
extern const FFCodec ff_pcm_s24daud_decoder;
extern const FFCodec ff_pcm_s24le_encoder;
extern const FFCodec ff_pcm_s24le_decoder;
extern const FFCodec ff_pcm_s24le_planar_encoder;
extern const FFCodec ff_pcm_s24le_planar_decoder;
extern const FFCodec ff_pcm_s32be_encoder;
extern const FFCodec ff_pcm_s32be_decoder;
extern const FFCodec ff_pcm_s32le_encoder;
extern const FFCodec ff_pcm_s32le_decoder;
extern const FFCodec ff_pcm_s32le_planar_encoder;
extern const FFCodec ff_pcm_s32le_planar_decoder;
extern const FFCodec ff_pcm_s64be_encoder;
extern const FFCodec ff_pcm_s64be_decoder;
extern const FFCodec ff_pcm_s64le_encoder;
extern const FFCodec ff_pcm_s64le_decoder;
extern const FFCodec ff_pcm_sga_decoder;
extern const FFCodec ff_pcm_u8_encoder;
extern const FFCodec ff_pcm_u8_decoder;
extern const FFCodec ff_pcm_u16be_encoder;
extern const FFCodec ff_pcm_u16be_decoder;
extern const FFCodec ff_pcm_u16le_encoder;
extern const FFCodec ff_pcm_u16le_decoder;
extern const FFCodec ff_pcm_u24be_encoder;
extern const FFCodec ff_pcm_u24be_decoder;
extern const FFCodec ff_pcm_u24le_encoder;
extern const FFCodec ff_pcm_u24le_decoder;
extern const FFCodec ff_pcm_u32be_encoder;
extern const FFCodec ff_pcm_u32be_decoder;
extern const FFCodec ff_pcm_u32le_encoder;
extern const FFCodec ff_pcm_u32le_decoder;
extern const FFCodec ff_pcm_vidc_encoder;
extern const FFCodec ff_pcm_vidc_decoder;

/* DPCM codecs */
extern const FFCodec ff_cbd2_dpcm_decoder;
extern const FFCodec ff_derf_dpcm_decoder;
extern const FFCodec ff_gremlin_dpcm_decoder;
extern const FFCodec ff_interplay_dpcm_decoder;
extern const FFCodec ff_roq_dpcm_encoder;
extern const FFCodec ff_roq_dpcm_decoder;
extern const FFCodec ff_sdx2_dpcm_decoder;
extern const FFCodec ff_sol_dpcm_decoder;
extern const FFCodec ff_xan_dpcm_decoder;
extern const FFCodec ff_wady_dpcm_decoder;

/* ADPCM codecs */
extern const FFCodec ff_adpcm_4xm_decoder;
extern const FFCodec ff_adpcm_adx_encoder;
extern const FFCodec ff_adpcm_adx_decoder;
extern const FFCodec ff_adpcm_afc_decoder;
extern const FFCodec ff_adpcm_agm_decoder;
extern const FFCodec ff_adpcm_aica_decoder;
extern const FFCodec ff_adpcm_argo_decoder;
extern const FFCodec ff_adpcm_argo_encoder;
extern const FFCodec ff_adpcm_ct_decoder;
extern const FFCodec ff_adpcm_dtk_decoder;
extern const FFCodec ff_adpcm_ea_decoder;
extern const FFCodec ff_adpcm_ea_maxis_xa_decoder;
extern const FFCodec ff_adpcm_ea_r1_decoder;
extern const FFCodec ff_adpcm_ea_r2_decoder;
extern const FFCodec ff_adpcm_ea_r3_decoder;
extern const FFCodec ff_adpcm_ea_xas_decoder;
extern const FFCodec ff_adpcm_g722_encoder;
extern const FFCodec ff_adpcm_g722_decoder;
extern const FFCodec ff_adpcm_g726_encoder;
extern const FFCodec ff_adpcm_g726_decoder;
extern const FFCodec ff_adpcm_g726le_encoder;
extern const FFCodec ff_adpcm_g726le_decoder;
extern const FFCodec ff_adpcm_ima_acorn_decoder;
extern const FFCodec ff_adpcm_ima_amv_decoder;
extern const FFCodec ff_adpcm_ima_amv_encoder;
extern const FFCodec ff_adpcm_ima_alp_decoder;
extern const FFCodec ff_adpcm_ima_alp_encoder;
extern const FFCodec ff_adpcm_ima_apc_decoder;
extern const FFCodec ff_adpcm_ima_apm_decoder;
extern const FFCodec ff_adpcm_ima_apm_encoder;
extern const FFCodec ff_adpcm_ima_cunning_decoder;
extern const FFCodec ff_adpcm_ima_dat4_decoder;
extern const FFCodec ff_adpcm_ima_dk3_decoder;
extern const FFCodec ff_adpcm_ima_dk4_decoder;
extern const FFCodec ff_adpcm_ima_ea_eacs_decoder;
extern const FFCodec ff_adpcm_ima_ea_sead_decoder;
extern const FFCodec ff_adpcm_ima_iss_decoder;
extern const FFCodec ff_adpcm_ima_moflex_decoder;
extern const FFCodec ff_adpcm_ima_mtf_decoder;
extern const FFCodec ff_adpcm_ima_oki_decoder;
extern const FFCodec ff_adpcm_ima_qt_encoder;
extern const FFCodec ff_adpcm_ima_qt_decoder;
extern const FFCodec ff_adpcm_ima_rad_decoder;
extern const FFCodec ff_adpcm_ima_ssi_decoder;
extern const FFCodec ff_adpcm_ima_ssi_encoder;
extern const FFCodec ff_adpcm_ima_smjpeg_decoder;
extern const FFCodec ff_adpcm_ima_wav_encoder;
extern const FFCodec ff_adpcm_ima_wav_decoder;
extern const FFCodec ff_adpcm_ima_ws_encoder;
extern const FFCodec ff_adpcm_ima_ws_decoder;
extern const FFCodec ff_adpcm_ms_encoder;
extern const FFCodec ff_adpcm_ms_decoder;
extern const FFCodec ff_adpcm_mtaf_decoder;
extern const FFCodec ff_adpcm_psx_decoder;
extern const FFCodec ff_adpcm_sbpro_2_decoder;
extern const FFCodec ff_adpcm_sbpro_3_decoder;
extern const FFCodec ff_adpcm_sbpro_4_decoder;
extern const FFCodec ff_adpcm_swf_encoder;
extern const FFCodec ff_adpcm_swf_decoder;
extern const FFCodec ff_adpcm_thp_decoder;
extern const FFCodec ff_adpcm_thp_le_decoder;
extern const FFCodec ff_adpcm_vima_decoder;
extern const FFCodec ff_adpcm_xa_decoder;
extern const FFCodec ff_adpcm_xmd_decoder;
extern const FFCodec ff_adpcm_yamaha_encoder;
extern const FFCodec ff_adpcm_yamaha_decoder;
extern const FFCodec ff_adpcm_zork_decoder;

/* subtitles */
extern const FFCodec ff_ssa_encoder;
extern const FFCodec ff_ssa_decoder;
extern const FFCodec ff_ass_encoder;
extern const FFCodec ff_ass_decoder;
extern const FFCodec ff_ccaption_decoder;
extern const FFCodec ff_dvbsub_encoder;
extern const FFCodec ff_dvbsub_decoder;
extern const FFCodec ff_dvdsub_encoder;
extern const FFCodec ff_dvdsub_decoder;
extern const FFCodec ff_jacosub_decoder;
extern const FFCodec ff_microdvd_decoder;
extern const FFCodec ff_movtext_encoder;
extern const FFCodec ff_movtext_decoder;
extern const FFCodec ff_mpl2_decoder;
extern const FFCodec ff_pgssub_decoder;
extern const FFCodec ff_pjs_decoder;
extern const FFCodec ff_realtext_decoder;
extern const FFCodec ff_sami_decoder;
extern const FFCodec ff_srt_encoder;
extern const FFCodec ff_srt_decoder;
extern const FFCodec ff_stl_decoder;
extern const FFCodec ff_subrip_encoder;
extern const FFCodec ff_subrip_decoder;
extern const FFCodec ff_subviewer_decoder;
extern const FFCodec ff_subviewer1_decoder;
extern const FFCodec ff_text_encoder;
extern const FFCodec ff_text_decoder;
extern const FFCodec ff_ttml_encoder;
extern const FFCodec ff_vplayer_decoder;
extern const FFCodec ff_webvtt_encoder;
extern const FFCodec ff_webvtt_decoder;
extern const FFCodec ff_xsub_encoder;
extern const FFCodec ff_xsub_decoder;

/* external libraries */
extern const FFCodec ff_aac_at_encoder;
extern const FFCodec ff_aac_at_decoder;
extern const FFCodec ff_ac3_at_decoder;
extern const FFCodec ff_adpcm_ima_qt_at_decoder;
extern const FFCodec ff_alac_at_encoder;
extern const FFCodec ff_alac_at_decoder;
extern const FFCodec ff_amr_nb_at_decoder;
extern const FFCodec ff_eac3_at_decoder;
extern const FFCodec ff_gsm_ms_at_decoder;
extern const FFCodec ff_ilbc_at_encoder;
extern const FFCodec ff_ilbc_at_decoder;
extern const FFCodec ff_mp1_at_decoder;
extern const FFCodec ff_mp2_at_decoder;
extern const FFCodec ff_mp3_at_decoder;
extern const FFCodec ff_pcm_alaw_at_encoder;
extern const FFCodec ff_pcm_alaw_at_decoder;
extern const FFCodec ff_pcm_mulaw_at_encoder;
extern const FFCodec ff_pcm_mulaw_at_decoder;
extern const FFCodec ff_qdmc_at_decoder;
extern const FFCodec ff_qdm2_at_decoder;
extern FFCodec ff_libaom_av1_encoder;
/* preferred over libaribb24 */
extern const FFCodec ff_libaribcaption_decoder;
extern const FFCodec ff_libaribb24_decoder;
extern const FFCodec ff_libcelt_decoder;
extern const FFCodec ff_libcodec2_encoder;
extern const FFCodec ff_libcodec2_decoder;
extern const FFCodec ff_libdav1d_decoder;
extern const FFCodec ff_libdavs2_decoder;
extern const FFCodec ff_libfdk_aac_encoder;
extern const FFCodec ff_libfdk_aac_decoder;
extern const FFCodec ff_libgsm_encoder;
extern const FFCodec ff_libgsm_decoder;
extern const FFCodec ff_libgsm_ms_encoder;
extern const FFCodec ff_libgsm_ms_decoder;
extern const FFCodec ff_libilbc_encoder;
extern const FFCodec ff_libilbc_decoder;
extern const FFCodec ff_libjxl_decoder;
extern const FFCodec ff_libjxl_encoder;
extern const FFCodec ff_libmp3lame_encoder;
extern const FFCodec ff_libopencore_amrnb_encoder;
extern const FFCodec ff_libopencore_amrnb_decoder;
extern const FFCodec ff_libopencore_amrwb_decoder;
extern const FFCodec ff_libopenjpeg_encoder;
extern const FFCodec ff_libopus_encoder;
extern const FFCodec ff_libopus_decoder;
extern const FFCodec ff_librav1e_encoder;
extern const FFCodec ff_librsvg_decoder;
extern const FFCodec ff_libshine_encoder;
extern const FFCodec ff_libspeex_encoder;
extern const FFCodec ff_libspeex_decoder;
extern const FFCodec ff_libsvtav1_encoder;
extern const FFCodec ff_libtheora_encoder;
extern const FFCodec ff_libtwolame_encoder;
extern const FFCodec ff_libuavs3d_decoder;
extern const FFCodec ff_libvo_amrwbenc_encoder;
extern const FFCodec ff_libvorbis_encoder;
extern const FFCodec ff_libvorbis_decoder;
extern const FFCodec ff_libvpx_vp8_encoder;
extern const FFCodec ff_libvpx_vp8_decoder;
extern FFCodec ff_libvpx_vp9_encoder;
extern const FFCodec ff_libvpx_vp9_decoder;
/* preferred over libwebp */
extern const FFCodec ff_libwebp_anim_encoder;
extern const FFCodec ff_libwebp_encoder;
extern const FFCodec ff_libx262_encoder;
#if CONFIG_LIBX264_ENCODER
#include <x264.h>
#if X264_BUILD < 153
#define LIBX264_CONST
#else
#define LIBX264_CONST const
#endif
extern LIBX264_CONST FFCodec ff_libx264_encoder;
#endif
extern const FFCodec ff_libx264rgb_encoder;
extern FFCodec ff_libx265_encoder;
extern const FFCodec ff_libxeve_encoder;
extern const FFCodec ff_libxevd_decoder;
extern const FFCodec ff_libxavs_encoder;
extern const FFCodec ff_libxavs2_encoder;
extern const FFCodec ff_libxvid_encoder;
extern const FFCodec ff_libzvbi_teletext_decoder;

/* text */
extern const FFCodec ff_bintext_decoder;
extern const FFCodec ff_xbin_decoder;
extern const FFCodec ff_idf_decoder;

/* external libraries, that shouldn't be used by default if one of the
 * above is available */
extern const FFCodec ff_aac_mf_encoder;
extern const FFCodec ff_ac3_mf_encoder;
extern const FFCodec ff_h263_v4l2m2m_encoder;
extern const FFCodec ff_libaom_av1_decoder;
/* hwaccel hooks only, so prefer external decoders */
extern const FFCodec ff_av1_decoder;
extern const FFCodec ff_av1_cuvid_decoder;
extern const FFCodec ff_av1_mediacodec_decoder;
extern const FFCodec ff_av1_mediacodec_encoder;
extern const FFCodec ff_av1_nvenc_encoder;
extern const FFCodec ff_av1_qsv_decoder;
extern const FFCodec ff_av1_qsv_encoder;
extern const FFCodec ff_av1_amf_encoder;
extern const FFCodec ff_av1_vaapi_encoder;
extern const FFCodec ff_libopenh264_encoder;
extern const FFCodec ff_libopenh264_decoder;
extern const FFCodec ff_h264_amf_encoder;
extern const FFCodec ff_h264_cuvid_decoder;
extern const FFCodec ff_h264_mf_encoder;
extern const FFCodec ff_h264_nvenc_encoder;
extern const FFCodec ff_h264_omx_encoder;
extern const FFCodec ff_h264_qsv_encoder;
extern const FFCodec ff_h264_v4l2m2m_encoder;
extern const FFCodec ff_h264_vaapi_encoder;
extern const FFCodec ff_h264_videotoolbox_encoder;
extern const FFCodec ff_hevc_amf_encoder;
extern const FFCodec ff_hevc_cuvid_decoder;
extern const FFCodec ff_hevc_mediacodec_decoder;
extern const FFCodec ff_hevc_mediacodec_encoder;
extern const FFCodec ff_hevc_mf_encoder;
extern const FFCodec ff_hevc_nvenc_encoder;
extern const FFCodec ff_hevc_qsv_encoder;
extern const FFCodec ff_hevc_v4l2m2m_encoder;
extern const FFCodec ff_hevc_vaapi_encoder;
extern const FFCodec ff_hevc_videotoolbox_encoder;
extern const FFCodec ff_libkvazaar_encoder;
extern const FFCodec ff_mjpeg_cuvid_decoder;
extern const FFCodec ff_mjpeg_qsv_encoder;
extern const FFCodec ff_mjpeg_qsv_decoder;
extern const FFCodec ff_mjpeg_vaapi_encoder;
extern const FFCodec ff_mp3_mf_encoder;
extern const FFCodec ff_mpeg1_cuvid_decoder;
extern const FFCodec ff_mpeg2_cuvid_decoder;
extern const FFCodec ff_mpeg2_qsv_encoder;
extern const FFCodec ff_mpeg2_vaapi_encoder;
extern const FFCodec ff_mpeg4_cuvid_decoder;
extern const FFCodec ff_mpeg4_mediacodec_decoder;
extern const FFCodec ff_mpeg4_mediacodec_encoder;
extern const FFCodec ff_mpeg4_omx_encoder;
extern const FFCodec ff_mpeg4_v4l2m2m_encoder;
extern const FFCodec ff_prores_videotoolbox_encoder;
extern const FFCodec ff_vc1_cuvid_decoder;
extern const FFCodec ff_vp8_cuvid_decoder;
extern const FFCodec ff_vp8_mediacodec_decoder;
extern const FFCodec ff_vp8_mediacodec_encoder;
extern const FFCodec ff_vp8_qsv_decoder;
extern const FFCodec ff_vp8_v4l2m2m_encoder;
extern const FFCodec ff_vp8_vaapi_encoder;
extern const FFCodec ff_vp9_cuvid_decoder;
extern const FFCodec ff_vp9_mediacodec_decoder;
extern const FFCodec ff_vp9_mediacodec_encoder;
extern const FFCodec ff_vp9_qsv_decoder;
extern const FFCodec ff_vp9_vaapi_encoder;
extern const FFCodec ff_vp9_qsv_encoder;

// null codecs
extern const FFCodec ff_vnull_decoder;
extern const FFCodec ff_vnull_encoder;
extern const FFCodec ff_anull_decoder;
extern const FFCodec ff_anull_encoder;

// The iterate API is not usable with ossfuzz due to the excessive size of binaries created
#if CONFIG_OSSFUZZ
const FFCodec * codec_list[] = {
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
            codec_list[i]->init_static_data((FFCodec*)codec_list[i]);
    }
}

const AVCodec *av_codec_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const FFCodec *c = codec_list[i];

    ff_thread_once(&av_codec_static_init, av_codec_init_static);

    if (c) {
        *opaque = (void*)(i + 1);
        return &c->p;
    }
    return NULL;
}

static enum AVCodecID remap_deprecated_codec_id(enum AVCodecID id)
{
    switch(id){
        //This is for future deprecatec codec ids, its empty since
        //last major bump but will fill up again over time, please don't remove it
        default                                         : return id;
    }
}

static const AVCodec *find_codec(enum AVCodecID id, int (*x)(const AVCodec *))
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
                return p;
        }
    }

    return experimental;
}

const AVCodec *avcodec_find_encoder(enum AVCodecID id)
{
    return find_codec(id, av_codec_is_encoder);
}

const AVCodec *avcodec_find_decoder(enum AVCodecID id)
{
    return find_codec(id, av_codec_is_decoder);
}

static const AVCodec *find_codec_by_name(const char *name, int (*x)(const AVCodec *))
{
    void *i = 0;
    const AVCodec *p;

    if (!name)
        return NULL;

    while ((p = av_codec_iterate(&i))) {
        if (!x(p))
            continue;
        if (strcmp(name, p->name) == 0)
            return p;
    }

    return NULL;
}

const AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    return find_codec_by_name(name, av_codec_is_encoder);
}

const AVCodec *avcodec_find_decoder_by_name(const char *name)
{
    return find_codec_by_name(name, av_codec_is_decoder);
}
