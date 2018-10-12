/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/thread.h"
#include "libavformat/internal.h"
#include "avformat.h"
#include "rtp.h"
#include "rdt.h"
#include "url.h"
#include "version.h"

/* (de)muxers */
extern AVOutputFormat ff_a64_muxer;
extern AVInputFormat  ff_aa_demuxer;
extern AVInputFormat  ff_aac_demuxer;
extern AVInputFormat  ff_ac3_demuxer;
extern AVOutputFormat ff_ac3_muxer;
extern AVInputFormat  ff_acm_demuxer;
extern AVInputFormat  ff_act_demuxer;
extern AVInputFormat  ff_adf_demuxer;
extern AVInputFormat  ff_adp_demuxer;
extern AVInputFormat  ff_ads_demuxer;
extern AVOutputFormat ff_adts_muxer;
extern AVInputFormat  ff_adx_demuxer;
extern AVOutputFormat ff_adx_muxer;
extern AVInputFormat  ff_aea_demuxer;
extern AVInputFormat  ff_afc_demuxer;
extern AVInputFormat  ff_aiff_demuxer;
extern AVOutputFormat ff_aiff_muxer;
extern AVInputFormat  ff_aix_demuxer;
extern AVInputFormat  ff_amr_demuxer;
extern AVOutputFormat ff_amr_muxer;
extern AVInputFormat  ff_amrnb_demuxer;
extern AVInputFormat  ff_amrwb_demuxer;
extern AVInputFormat  ff_anm_demuxer;
extern AVInputFormat  ff_apc_demuxer;
extern AVInputFormat  ff_ape_demuxer;
extern AVInputFormat  ff_apng_demuxer;
extern AVOutputFormat ff_apng_muxer;
extern AVInputFormat  ff_aptx_demuxer;
extern AVOutputFormat ff_aptx_muxer;
extern AVInputFormat  ff_aptx_hd_demuxer;
extern AVOutputFormat ff_aptx_hd_muxer;
extern AVInputFormat  ff_aqtitle_demuxer;
extern AVInputFormat  ff_asf_demuxer;
extern AVOutputFormat ff_asf_muxer;
extern AVInputFormat  ff_asf_o_demuxer;
extern AVInputFormat  ff_ass_demuxer;
extern AVOutputFormat ff_ass_muxer;
extern AVInputFormat  ff_ast_demuxer;
extern AVOutputFormat ff_ast_muxer;
extern AVOutputFormat ff_asf_stream_muxer;
extern AVInputFormat  ff_au_demuxer;
extern AVOutputFormat ff_au_muxer;
extern AVInputFormat  ff_avi_demuxer;
extern AVOutputFormat ff_avi_muxer;
extern AVInputFormat  ff_avisynth_demuxer;
extern AVOutputFormat ff_avm2_muxer;
extern AVInputFormat  ff_avr_demuxer;
extern AVInputFormat  ff_avs_demuxer;
extern AVInputFormat  ff_avs2_demuxer;
extern AVOutputFormat ff_avs2_muxer;
extern AVInputFormat  ff_bethsoftvid_demuxer;
extern AVInputFormat  ff_bfi_demuxer;
extern AVInputFormat  ff_bintext_demuxer;
extern AVInputFormat  ff_bink_demuxer;
extern AVInputFormat  ff_bit_demuxer;
extern AVOutputFormat ff_bit_muxer;
extern AVInputFormat  ff_bmv_demuxer;
extern AVInputFormat  ff_bfstm_demuxer;
extern AVInputFormat  ff_brstm_demuxer;
extern AVInputFormat  ff_boa_demuxer;
extern AVInputFormat  ff_c93_demuxer;
extern AVInputFormat  ff_caf_demuxer;
extern AVOutputFormat ff_caf_muxer;
extern AVInputFormat  ff_cavsvideo_demuxer;
extern AVOutputFormat ff_cavsvideo_muxer;
extern AVInputFormat  ff_cdg_demuxer;
extern AVInputFormat  ff_cdxl_demuxer;
extern AVInputFormat  ff_cine_demuxer;
extern AVInputFormat  ff_codec2_demuxer;
extern AVOutputFormat ff_codec2_muxer;
extern AVInputFormat  ff_codec2raw_demuxer;
extern AVOutputFormat ff_codec2raw_muxer;
extern AVInputFormat  ff_concat_demuxer;
extern AVOutputFormat ff_crc_muxer;
extern AVInputFormat  ff_dash_demuxer;
extern AVOutputFormat ff_dash_muxer;
extern AVInputFormat  ff_data_demuxer;
extern AVOutputFormat ff_data_muxer;
extern AVInputFormat  ff_daud_demuxer;
extern AVOutputFormat ff_daud_muxer;
extern AVInputFormat  ff_dcstr_demuxer;
extern AVInputFormat  ff_dfa_demuxer;
extern AVInputFormat  ff_dirac_demuxer;
extern AVOutputFormat ff_dirac_muxer;
extern AVInputFormat  ff_dnxhd_demuxer;
extern AVOutputFormat ff_dnxhd_muxer;
extern AVInputFormat  ff_dsf_demuxer;
extern AVInputFormat  ff_dsicin_demuxer;
extern AVInputFormat  ff_dss_demuxer;
extern AVInputFormat  ff_dts_demuxer;
extern AVOutputFormat ff_dts_muxer;
extern AVInputFormat  ff_dtshd_demuxer;
extern AVInputFormat  ff_dv_demuxer;
extern AVOutputFormat ff_dv_muxer;
extern AVInputFormat  ff_dvbsub_demuxer;
extern AVInputFormat  ff_dvbtxt_demuxer;
extern AVInputFormat  ff_dxa_demuxer;
extern AVInputFormat  ff_ea_demuxer;
extern AVInputFormat  ff_ea_cdata_demuxer;
extern AVInputFormat  ff_eac3_demuxer;
extern AVOutputFormat ff_eac3_muxer;
extern AVInputFormat  ff_epaf_demuxer;
extern AVOutputFormat ff_f4v_muxer;
extern AVInputFormat  ff_ffmetadata_demuxer;
extern AVOutputFormat ff_ffmetadata_muxer;
extern AVOutputFormat ff_fifo_muxer;
extern AVOutputFormat ff_fifo_test_muxer;
extern AVInputFormat  ff_filmstrip_demuxer;
extern AVOutputFormat ff_filmstrip_muxer;
extern AVInputFormat  ff_fits_demuxer;
extern AVOutputFormat ff_fits_muxer;
extern AVInputFormat  ff_flac_demuxer;
extern AVOutputFormat ff_flac_muxer;
extern AVInputFormat  ff_flic_demuxer;
extern AVInputFormat  ff_flv_demuxer;
extern AVOutputFormat ff_flv_muxer;
extern AVInputFormat  ff_live_flv_demuxer;
extern AVInputFormat  ff_fourxm_demuxer;
extern AVOutputFormat ff_framecrc_muxer;
extern AVOutputFormat ff_framehash_muxer;
extern AVOutputFormat ff_framemd5_muxer;
extern AVInputFormat  ff_frm_demuxer;
extern AVInputFormat  ff_fsb_demuxer;
extern AVInputFormat  ff_g722_demuxer;
extern AVOutputFormat ff_g722_muxer;
extern AVInputFormat  ff_g723_1_demuxer;
extern AVOutputFormat ff_g723_1_muxer;
extern AVInputFormat  ff_g726_demuxer;
extern AVOutputFormat ff_g726_muxer;
extern AVInputFormat  ff_g726le_demuxer;
extern AVOutputFormat ff_g726le_muxer;
extern AVInputFormat  ff_g729_demuxer;
extern AVInputFormat  ff_gdv_demuxer;
extern AVInputFormat  ff_genh_demuxer;
extern AVInputFormat  ff_gif_demuxer;
extern AVOutputFormat ff_gif_muxer;
extern AVInputFormat  ff_gsm_demuxer;
extern AVOutputFormat ff_gsm_muxer;
extern AVInputFormat  ff_gxf_demuxer;
extern AVOutputFormat ff_gxf_muxer;
extern AVInputFormat  ff_h261_demuxer;
extern AVOutputFormat ff_h261_muxer;
extern AVInputFormat  ff_h263_demuxer;
extern AVOutputFormat ff_h263_muxer;
extern AVInputFormat  ff_h264_demuxer;
extern AVOutputFormat ff_h264_muxer;
extern AVOutputFormat ff_hash_muxer;
extern AVOutputFormat ff_hds_muxer;
extern AVInputFormat  ff_hevc_demuxer;
extern AVOutputFormat ff_hevc_muxer;
extern AVInputFormat  ff_hls_demuxer;
extern AVOutputFormat ff_hls_muxer;
extern AVInputFormat  ff_hnm_demuxer;
extern AVInputFormat  ff_ico_demuxer;
extern AVOutputFormat ff_ico_muxer;
extern AVInputFormat  ff_idcin_demuxer;
extern AVInputFormat  ff_idf_demuxer;
extern AVInputFormat  ff_iff_demuxer;
extern AVInputFormat  ff_ilbc_demuxer;
extern AVOutputFormat ff_ilbc_muxer;
extern AVInputFormat  ff_image2_demuxer;
extern AVOutputFormat ff_image2_muxer;
extern AVInputFormat  ff_image2pipe_demuxer;
extern AVOutputFormat ff_image2pipe_muxer;
extern AVInputFormat  ff_image2_alias_pix_demuxer;
extern AVInputFormat  ff_image2_brender_pix_demuxer;
extern AVInputFormat  ff_ingenient_demuxer;
extern AVInputFormat  ff_ipmovie_demuxer;
extern AVOutputFormat ff_ipod_muxer;
extern AVInputFormat  ff_ircam_demuxer;
extern AVOutputFormat ff_ircam_muxer;
extern AVOutputFormat ff_ismv_muxer;
extern AVInputFormat  ff_iss_demuxer;
extern AVInputFormat  ff_iv8_demuxer;
extern AVInputFormat  ff_ivf_demuxer;
extern AVOutputFormat ff_ivf_muxer;
extern AVInputFormat  ff_ivr_demuxer;
extern AVInputFormat  ff_jacosub_demuxer;
extern AVOutputFormat ff_jacosub_muxer;
extern AVInputFormat  ff_jv_demuxer;
extern AVOutputFormat ff_latm_muxer;
extern AVInputFormat  ff_lmlm4_demuxer;
extern AVInputFormat  ff_loas_demuxer;
extern AVInputFormat  ff_lrc_demuxer;
extern AVOutputFormat ff_lrc_muxer;
extern AVInputFormat  ff_lvf_demuxer;
extern AVInputFormat  ff_lxf_demuxer;
extern AVInputFormat  ff_m4v_demuxer;
extern AVOutputFormat ff_m4v_muxer;
extern AVOutputFormat ff_md5_muxer;
extern AVInputFormat  ff_matroska_demuxer;
extern AVOutputFormat ff_matroska_muxer;
extern AVOutputFormat ff_matroska_audio_muxer;
extern AVInputFormat  ff_mgsts_demuxer;
extern AVInputFormat  ff_microdvd_demuxer;
extern AVOutputFormat ff_microdvd_muxer;
extern AVInputFormat  ff_mjpeg_demuxer;
extern AVOutputFormat ff_mjpeg_muxer;
extern AVInputFormat  ff_mjpeg_2000_demuxer;
extern AVInputFormat  ff_mlp_demuxer;
extern AVOutputFormat ff_mlp_muxer;
extern AVInputFormat  ff_mlv_demuxer;
extern AVInputFormat  ff_mm_demuxer;
extern AVInputFormat  ff_mmf_demuxer;
extern AVOutputFormat ff_mmf_muxer;
extern AVInputFormat  ff_mov_demuxer;
extern AVOutputFormat ff_mov_muxer;
extern AVOutputFormat ff_mp2_muxer;
extern AVInputFormat  ff_mp3_demuxer;
extern AVOutputFormat ff_mp3_muxer;
extern AVOutputFormat ff_mp4_muxer;
extern AVInputFormat  ff_mpc_demuxer;
extern AVInputFormat  ff_mpc8_demuxer;
extern AVOutputFormat ff_mpeg1system_muxer;
extern AVOutputFormat ff_mpeg1vcd_muxer;
extern AVOutputFormat ff_mpeg1video_muxer;
extern AVOutputFormat ff_mpeg2dvd_muxer;
extern AVOutputFormat ff_mpeg2svcd_muxer;
extern AVOutputFormat ff_mpeg2video_muxer;
extern AVOutputFormat ff_mpeg2vob_muxer;
extern AVInputFormat  ff_mpegps_demuxer;
extern AVInputFormat  ff_mpegts_demuxer;
extern AVOutputFormat ff_mpegts_muxer;
extern AVInputFormat  ff_mpegtsraw_demuxer;
extern AVInputFormat  ff_mpegvideo_demuxer;
extern AVInputFormat  ff_mpjpeg_demuxer;
extern AVOutputFormat ff_mpjpeg_muxer;
extern AVInputFormat  ff_mpl2_demuxer;
extern AVInputFormat  ff_mpsub_demuxer;
extern AVInputFormat  ff_msf_demuxer;
extern AVInputFormat  ff_msnwc_tcp_demuxer;
extern AVInputFormat  ff_mtaf_demuxer;
extern AVInputFormat  ff_mtv_demuxer;
extern AVInputFormat  ff_musx_demuxer;
extern AVInputFormat  ff_mv_demuxer;
extern AVInputFormat  ff_mvi_demuxer;
extern AVInputFormat  ff_mxf_demuxer;
extern AVOutputFormat ff_mxf_muxer;
extern AVOutputFormat ff_mxf_d10_muxer;
extern AVOutputFormat ff_mxf_opatom_muxer;
extern AVInputFormat  ff_mxg_demuxer;
extern AVInputFormat  ff_nc_demuxer;
extern AVInputFormat  ff_nistsphere_demuxer;
extern AVInputFormat  ff_nsp_demuxer;
extern AVInputFormat  ff_nsv_demuxer;
extern AVOutputFormat ff_null_muxer;
extern AVInputFormat  ff_nut_demuxer;
extern AVOutputFormat ff_nut_muxer;
extern AVInputFormat  ff_nuv_demuxer;
extern AVOutputFormat ff_oga_muxer;
extern AVInputFormat  ff_ogg_demuxer;
extern AVOutputFormat ff_ogg_muxer;
extern AVOutputFormat ff_ogv_muxer;
extern AVInputFormat  ff_oma_demuxer;
extern AVOutputFormat ff_oma_muxer;
extern AVOutputFormat ff_opus_muxer;
extern AVInputFormat  ff_paf_demuxer;
extern AVInputFormat  ff_pcm_alaw_demuxer;
extern AVOutputFormat ff_pcm_alaw_muxer;
extern AVInputFormat  ff_pcm_mulaw_demuxer;
extern AVOutputFormat ff_pcm_mulaw_muxer;
extern AVInputFormat  ff_pcm_f64be_demuxer;
extern AVOutputFormat ff_pcm_f64be_muxer;
extern AVInputFormat  ff_pcm_f64le_demuxer;
extern AVOutputFormat ff_pcm_f64le_muxer;
extern AVInputFormat  ff_pcm_f32be_demuxer;
extern AVOutputFormat ff_pcm_f32be_muxer;
extern AVInputFormat  ff_pcm_f32le_demuxer;
extern AVOutputFormat ff_pcm_f32le_muxer;
extern AVInputFormat  ff_pcm_s32be_demuxer;
extern AVOutputFormat ff_pcm_s32be_muxer;
extern AVInputFormat  ff_pcm_s32le_demuxer;
extern AVOutputFormat ff_pcm_s32le_muxer;
extern AVInputFormat  ff_pcm_s24be_demuxer;
extern AVOutputFormat ff_pcm_s24be_muxer;
extern AVInputFormat  ff_pcm_s24le_demuxer;
extern AVOutputFormat ff_pcm_s24le_muxer;
extern AVInputFormat  ff_pcm_s16be_demuxer;
extern AVOutputFormat ff_pcm_s16be_muxer;
extern AVInputFormat  ff_pcm_s16le_demuxer;
extern AVOutputFormat ff_pcm_s16le_muxer;
extern AVInputFormat  ff_pcm_s8_demuxer;
extern AVOutputFormat ff_pcm_s8_muxer;
extern AVInputFormat  ff_pcm_u32be_demuxer;
extern AVOutputFormat ff_pcm_u32be_muxer;
extern AVInputFormat  ff_pcm_u32le_demuxer;
extern AVOutputFormat ff_pcm_u32le_muxer;
extern AVInputFormat  ff_pcm_u24be_demuxer;
extern AVOutputFormat ff_pcm_u24be_muxer;
extern AVInputFormat  ff_pcm_u24le_demuxer;
extern AVOutputFormat ff_pcm_u24le_muxer;
extern AVInputFormat  ff_pcm_u16be_demuxer;
extern AVOutputFormat ff_pcm_u16be_muxer;
extern AVInputFormat  ff_pcm_u16le_demuxer;
extern AVOutputFormat ff_pcm_u16le_muxer;
extern AVInputFormat  ff_pcm_u8_demuxer;
extern AVOutputFormat ff_pcm_u8_muxer;
extern AVInputFormat  ff_pjs_demuxer;
extern AVInputFormat  ff_pmp_demuxer;
extern AVOutputFormat ff_psp_muxer;
extern AVInputFormat  ff_pva_demuxer;
extern AVInputFormat  ff_pvf_demuxer;
extern AVInputFormat  ff_qcp_demuxer;
extern AVInputFormat  ff_r3d_demuxer;
extern AVInputFormat  ff_rawvideo_demuxer;
extern AVOutputFormat ff_rawvideo_muxer;
extern AVInputFormat  ff_realtext_demuxer;
extern AVInputFormat  ff_redspark_demuxer;
extern AVInputFormat  ff_rl2_demuxer;
extern AVInputFormat  ff_rm_demuxer;
extern AVOutputFormat ff_rm_muxer;
extern AVInputFormat  ff_roq_demuxer;
extern AVOutputFormat ff_roq_muxer;
extern AVInputFormat  ff_rpl_demuxer;
extern AVInputFormat  ff_rsd_demuxer;
extern AVInputFormat  ff_rso_demuxer;
extern AVOutputFormat ff_rso_muxer;
extern AVInputFormat  ff_rtp_demuxer;
extern AVOutputFormat ff_rtp_muxer;
extern AVOutputFormat ff_rtp_mpegts_muxer;
extern AVInputFormat  ff_rtsp_demuxer;
extern AVOutputFormat ff_rtsp_muxer;
extern AVInputFormat  ff_s337m_demuxer;
extern AVInputFormat  ff_sami_demuxer;
extern AVInputFormat  ff_sap_demuxer;
extern AVOutputFormat ff_sap_muxer;
extern AVInputFormat  ff_sbc_demuxer;
extern AVOutputFormat ff_sbc_muxer;
extern AVInputFormat  ff_sbg_demuxer;
extern AVInputFormat  ff_scc_demuxer;
extern AVOutputFormat ff_scc_muxer;
extern AVInputFormat  ff_sdp_demuxer;
extern AVInputFormat  ff_sdr2_demuxer;
extern AVInputFormat  ff_sds_demuxer;
extern AVInputFormat  ff_sdx_demuxer;
extern AVInputFormat  ff_segafilm_demuxer;
extern AVOutputFormat ff_segafilm_muxer;
extern AVOutputFormat ff_segment_muxer;
extern AVOutputFormat ff_stream_segment_muxer;
extern AVInputFormat  ff_ser_demuxer;
extern AVInputFormat  ff_shorten_demuxer;
extern AVInputFormat  ff_siff_demuxer;
extern AVOutputFormat ff_singlejpeg_muxer;
extern AVInputFormat  ff_sln_demuxer;
extern AVInputFormat  ff_smacker_demuxer;
extern AVInputFormat  ff_smjpeg_demuxer;
extern AVOutputFormat ff_smjpeg_muxer;
extern AVOutputFormat ff_smoothstreaming_muxer;
extern AVInputFormat  ff_smush_demuxer;
extern AVInputFormat  ff_sol_demuxer;
extern AVInputFormat  ff_sox_demuxer;
extern AVOutputFormat ff_sox_muxer;
extern AVOutputFormat ff_spx_muxer;
extern AVInputFormat  ff_spdif_demuxer;
extern AVOutputFormat ff_spdif_muxer;
extern AVInputFormat  ff_srt_demuxer;
extern AVOutputFormat ff_srt_muxer;
extern AVInputFormat  ff_str_demuxer;
extern AVInputFormat  ff_stl_demuxer;
extern AVInputFormat  ff_subviewer1_demuxer;
extern AVInputFormat  ff_subviewer_demuxer;
extern AVInputFormat  ff_sup_demuxer;
extern AVOutputFormat ff_sup_muxer;
extern AVInputFormat  ff_svag_demuxer;
extern AVInputFormat  ff_swf_demuxer;
extern AVOutputFormat ff_swf_muxer;
extern AVInputFormat  ff_tak_demuxer;
extern AVOutputFormat ff_tee_muxer;
extern AVInputFormat  ff_tedcaptions_demuxer;
extern AVOutputFormat ff_tg2_muxer;
extern AVOutputFormat ff_tgp_muxer;
extern AVInputFormat  ff_thp_demuxer;
extern AVInputFormat  ff_threedostr_demuxer;
extern AVInputFormat  ff_tiertexseq_demuxer;
extern AVOutputFormat ff_mkvtimestamp_v2_muxer;
extern AVInputFormat  ff_tmv_demuxer;
extern AVInputFormat  ff_truehd_demuxer;
extern AVOutputFormat ff_truehd_muxer;
extern AVInputFormat  ff_tta_demuxer;
extern AVOutputFormat ff_tta_muxer;
extern AVInputFormat  ff_txd_demuxer;
extern AVInputFormat  ff_tty_demuxer;
extern AVInputFormat  ff_ty_demuxer;
extern AVOutputFormat ff_uncodedframecrc_muxer;
extern AVInputFormat  ff_v210_demuxer;
extern AVInputFormat  ff_v210x_demuxer;
extern AVInputFormat  ff_vag_demuxer;
extern AVInputFormat  ff_vc1_demuxer;
extern AVOutputFormat ff_vc1_muxer;
extern AVInputFormat  ff_vc1t_demuxer;
extern AVOutputFormat ff_vc1t_muxer;
extern AVInputFormat  ff_vivo_demuxer;
extern AVInputFormat  ff_vmd_demuxer;
extern AVInputFormat  ff_vobsub_demuxer;
extern AVInputFormat  ff_voc_demuxer;
extern AVOutputFormat ff_voc_muxer;
extern AVInputFormat  ff_vpk_demuxer;
extern AVInputFormat  ff_vplayer_demuxer;
extern AVInputFormat  ff_vqf_demuxer;
extern AVInputFormat  ff_w64_demuxer;
extern AVOutputFormat ff_w64_muxer;
extern AVInputFormat  ff_wav_demuxer;
extern AVOutputFormat ff_wav_muxer;
extern AVInputFormat  ff_wc3_demuxer;
extern AVOutputFormat ff_webm_muxer;
extern AVInputFormat  ff_webm_dash_manifest_demuxer;
extern AVOutputFormat ff_webm_dash_manifest_muxer;
extern AVOutputFormat ff_webm_chunk_muxer;
extern AVOutputFormat ff_webp_muxer;
extern AVInputFormat  ff_webvtt_demuxer;
extern AVOutputFormat ff_webvtt_muxer;
extern AVInputFormat  ff_wsaud_demuxer;
extern AVInputFormat  ff_wsd_demuxer;
extern AVInputFormat  ff_wsvqa_demuxer;
extern AVInputFormat  ff_wtv_demuxer;
extern AVOutputFormat ff_wtv_muxer;
extern AVInputFormat  ff_wve_demuxer;
extern AVInputFormat  ff_wv_demuxer;
extern AVOutputFormat ff_wv_muxer;
extern AVInputFormat  ff_xa_demuxer;
extern AVInputFormat  ff_xbin_demuxer;
extern AVInputFormat  ff_xmv_demuxer;
extern AVInputFormat  ff_xvag_demuxer;
extern AVInputFormat  ff_xwma_demuxer;
extern AVInputFormat  ff_yop_demuxer;
extern AVInputFormat  ff_yuv4mpegpipe_demuxer;
extern AVOutputFormat ff_yuv4mpegpipe_muxer;
/* image demuxers */
extern AVInputFormat  ff_image_bmp_pipe_demuxer;
extern AVInputFormat  ff_image_dds_pipe_demuxer;
extern AVInputFormat  ff_image_dpx_pipe_demuxer;
extern AVInputFormat  ff_image_exr_pipe_demuxer;
extern AVInputFormat  ff_image_j2k_pipe_demuxer;
extern AVInputFormat  ff_image_jpeg_pipe_demuxer;
extern AVInputFormat  ff_image_jpegls_pipe_demuxer;
extern AVInputFormat  ff_image_pam_pipe_demuxer;
extern AVInputFormat  ff_image_pbm_pipe_demuxer;
extern AVInputFormat  ff_image_pcx_pipe_demuxer;
extern AVInputFormat  ff_image_pgmyuv_pipe_demuxer;
extern AVInputFormat  ff_image_pgm_pipe_demuxer;
extern AVInputFormat  ff_image_pictor_pipe_demuxer;
extern AVInputFormat  ff_image_png_pipe_demuxer;
extern AVInputFormat  ff_image_ppm_pipe_demuxer;
extern AVInputFormat  ff_image_psd_pipe_demuxer;
extern AVInputFormat  ff_image_qdraw_pipe_demuxer;
extern AVInputFormat  ff_image_sgi_pipe_demuxer;
extern AVInputFormat  ff_image_svg_pipe_demuxer;
extern AVInputFormat  ff_image_sunrast_pipe_demuxer;
extern AVInputFormat  ff_image_tiff_pipe_demuxer;
extern AVInputFormat  ff_image_webp_pipe_demuxer;
extern AVInputFormat  ff_image_xpm_pipe_demuxer;
extern AVInputFormat  ff_image_xwd_pipe_demuxer;

