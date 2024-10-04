/*
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

#include <stdint.h>

#include "avcodec.h"

extern const AVCodecParser ff_aac_parser;
extern const AVCodecParser ff_aac_latm_parser;
extern const AVCodecParser ff_ac3_parser;
extern const AVCodecParser ff_adx_parser;
extern const AVCodecParser ff_amr_parser;
extern const AVCodecParser ff_av1_parser;
extern const AVCodecParser ff_avs2_parser;
extern const AVCodecParser ff_avs3_parser;
extern const AVCodecParser ff_bmp_parser;
extern const AVCodecParser ff_cavsvideo_parser;
extern const AVCodecParser ff_cook_parser;
extern const AVCodecParser ff_cri_parser;
extern const AVCodecParser ff_dca_parser;
extern const AVCodecParser ff_dirac_parser;
extern const AVCodecParser ff_dnxhd_parser;
extern const AVCodecParser ff_dnxuc_parser;
extern const AVCodecParser ff_dolby_e_parser;
extern const AVCodecParser ff_dpx_parser;
extern const AVCodecParser ff_dvaudio_parser;
extern const AVCodecParser ff_dvbsub_parser;
extern const AVCodecParser ff_dvdsub_parser;
extern const AVCodecParser ff_dvd_nav_parser;
extern const AVCodecParser ff_evc_parser;
extern const AVCodecParser ff_flac_parser;
extern const AVCodecParser ff_ftr_parser;
extern const AVCodecParser ff_g723_1_parser;
extern const AVCodecParser ff_g729_parser;
extern const AVCodecParser ff_gif_parser;
extern const AVCodecParser ff_gsm_parser;
extern const AVCodecParser ff_h261_parser;
extern const AVCodecParser ff_h263_parser;
extern const AVCodecParser ff_h264_parser;
extern const AVCodecParser ff_hevc_parser;
extern const AVCodecParser ff_hdr_parser;
extern const AVCodecParser ff_ipu_parser;
extern const AVCodecParser ff_jpeg2000_parser;
extern const AVCodecParser ff_jpegxl_parser;
extern const AVCodecParser ff_misc4_parser;
extern const AVCodecParser ff_mjpeg_parser;
extern const AVCodecParser ff_mlp_parser;
extern const AVCodecParser ff_mpeg4video_parser;
extern const AVCodecParser ff_mpegaudio_parser;
extern const AVCodecParser ff_mpegvideo_parser;
extern const AVCodecParser ff_opus_parser;
extern const AVCodecParser ff_png_parser;
extern const AVCodecParser ff_pnm_parser;
extern const AVCodecParser ff_qoi_parser;
extern const AVCodecParser ff_rv34_parser;
extern const AVCodecParser ff_sbc_parser;
extern const AVCodecParser ff_sipr_parser;
extern const AVCodecParser ff_tak_parser;
extern const AVCodecParser ff_vc1_parser;
extern const AVCodecParser ff_vorbis_parser;
extern const AVCodecParser ff_vp3_parser;
extern const AVCodecParser ff_vp8_parser;
extern const AVCodecParser ff_vp9_parser;
extern const AVCodecParser ff_vvc_parser;
extern const AVCodecParser ff_webp_parser;
extern const AVCodecParser ff_xbm_parser;
extern const AVCodecParser ff_xma_parser;
extern const AVCodecParser ff_xwd_parser;

#include "libavcodec/parser_list.c"

const AVCodecParser *av_parser_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVCodecParser *p = parser_list[i];

    if (p)
        *opaque = (void*)(i + 1);

    return p;
}
