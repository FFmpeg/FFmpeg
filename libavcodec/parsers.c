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
#include "parser_internal.h"

#if FF_API_PARSER_PRIVATE
#include "libavutil/internal.h"
#include <assert.h>
#include <stddef.h>

FF_DISABLE_DEPRECATION_WARNINGS
#define CHECK_OFFSET(field, public_prefix) static_assert(offsetof(FFCodecParser, field) == offsetof(FFCodecParser, p.public_prefix ## field), "Wrong offsets")
CHECK_OFFSET(codec_ids,);
CHECK_OFFSET(priv_data_size,);
CHECK_OFFSET(init, parser_);
CHECK_OFFSET(parse, parser_);
CHECK_OFFSET(close, parser_);
CHECK_OFFSET(split,);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

extern const FFCodecParser ff_aac_parser;
extern const FFCodecParser ff_aac_latm_parser;
extern const FFCodecParser ff_ac3_parser;
extern const FFCodecParser ff_adx_parser;
extern const FFCodecParser ff_ahx_parser;
extern const FFCodecParser ff_amr_parser;
extern const FFCodecParser ff_apv_parser;
extern const FFCodecParser ff_av1_parser;
extern const FFCodecParser ff_avs2_parser;
extern const FFCodecParser ff_avs3_parser;
extern const FFCodecParser ff_bmp_parser;
extern const FFCodecParser ff_cavsvideo_parser;
extern const FFCodecParser ff_cook_parser;
extern const FFCodecParser ff_cri_parser;
extern const FFCodecParser ff_dca_parser;
extern const FFCodecParser ff_dirac_parser;
extern const FFCodecParser ff_dnxhd_parser;
extern const FFCodecParser ff_dnxuc_parser;
extern const FFCodecParser ff_dolby_e_parser;
extern const FFCodecParser ff_dpx_parser;
extern const FFCodecParser ff_dvaudio_parser;
extern const FFCodecParser ff_dvbsub_parser;
extern const FFCodecParser ff_dvdsub_parser;
extern const FFCodecParser ff_dvd_nav_parser;
extern const FFCodecParser ff_evc_parser;
extern const FFCodecParser ff_flac_parser;
extern const FFCodecParser ff_ftr_parser;
extern const FFCodecParser ff_ffv1_parser;
extern const FFCodecParser ff_g723_1_parser;
extern const FFCodecParser ff_g729_parser;
extern const FFCodecParser ff_gif_parser;
extern const FFCodecParser ff_gsm_parser;
extern const FFCodecParser ff_h261_parser;
extern const FFCodecParser ff_h263_parser;
extern const FFCodecParser ff_h264_parser;
extern const FFCodecParser ff_hevc_parser;
extern const FFCodecParser ff_hdr_parser;
extern const FFCodecParser ff_ipu_parser;
extern const FFCodecParser ff_jpeg2000_parser;
extern const FFCodecParser ff_jpegxl_parser;
extern const FFCodecParser ff_misc4_parser;
extern const FFCodecParser ff_mjpeg_parser;
extern const FFCodecParser ff_mlp_parser;
extern const FFCodecParser ff_mpeg4video_parser;
extern const FFCodecParser ff_mpegaudio_parser;
extern const FFCodecParser ff_mpegvideo_parser;
extern const FFCodecParser ff_opus_parser;
extern const FFCodecParser ff_prores_parser;
extern const FFCodecParser ff_png_parser;
extern const FFCodecParser ff_pnm_parser;
extern const FFCodecParser ff_prores_raw_parser;
extern const FFCodecParser ff_qoi_parser;
extern const FFCodecParser ff_rv34_parser;
extern const FFCodecParser ff_sbc_parser;
extern const FFCodecParser ff_sipr_parser;
extern const FFCodecParser ff_tak_parser;
extern const FFCodecParser ff_vc1_parser;
extern const FFCodecParser ff_vorbis_parser;
extern const FFCodecParser ff_vp3_parser;
extern const FFCodecParser ff_vp8_parser;
extern const FFCodecParser ff_vp9_parser;
extern const FFCodecParser ff_vvc_parser;
extern const FFCodecParser ff_webp_parser;
extern const FFCodecParser ff_xbm_parser;
extern const FFCodecParser ff_xma_parser;
extern const FFCodecParser ff_xwd_parser;

#include "libavcodec/parser_list.c"

const AVCodecParser *av_parser_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const FFCodecParser *p = parser_list[i];

    if (p) {
        *opaque = (void*)(i + 1);
        return &p->p;
    }

    return NULL;
}
