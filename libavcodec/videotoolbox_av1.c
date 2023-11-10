/*
 * Videotoolbox hardware acceleration for AV1
 * Copyright (c) 2023 Jan Ekström
 * Copyright (c) 2024 Ruslan Chernenko
 * Copyright (c) 2024 Martin Storsjö
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

#include "libavutil/mem.h"

#include "av1dec.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "vt_internal.h"

CFDataRef ff_videotoolbox_av1c_extradata_create(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    uint8_t *buf;
    CFDataRef data;
    if (!s->raw_seq)
        return NULL;

    buf = av_malloc(s->seq_data_ref->size + 4);
    if (!buf)
        return NULL;
    buf[0] = 0x81; // version and marker (constant)
    buf[1] = s->raw_seq->seq_profile << 5 | s->raw_seq->seq_level_idx[0];
    buf[2] = s->raw_seq->seq_tier[0]                << 7 |
             s->raw_seq->color_config.high_bitdepth << 6 |
             s->raw_seq->color_config.twelve_bit    << 5 |
             s->raw_seq->color_config.mono_chrome   << 4 |
             s->raw_seq->color_config.subsampling_x << 3 |
             s->raw_seq->color_config.subsampling_y << 2 |
             s->raw_seq->color_config.chroma_sample_position;

    if (s->raw_seq->initial_display_delay_present_flag)
        buf[3] = 0 << 5 |
                 s->raw_seq->initial_display_delay_present_flag << 4 |
                 s->raw_seq->initial_display_delay_minus_1[0];
    else
        buf[3] = 0x00;
    memcpy(buf + 4, s->seq_data_ref->data, s->seq_data_ref->size);
    data = CFDataCreate(kCFAllocatorDefault, buf, s->seq_data_ref->size + 4);
    av_free(buf);
    return data;
};


static int videotoolbox_av1_start_frame(AVCodecContext *avctx,
                                        const uint8_t *buffer,
                                        uint32_t size)
{
    return 0;
}

static int videotoolbox_av1_decode_slice(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    return 0;
}

static int videotoolbox_av1_end_frame(AVCodecContext *avctx)
{
    const AV1DecContext *s = avctx->priv_data;
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    AVFrame *frame = s->cur_frame.f;

    vtctx->bitstream_size = 0;
    for (int i = s->start_unit; i < s->nb_unit; i++)
        ff_videotoolbox_buffer_append(vtctx, s->current_obu.units[i].data,
                                      s->current_obu.units[i].data_size);
    return ff_videotoolbox_common_end_frame(avctx, frame);
}

const FFHWAccel ff_av1_videotoolbox_hwaccel = {
    .p.name         = "av1_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_av1_start_frame,
    .decode_slice   = videotoolbox_av1_decode_slice,
    .end_frame      = videotoolbox_av1_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};
