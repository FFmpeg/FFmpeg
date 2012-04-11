/*
 * Audio Frame Queue
 * Copyright (c) 2012 Justin Ruggles
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

#include "audio_frame_queue.h"
#include "internal.h"
#include "libavutil/avassert.h"

void ff_af_queue_init(AVCodecContext *avctx, AudioFrameQueue *afq)
{
    afq->avctx = avctx;
    afq->remaining_delay   = avctx->delay;
    afq->remaining_samples = avctx->delay;
    afq->frame_count       = 0;
}

void ff_af_queue_close(AudioFrameQueue *afq)
{
    if(afq->frame_count)
        av_log(afq->avctx, AV_LOG_WARNING, "%d frames left in que on closing\n", afq->frame_count);
    av_freep(&afq->frames);
    memset(afq, 0, sizeof(*afq));
}

int ff_af_queue_add(AudioFrameQueue *afq, const AVFrame *f)
{
    AudioFrame *new = av_fast_realloc(afq->frames, &afq->frame_alloc, sizeof(*afq->frames)*(afq->frame_count+1));
    if(!new)
        return AVERROR(ENOMEM);
    afq->frames = new;
    new += afq->frame_count;

    /* get frame parameters */
    new->duration = f->nb_samples;
    new->duration += afq->remaining_delay;
    if (f->pts != AV_NOPTS_VALUE) {
        new->pts = av_rescale_q(f->pts,
                                      afq->avctx->time_base,
                                      (AVRational){ 1, afq->avctx->sample_rate });
        new->pts -= afq->remaining_delay;
        if(afq->frame_count && new[-1].pts >= new->pts)
            av_log(afq->avctx, AV_LOG_WARNING, "Que input is backward in time\n");
    } else {
        new->pts = AV_NOPTS_VALUE;
    }
    afq->remaining_delay = 0;

    /* add frame sample count */
    afq->remaining_samples += f->nb_samples;

    afq->frame_count++;

    return 0;
}

void ff_af_queue_remove(AudioFrameQueue *afq, int nb_samples, int64_t *pts,
                        int *duration)
{
    int64_t out_pts = AV_NOPTS_VALUE;
    int removed_samples = 0;
    int i;

    if (afq->frame_count || afq->frame_alloc) {
        if (afq->frames->pts != AV_NOPTS_VALUE)
            out_pts = afq->frames->pts;
    }
    if(!afq->frame_count)
        av_log(afq->avctx, AV_LOG_WARNING, "Trying to remove %d samples, but que empty\n", nb_samples);
    if (pts)
        *pts = ff_samples_to_time_base(afq->avctx, out_pts);

    for(i=0; nb_samples && i<afq->frame_count; i++){
        int n= FFMIN(afq->frames[i].duration, nb_samples);
        afq->frames[i].duration -= n;
        nb_samples              -= n;
        removed_samples         += n;
        if(afq->frames[i].pts != AV_NOPTS_VALUE)
            afq->frames[i].pts      += n;
    }
    i -= i && afq->frames[i-1].duration;
    memmove(afq->frames, afq->frames + i, sizeof(*afq->frames) * (afq->frame_count - i));
    afq->frame_count -= i;

    if(nb_samples){
        av_assert0(!afq->frame_count);
        if(afq->frames[0].pts != AV_NOPTS_VALUE)
            afq->frames[0].pts += nb_samples;
        av_log(afq->avctx, AV_LOG_DEBUG, "Trying to remove %d more samples than are in the que\n", nb_samples);
    }
    if (duration)
        *duration = ff_samples_to_time_base(afq->avctx, removed_samples);
}

