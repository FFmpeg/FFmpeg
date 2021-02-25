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

#include "libavutil/thread.h"

#include "avcodec.h"
#include "version.h"

extern AVCodecParser ff_aac_parser;
extern AVCodecParser ff_aac_latm_parser;
extern AVCodecParser ff_ac3_parser;
extern AVCodecParser ff_adx_parser;
extern AVCodecParser ff_av1_parser;
extern AVCodecParser ff_avs2_parser;
extern AVCodecParser ff_avs3_parser;
extern AVCodecParser ff_bmp_parser;
extern AVCodecParser ff_cavsvideo_parser;
extern AVCodecParser ff_cook_parser;
extern AVCodecParser ff_cri_parser;
extern AVCodecParser ff_dca_parser;
extern AVCodecParser ff_dirac_parser;
extern AVCodecParser ff_dnxhd_parser;
extern AVCodecParser ff_dolby_e_parser;
extern AVCodecParser ff_dpx_parser;
extern AVCodecParser ff_dvaudio_parser;
extern AVCodecParser ff_dvbsub_parser;
extern AVCodecParser ff_dvdsub_parser;
extern AVCodecParser ff_dvd_nav_parser;
extern AVCodecParser ff_flac_parser;
extern AVCodecParser ff_g723_1_parser;
extern AVCodecParser ff_g729_parser;
extern AVCodecParser ff_gif_parser;
extern AVCodecParser ff_gsm_parser;
extern AVCodecParser ff_h261_parser;
extern AVCodecParser ff_h263_parser;
extern AVCodecParser ff_h264_parser;
extern AVCodecParser ff_hevc_parser;
extern AVCodecParser ff_ipu_parser;
extern AVCodecParser ff_jpeg2000_parser;
extern AVCodecParser ff_mjpeg_parser;
extern AVCodecParser ff_mlp_parser;
extern AVCodecParser ff_mpeg4video_parser;
extern AVCodecParser ff_mpegaudio_parser;
extern AVCodecParser ff_mpegvideo_parser;
extern AVCodecParser ff_opus_parser;
extern AVCodecParser ff_png_parser;
extern AVCodecParser ff_pnm_parser;
extern AVCodecParser ff_rv30_parser;
extern AVCodecParser ff_rv40_parser;
extern AVCodecParser ff_sbc_parser;
extern AVCodecParser ff_sipr_parser;
extern AVCodecParser ff_tak_parser;
extern AVCodecParser ff_vc1_parser;
extern AVCodecParser ff_vorbis_parser;
extern AVCodecParser ff_vp3_parser;
extern AVCodecParser ff_vp8_parser;
extern AVCodecParser ff_vp9_parser;
extern AVCodecParser ff_webp_parser;
extern AVCodecParser ff_xbm_parser;
extern AVCodecParser ff_xma_parser;

#include "libavcodec/parser_list.c"

#if FF_API_NEXT
FF_DISABLE_DEPRECATION_WARNINGS
static AVOnce av_parser_next_init = AV_ONCE_INIT;

static void av_parser_init_next(void)
{
    AVCodecParser *prev = NULL, *p;
    int i = 0;
    while ((p = (AVCodecParser*)parser_list[i++])) {
        if (prev)
            prev->next = p;
        prev = p;
    }
}

AVCodecParser *av_parser_next(const AVCodecParser *p)
{
    ff_thread_once(&av_parser_next_init, av_parser_init_next);

    if (p)
        return p->next;
    else
        return (AVCodecParser*)parser_list[0];
}

void av_register_codec_parser(AVCodecParser *parser)
{
    ff_thread_once(&av_parser_next_init, av_parser_init_next);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

const AVCodecParser *av_parser_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVCodecParser *p = parser_list[i];

    if (p)
        *opaque = (void*)(i + 1);

    return p;
}
