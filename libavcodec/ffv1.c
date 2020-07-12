/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003-2013 Michael Niedermayer <michaelni@gmx.at>
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
 * FF Video Codec 1 (a lossless codec)
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "internal.h"
#include "rangecoder.h"
#include "mathops.h"
#include "ffv1.h"

av_cold int ff_ffv1_common_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;

    if (!avctx->width || !avctx->height)
        return AVERROR_INVALIDDATA;

    s->avctx = avctx;
    s->flags = avctx->flags;

    s->picture.f = av_frame_alloc();
    s->last_picture.f = av_frame_alloc();
    if (!s->picture.f || !s->last_picture.f)
        return AVERROR(ENOMEM);

    s->width  = avctx->width;
    s->height = avctx->height;

    // defaults
    s->num_h_slices = 1;
    s->num_v_slices = 1;

    return 0;
}

av_cold int ff_ffv1_init_slice_state(FFV1Context *f, FFV1Context *fs)
{
    int j, i;

    fs->plane_count  = f->plane_count;
    fs->transparency = f->transparency;
    for (j = 0; j < f->plane_count; j++) {
        PlaneContext *const p = &fs->plane[j];

        if (fs->ac != AC_GOLOMB_RICE) {
            if (!p->state)
                p->state = av_malloc_array(p->context_count, CONTEXT_SIZE *
                                     sizeof(uint8_t));
            if (!p->state)
                return AVERROR(ENOMEM);
        } else {
            if (!p->vlc_state) {
                p->vlc_state = av_mallocz_array(p->context_count, sizeof(VlcState));
                if (!p->vlc_state)
                    return AVERROR(ENOMEM);
                for (i = 0; i < p->context_count; i++) {
                    p->vlc_state[i].error_sum = 4;
                    p->vlc_state[i].count     = 1;
                }
            }
        }
    }

    if (fs->ac == AC_RANGE_CUSTOM_TAB) {
        //FIXME only redo if state_transition changed
        for (j = 1; j < 256; j++) {
            fs->c. one_state[      j] = f->state_transition[j];
            fs->c.zero_state[256 - j] = 256 - fs->c.one_state[j];
        }
    }

    return 0;
}

av_cold int ff_ffv1_init_slices_state(FFV1Context *f)
{
    int i, ret;
    for (i = 0; i < f->max_slice_count; i++) {
        FFV1Context *fs = f->slice_context[i];
        if ((ret = ff_ffv1_init_slice_state(f, fs)) < 0)
            return AVERROR(ENOMEM);
    }
    return 0;
}

av_cold int ff_ffv1_init_slice_contexts(FFV1Context *f)
{
    int i;

    f->max_slice_count = f->num_h_slices * f->num_v_slices;
    av_assert0(f->max_slice_count > 0);

    for (i = 0; i < f->max_slice_count; i++) {
        int sx          = i % f->num_h_slices;
        int sy          = i / f->num_h_slices;
        int sxs         = f->avctx->width  *  sx      / f->num_h_slices;
        int sxe         = f->avctx->width  * (sx + 1) / f->num_h_slices;
        int sys         = f->avctx->height *  sy      / f->num_v_slices;
        int sye         = f->avctx->height * (sy + 1) / f->num_v_slices;
        FFV1Context *fs = av_mallocz(sizeof(*fs));

        if (!fs)
            goto memfail;

        f->slice_context[i] = fs;
        memcpy(fs, f, sizeof(*fs));
        memset(fs->rc_stat2, 0, sizeof(fs->rc_stat2));

        fs->slice_width  = sxe - sxs;
        fs->slice_height = sye - sys;
        fs->slice_x      = sxs;
        fs->slice_y      = sys;

        fs->sample_buffer = av_malloc_array((fs->width + 6), 3 * MAX_PLANES *
                                      sizeof(*fs->sample_buffer));
        fs->sample_buffer32 = av_malloc_array((fs->width + 6), 3 * MAX_PLANES *
                                        sizeof(*fs->sample_buffer32));
        if (!fs->sample_buffer || !fs->sample_buffer32) {
            av_freep(&fs->sample_buffer);
            av_freep(&fs->sample_buffer32);
            av_freep(&f->slice_context[i]);
            goto memfail;
        }
    }
    return 0;

memfail:
    while(--i >= 0) {
        av_freep(&f->slice_context[i]->sample_buffer);
        av_freep(&f->slice_context[i]->sample_buffer32);
        av_freep(&f->slice_context[i]);
    }
    return AVERROR(ENOMEM);
}

int ff_ffv1_allocate_initial_states(FFV1Context *f)
{
    int i;

    for (i = 0; i < f->quant_table_count; i++) {
        f->initial_states[i] = av_malloc_array(f->context_count[i],
                                         sizeof(*f->initial_states[i]));
        if (!f->initial_states[i])
            return AVERROR(ENOMEM);
        memset(f->initial_states[i], 128,
               f->context_count[i] * sizeof(*f->initial_states[i]));
    }
    return 0;
}

void ff_ffv1_clear_slice_state(FFV1Context *f, FFV1Context *fs)
{
    int i, j;

    for (i = 0; i < f->plane_count; i++) {
        PlaneContext *p = &fs->plane[i];

        p->interlace_bit_state[0] = 128;
        p->interlace_bit_state[1] = 128;

        if (fs->ac != AC_GOLOMB_RICE) {
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


av_cold int ff_ffv1_close(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i, j;

    if (s->picture.f)
        ff_thread_release_buffer(avctx, &s->picture);
    av_frame_free(&s->picture.f);

    if (s->last_picture.f)
        ff_thread_release_buffer(avctx, &s->last_picture);
    av_frame_free(&s->last_picture.f);

    for (j = 0; j < s->max_slice_count; j++) {
        FFV1Context *fs = s->slice_context[j];
        for (i = 0; i < s->plane_count; i++) {
            PlaneContext *p = &fs->plane[i];

            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        av_freep(&fs->sample_buffer);
        av_freep(&fs->sample_buffer32);
    }

    av_freep(&avctx->stats_out);
    for (j = 0; j < s->quant_table_count; j++) {
        av_freep(&s->initial_states[j]);
        for (i = 0; i < s->max_slice_count; i++) {
            FFV1Context *sf = s->slice_context[i];
            av_freep(&sf->rc_stat2[j]);
        }
        av_freep(&s->rc_stat2[j]);
    }

    for (i = 0; i < s->max_slice_count; i++)
        av_freep(&s->slice_context[i]);

    return 0;
}
