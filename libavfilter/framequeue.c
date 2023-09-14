/*
 * Generic frame queue
 * Copyright (c) 2016 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "framequeue.h"

static inline FFFrameBucket *bucket(FFFrameQueue *fq, size_t idx)
{
    return &fq->queue[(fq->tail + idx) & (fq->allocated - 1)];
}

void ff_framequeue_global_init(FFFrameQueueGlobal *fqg)
{
}

static void check_consistency(FFFrameQueue *fq)
{
#if defined(ASSERT_LEVEL) && ASSERT_LEVEL >= 2
    uint64_t nb_samples = 0;
    size_t i;

    av_assert0(fq->queued == fq->total_frames_head - fq->total_frames_tail);
    for (i = 0; i < fq->queued; i++)
        nb_samples += bucket(fq, i)->frame->nb_samples;
    av_assert0(nb_samples == fq->total_samples_head - fq->total_samples_tail);
#endif
}

void ff_framequeue_init(FFFrameQueue *fq, FFFrameQueueGlobal *fqg)
{
    fq->queue = &fq->first_bucket;
    fq->allocated = 1;
}

void ff_framequeue_free(FFFrameQueue *fq)
{
    while (fq->queued) {
        AVFrame *frame = ff_framequeue_take(fq);
        av_frame_free(&frame);
    }
    if (fq->queue != &fq->first_bucket)
        av_freep(&fq->queue);
}

int ff_framequeue_add(FFFrameQueue *fq, AVFrame *frame)
{
    FFFrameBucket *b;

    check_consistency(fq);
    if (fq->queued == fq->allocated) {
        if (fq->allocated == 1) {
            size_t na = 8;
            FFFrameBucket *nq = av_realloc_array(NULL, na, sizeof(*nq));
            if (!nq)
                return AVERROR(ENOMEM);
            nq[0] = fq->queue[0];
            fq->queue = nq;
            fq->allocated = na;
        } else {
            size_t na = fq->allocated << 1;
            FFFrameBucket *nq = av_realloc_array(fq->queue, na, sizeof(*nq));
            if (!nq)
                return AVERROR(ENOMEM);
            if (fq->tail)
                memmove(nq + fq->allocated, nq, fq->tail * sizeof(*nq));
            fq->queue = nq;
            fq->allocated = na;
        }
    }
    b = bucket(fq, fq->queued);
    b->frame = frame;
    fq->queued++;
    fq->total_frames_head++;
    fq->total_samples_head += frame->nb_samples;
    check_consistency(fq);
    return 0;
}

AVFrame *ff_framequeue_take(FFFrameQueue *fq)
{
    FFFrameBucket *b;

    check_consistency(fq);
    av_assert1(fq->queued);
    b = bucket(fq, 0);
    fq->queued--;
    fq->tail++;
    fq->tail &= fq->allocated - 1;
    fq->total_frames_tail++;
    fq->total_samples_tail += b->frame->nb_samples;
    fq->samples_skipped = 0;
    check_consistency(fq);
    return b->frame;
}

AVFrame *ff_framequeue_peek(FFFrameQueue *fq, size_t idx)
{
    FFFrameBucket *b;

    check_consistency(fq);
    av_assert1(idx < fq->queued);
    b = bucket(fq, idx);
    check_consistency(fq);
    return b->frame;
}

void ff_framequeue_skip_samples(FFFrameQueue *fq, size_t samples, AVRational time_base)
{
    FFFrameBucket *b;
    size_t bytes;
    int planar, planes, i;

    check_consistency(fq);
    av_assert1(fq->queued);
    b = bucket(fq, 0);
    av_assert1(samples < b->frame->nb_samples);
    planar = av_sample_fmt_is_planar(b->frame->format);
    planes = planar ? b->frame->ch_layout.nb_channels : 1;
    bytes = samples * av_get_bytes_per_sample(b->frame->format);
    if (!planar)
        bytes *= b->frame->ch_layout.nb_channels;
    if (b->frame->pts != AV_NOPTS_VALUE)
        b->frame->pts += av_rescale_q(samples, av_make_q(1, b->frame->sample_rate), time_base);
    b->frame->nb_samples -= samples;
    b->frame->linesize[0] -= bytes;
    for (i = 0; i < planes; i++)
        b->frame->extended_data[i] += bytes;
    for (i = 0; i < planes && i < AV_NUM_DATA_POINTERS; i++)
        b->frame->data[i] = b->frame->extended_data[i];
    fq->total_samples_tail += samples;
    fq->samples_skipped = 1;
    ff_framequeue_update_peeked(fq, 0);
}
