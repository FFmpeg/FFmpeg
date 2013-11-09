/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * FF Video Codec 1 (a lossless codec)
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "get_bits.h"
#include "put_bits.h"
#include "rangecoder.h"
#include "golomb.h"
#include "mathops.h"
#include "ffv1.h"

const int8_t ffv1_quant5_10bit[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -0, -0, -0, -0, -0, -0, -0, -0, -0, -0,
};

const int8_t ffv1_quant5[256] = {
     0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1,
};

const int8_t ffv1_quant9_10bit[256] = {
     0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -3, -3, -3,
    -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
    -3, -3, -3, -3, -3, -3, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -1, -1, -1, -1, -1, -1, -1, -1, -0, -0, -0, -0,
};

const int8_t ffv1_quant11[256] = {
     0,  1,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -3, -3, -3, -3, -3, -3, -3, -2, -2, -2, -1,
};

const uint8_t ffv1_ver2_state[256] = {
      0,  10,  10,  10,  10,  16,  16,  16,  28,  16,  16,  29,  42,  49,  20,  49,
     59,  25,  26,  26,  27,  31,  33,  33,  33,  34,  34,  37,  67,  38,  39,  39,
     40,  40,  41,  79,  43,  44,  45,  45,  48,  48,  64,  50,  51,  52,  88,  52,
     53,  74,  55,  57,  58,  58,  74,  60,  101, 61,  62,  84,  66,  66,  68,  69,
     87,  82,  71,  97,  73,  73,  82,  75,  111, 77,  94,  78,  87,  81,  83,  97,
     85,  83,  94,  86,  99,  89,  90,  99,  111, 92,  93,  134, 95,  98,  105, 98,
    105, 110, 102, 108, 102, 118, 103, 106, 106, 113, 109, 112, 114, 112, 116, 125,
    115, 116, 117, 117, 126, 119, 125, 121, 121, 123, 145, 124, 126, 131, 127, 129,
    165, 130, 132, 138, 133, 135, 145, 136, 137, 139, 146, 141, 143, 142, 144, 148,
    147, 155, 151, 149, 151, 150, 152, 157, 153, 154, 156, 168, 158, 162, 161, 160,
    172, 163, 169, 164, 166, 184, 167, 170, 177, 174, 171, 173, 182, 176, 180, 178,
    175, 189, 179, 181, 186, 183, 192, 185, 200, 187, 191, 188, 190, 197, 193, 196,
    197, 194, 195, 196, 198, 202, 199, 201, 210, 203, 207, 204, 205, 206, 208, 214,
    209, 211, 221, 212, 213, 215, 224, 216, 217, 218, 219, 220, 222, 228, 223, 225,
    226, 224, 227, 229, 240, 230, 231, 232, 233, 234, 235, 236, 238, 239, 237, 242,
    241, 243, 242, 244, 245, 246, 247, 248, 249, 250, 251, 252, 252, 253, 254, 255,
};


av_cold int ffv1_common_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;

    s->avctx = avctx;
    s->flags = avctx->flags;

    if (!avctx->width || !avctx->height)
        return AVERROR_INVALIDDATA;

    ff_dsputil_init(&s->dsp, avctx);

    s->width  = avctx->width;
    s->height = avctx->height;

    // defaults
    s->num_h_slices = 1;
    s->num_v_slices = 1;

    return 0;
}

int ffv1_init_slice_state(FFV1Context *f, FFV1Context *fs)
{
    int j;

    fs->plane_count  = f->plane_count;
    fs->transparency = f->transparency;
    for (j = 0; j < f->plane_count; j++) {
        PlaneContext *const p = &fs->plane[j];

        if (fs->ac) {
            if (!p->state)
                p->state = av_malloc(CONTEXT_SIZE * p->context_count *
                                     sizeof(uint8_t));
            if (!p->state)
                return AVERROR(ENOMEM);
        } else {
            if (!p->vlc_state)
                p->vlc_state = av_malloc(p->context_count * sizeof(VlcState));
            if (!p->vlc_state)
                return AVERROR(ENOMEM);
        }
    }

    if (fs->ac > 1) {
        //FIXME only redo if state_transition changed
        for (j = 1; j < 256; j++) {
            fs->c.one_state[j]        = f->state_transition[j];
            fs->c.zero_state[256 - j] = 256 - fs->c.one_state[j];
        }
    }

    return 0;
}