/* external libraries */
extern AVOutputFormat ff_chromaprint_muxer;
extern AVInputFormat  ff_libgme_demuxer;
extern AVInputFormat  ff_libmodplug_demuxer;
extern AVInputFormat  ff_libopenmpt_demuxer;
extern AVInputFormat  ff_vapoursynth_demuxer;

#include "libavformat/muxer_list.c"
#include "libavformat/demuxer_list.c"

static const AVInputFormat * const *indev_list = NULL;
static const AVOutputFormat * const *outdev_list = NULL;

const AVOutputFormat *av_muxer_iterate(void **opaque)
{
    static const uintptr_t size = sizeof(muxer_list)/sizeof(muxer_list[0]) - 1;
    uintptr_t i = (uintptr_t)*opaque;
    const AVOutputFormat *f = NULL;

    if (i < size) {
        f = muxer_list[i];
    } else if (indev_list) {
        f = outdev_list[i - size];
    }

    if (f)
        *opaque = (void*)(i + 1);
    return f;
}

const AVInputFormat *av_demuxer_iterate(void **opaque)
{
    static const uintptr_t size = sizeof(demuxer_list)/sizeof(demuxer_list[0]) - 1;
    uintptr_t i = (uintptr_t)*opaque;
    const AVInputFormat *f = NULL;

    if (i < size) {
        f = demuxer_list[i];
    } else if (outdev_list) {
        f = indev_list[i - size];
    }

    if (f)
        *opaque = (void*)(i + 1);
    return f;
}

