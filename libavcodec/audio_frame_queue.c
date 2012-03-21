/*
 * Audio Frame Queue
 * Copyright (c) 2012 Justin Ruggles
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

#include "libavutil/mathematics.h"
#include "internal.h"
#include "audio_frame_queue.h"

void ff_af_queue_init(AVCodecContext *avctx, AudioFrameQueue *afq)
{
    afq->avctx             = avctx;
    afq->next_pts          = AV_NOPTS_VALUE;
    afq->remaining_delay   = avctx->delay;
    afq->remaining_samples = avctx->delay;
    afq->frame_queue       = NULL;
}

static void delete_next_frame(AudioFrameQueue *afq)
{
    AudioFrame *f = afq->frame_queue;
    if (f) {
        afq->frame_queue = f->next;
        f->next = NULL;
        av_freep(&f);
    }
}

void ff_af_queue_close(AudioFrameQueue *afq)
{
    /* remove/free any remaining frames */
    while (afq->frame_queue)
        delete_next_frame(afq);
    memset(afq, 0, sizeof(*afq));
}

int ff_af_queue_add(AudioFrameQueue *afq, const AVFrame *f)
{
    AudioFrame *new_frame;
    AudioFrame *queue_end = afq->frame_queue;

    /* find the end of the queue */
    while (queue_end && queue_end->next)
        queue_end = queue_end->next;

    /* allocate new frame queue entry */
    if (!(new_frame = av_malloc(sizeof(*new_frame))))
        return AVERROR(ENOMEM);

    /* get frame parameters */
    new_frame->next = NULL;
    new_frame->duration = f->nb_samples;
    if (f->pts != AV_NOPTS_VALUE) {
        new_frame->pts = av_rescale_q(f->pts,
                                      afq->avctx->time_base,
                                      (AVRational){ 1, afq->avctx->sample_rate });
        afq->next_pts = new_frame->pts + new_frame->duration;
    } else {
        new_frame->pts = AV_NOPTS_VALUE;
        afq->next_pts  = AV_NOPTS_VALUE;
    }

    /* add new frame to the end of the queue */
    if (!queue_end)
        afq->frame_queue = new_frame;
    else
        queue_end->next = new_frame;

    /* add frame sample count */
    afq->remaining_samples += f->nb_samples;

#ifdef DEBUG
    ff_af_queue_log_state(afq);
#endif

    return 0;
}

void ff_af_queue_remove(AudioFrameQueue *afq, int nb_samples, int64_t *pts,
                        int *duration)
{
    int64_t out_pts = AV_NOPTS_VALUE;
    int removed_samples = 0;

#ifdef DEBUG
    ff_af_queue_log_state(afq);
#endif

    /* get output pts from the next frame or generated pts */
    if (afq->frame_queue) {
        if (afq->frame_queue->pts != AV_NOPTS_VALUE)
            out_pts = afq->frame_queue->pts - afq->remaining_delay;
    } else {
        if (afq->next_pts != AV_NOPTS_VALUE)
            out_pts = afq->next_pts - afq->remaining_delay;
    }
    if (pts) {
        if (out_pts != AV_NOPTS_VALUE)
            *pts = ff_samples_to_time_base(afq->avctx, out_pts);
        else
            *pts = AV_NOPTS_VALUE;
    }

    /* if the delay is larger than the packet duration, we use up delay samples
       for the output packet and leave all frames in the queue */
    if (afq->remaining_delay >= nb_samples) {
        removed_samples      += nb_samples;
        afq->remaining_delay -= nb_samples;
    }
    /* remove frames from the queue until we have enough to cover the
       requested number of samples or until the queue is empty */
    while (removed_samples < nb_samples && afq->frame_queue) {
        removed_samples += afq->frame_queue->duration;
        delete_next_frame(afq);
    }
    afq->remaining_samples -= removed_samples;

    /* if there are no frames left and we have room for more samples, use
       any remaining delay samples */
    if (removed_samples < nb_samples && afq->remaining_samples > 0) {
        int add_samples = FFMIN(afq->remaining_samples,
                                nb_samples - removed_samples);
        removed_samples        += add_samples;
        afq->remaining_samples -= add_samples;
    }
    if (removed_samples > nb_samples)
        av_log(afq->avctx, AV_LOG_WARNING, "frame_size is too large\n");
    if (duration)
        *duration = ff_samples_to_time_base(afq->avctx, removed_samples);
}

void ff_af_queue_log_state(AudioFrameQueue *afq)
{
    AudioFrame *f;
    av_log(afq->avctx, AV_LOG_DEBUG, "remaining delay   = %d\n",
           afq->remaining_delay);
    av_log(afq->avctx, AV_LOG_DEBUG, "remaining samples = %d\n",
           afq->remaining_samples);
    av_log(afq->avctx, AV_LOG_DEBUG, "frames:\n");
    f = afq->frame_queue;
    while (f) {
        av_log(afq->avctx, AV_LOG_DEBUG, "  [ pts=%9"PRId64" duration=%d ]\n",
               f->pts, f->duration);
        f = f->next;
    }
}