av_cold int ffv1_init_slice_contexts(FFV1Context *f)
{
    int i;

    f->slice_count = f->num_h_slices * f->num_v_slices;
    if (f->slice_count <= 0) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid number of slices\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < f->slice_count; i++) {
        FFV1Context *fs = av_mallocz(sizeof(*fs));
        int sx          = i % f->num_h_slices;
        int sy          = i / f->num_h_slices;
        int sxs         = f->avctx->width  *  sx      / f->num_h_slices;
        int sxe         = f->avctx->width  * (sx + 1) / f->num_h_slices;
        int sys         = f->avctx->height *  sy      / f->num_v_slices;
        int sye         = f->avctx->height * (sy + 1) / f->num_v_slices;
        f->slice_context[i] = fs;
        memcpy(fs, f, sizeof(*fs));
        memset(fs->rc_stat2, 0, sizeof(fs->rc_stat2));

        fs->slice_width  = sxe - sxs;
        fs->slice_height = sye - sys;
        fs->slice_x      = sxs;
        fs->slice_y      = sys;

        fs->sample_buffer = av_malloc(3 * MAX_PLANES * (fs->width + 6) *
                                      sizeof(*fs->sample_buffer));
        if (!fs->sample_buffer)
            return AVERROR(ENOMEM);
    }
    return 0;
}

int ffv1_allocate_initial_states(FFV1Context *f)
{
    int i;

    for (i = 0; i < f->quant_table_count; i++) {
        f->initial_states[i] = av_malloc(f->context_count[i] *
                                         sizeof(*f->initial_states[i]));
        if (!f->initial_states[i])
            return AVERROR(ENOMEM);
        memset(f->initial_states[i], 128,
               f->context_count[i] * sizeof(*f->initial_states[i]));
    }
    return 0;
}

void ffv1_clear_slice_state(FFV1Context *f, FFV1Context *fs)
{
    int i, j;

    for (i = 0; i < f->plane_count; i++) {
        PlaneContext *p = &fs->plane[i];

        p->interlace_bit_state[0] = 128;
        p->interlace_bit_state[1] = 128;

        if (fs->ac) {
            if (f->initial_states[p->quant_table_index]) {
                memcpy(p->state, f->initial_states[p->quant_table_index],
                       CONTEXT_SIZE * p->context_count);
            } else
                memset(p->state, 128, CONTEXT_SIZE * p->context_count);
        } else {
            for (j = 0; j < p->context_count; j++) {
                p->vlc_state[j].drift     = 0;
                p->vlc_state[j].error_sum = 4;    //FFMAX((RANGE + 32)/64, 2);
                p->vlc_state[j].bias      = 0;
                p->vlc_state[j].count     = 1;
            }
        }
    }
}

av_cold int ffv1_close(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i, j;

    for (j = 0; j < s->slice_count; j++) {
        FFV1Context *fs = s->slice_context[j];
        for (i = 0; i < s->plane_count; i++) {
            PlaneContext *p = &fs->plane[i];

            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        av_freep(&fs->sample_buffer);
    }

    av_freep(&avctx->stats_out);
    for (j = 0; j < s->quant_table_count; j++) {
        av_freep(&s->initial_states[j]);
        for (i = 0; i < s->slice_count; i++) {
            FFV1Context *sf = s->slice_context[i];
            av_freep(&sf->rc_stat2[j]);
        }
        av_freep(&s->rc_stat2[j]);
    }

    for (i = 0; i < s->slice_count; i++)
        av_freep(&s->slice_context[i]);

    return 0;
}