static AVMutex avpriv_register_devices_mutex = AV_MUTEX_INITIALIZER;

#if FF_API_NEXT
FF_DISABLE_DEPRECATION_WARNINGS
static AVOnce av_format_next_init = AV_ONCE_INIT;

static void av_format_init_next(void)
{
    AVOutputFormat *prevout = NULL, *out;
    AVInputFormat *previn = NULL, *in;

    ff_mutex_lock(&avpriv_register_devices_mutex);

    for (int i = 0; (out = (AVOutputFormat*)muxer_list[i]); i++) {
        if (prevout)
            prevout->next = out;
        prevout = out;
    }

    if (outdev_list) {
        for (int i = 0; (out = (AVOutputFormat*)outdev_list[i]); i++) {
            if (prevout)
                prevout->next = out;
            prevout = out;
        }
    }

    for (int i = 0; (in = (AVInputFormat*)demuxer_list[i]); i++) {
        if (previn)
            previn->next = in;
        previn = in;
    }

    if (indev_list) {
        for (int i = 0; (in = (AVInputFormat*)indev_list[i]); i++) {
            if (previn)
                previn->next = in;
            previn = in;
        }
    }

    ff_mutex_unlock(&avpriv_register_devices_mutex);
}

AVInputFormat *av_iformat_next(const AVInputFormat *f)
{
    ff_thread_once(&av_format_next_init, av_format_init_next);

    if (f)
        return f->next;
    else {
        void *opaque = NULL;
        return (AVInputFormat *)av_demuxer_iterate(&opaque);
    }
}

AVOutputFormat *av_oformat_next(const AVOutputFormat *f)
{
    ff_thread_once(&av_format_next_init, av_format_init_next);

    if (f)
        return f->next;
    else {
        void *opaque = NULL;
        return (AVOutputFormat *)av_muxer_iterate(&opaque);
    }
}

void av_register_all(void)
{
    ff_thread_once(&av_format_next_init, av_format_init_next);
}

void av_register_input_format(AVInputFormat *format)
{
    ff_thread_once(&av_format_next_init, av_format_init_next);
}

void av_register_output_format(AVOutputFormat *format)
{
    ff_thread_once(&av_format_next_init, av_format_init_next);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

void avpriv_register_devices(const AVOutputFormat * const o[], const AVInputFormat * const i[])
{
    ff_mutex_lock(&avpriv_register_devices_mutex);
    outdev_list = o;
    indev_list = i;
    ff_mutex_unlock(&avpriv_register_devices_mutex);
#if FF_API_NEXT
    av_format_init_next();
#endif
}
