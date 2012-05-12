/*
 * Copyright (c) 2011 Nicolas George <nicolas.george@normalesup.org>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Stream (de)synchronization filter
 */

#include "libavutil/eval.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

#define QUEUE_SIZE 16

static const char * const var_names[] = {
    "b1", "b2",
    "s1", "s2",
    "t1", "t2",
    NULL
};

enum var_name {
    VAR_B1, VAR_B2,
    VAR_S1, VAR_S2,
    VAR_T1, VAR_T2,
    VAR_NB
};

typedef struct {
    AVExpr *expr;
    double var_values[VAR_NB];
    struct buf_queue {
        AVFilterBufferRef *buf[QUEUE_SIZE];
        unsigned tail, nb;
        /* buf[tail] is the oldest,
           buf[(tail + nb) % QUEUE_SIZE] is where the next is added */
    } queue[2];
    int req[2];
    int next_out;
    int eof; /* bitmask, one bit for each stream */
} AStreamSyncContext;

static const char *default_expr = "t1-t2";

static av_cold int init(AVFilterContext *ctx, const char *args0, void *opaque)
{
    AStreamSyncContext *as = ctx->priv;
    const char *expr = args0 ? args0 : default_expr;
    int r, i;

    r = av_expr_parse(&as->expr, expr, var_names,
                      NULL, NULL, NULL, NULL, 0, ctx);
    if (r < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error in expression \"%s\"\n", expr);
        return r;
    }
    for (i = 0; i < 42; i++)
        av_expr_eval(as->expr, as->var_values, NULL); /* exercize prng */
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    int i;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;

    for (i = 0; i < 2; i++) {
        formats = ctx->inputs[i]->in_formats;
        avfilter_formats_ref(formats, &ctx->inputs[i]->out_formats);
        avfilter_formats_ref(formats, &ctx->outputs[i]->in_formats);
        layouts = ctx->inputs[i]->in_channel_layouts;
        ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts);
        ff_channel_layouts_ref(layouts, &ctx->outputs[i]->in_channel_layouts);
    }
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int id = outlink == ctx->outputs[1];

    outlink->sample_rate = ctx->inputs[id]->sample_rate;
    outlink->time_base   = ctx->inputs[id]->time_base;
    return 0;
}

static void send_out(AVFilterContext *ctx, int out_id)
{
    AStreamSyncContext *as = ctx->priv;
    struct buf_queue *queue = &as->queue[out_id];
    AVFilterBufferRef *buf = queue->buf[queue->tail];

    queue->buf[queue->tail] = NULL;
    as->var_values[VAR_B1 + out_id]++;
    as->var_values[VAR_S1 + out_id] += buf->audio->nb_samples;
    if (buf->pts != AV_NOPTS_VALUE)
        as->var_values[VAR_T1 + out_id] =
            av_q2d(ctx->outputs[out_id]->time_base) * buf->pts;
    as->var_values[VAR_T1 + out_id] += buf->audio->nb_samples /
                                   (double)ctx->inputs[out_id]->sample_rate;
    ff_filter_samples(ctx->outputs[out_id], buf);
    queue->nb--;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    if (as->req[out_id])
        as->req[out_id]--;
}

static void send_next(AVFilterContext *ctx)
{
    AStreamSyncContext *as = ctx->priv;
    int i;

    while (1) {
        if (!as->queue[as->next_out].nb)
            break;
        send_out(ctx, as->next_out);
        if (!as->eof)
            as->next_out = av_expr_eval(as->expr, as->var_values, NULL) >= 0;
    }
    for (i = 0; i < 2; i++)
        if (as->queue[i].nb == QUEUE_SIZE)
            send_out(ctx, i);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AStreamSyncContext *as = ctx->priv;
    int id = outlink == ctx->outputs[1];

    as->req[id]++;
    while (as->req[id] && !(as->eof & (1 << id))) {
        if (as->queue[as->next_out].nb) {
            send_next(ctx);
        } else {
            as->eof |= 1 << as->next_out;
            avfilter_request_frame(ctx->inputs[as->next_out]);
            if (as->eof & (1 << as->next_out))
                as->next_out = !as->next_out;
        }
    }
    return 0;
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AStreamSyncContext *as = ctx->priv;
    int id = inlink == ctx->inputs[1];

    as->queue[id].buf[(as->queue[id].tail + as->queue[id].nb++) % QUEUE_SIZE] =
        insamples;
    as->eof &= ~(1 << id);
    send_next(ctx);
}

AVFilter avfilter_af_astreamsync = {
    .name          = "astreamsync",
    .description   = NULL_IF_CONFIG_SMALL("Copy two streams of audio data "
                                          "in a configurable order."),
    .priv_size     = sizeof(AStreamSyncContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "in1",
          .type             = AVMEDIA_TYPE_AUDIO,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name             = "in2",
          .type             = AVMEDIA_TYPE_AUDIO,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
        { .name             = "out1",
          .type             = AVMEDIA_TYPE_AUDIO,
          .config_props     = config_output,
          .request_frame    = request_frame, },
        { .name             = "out2",
          .type             = AVMEDIA_TYPE_AUDIO,
          .config_props     = config_output,
          .request_frame    = request_frame, },
        { .name = NULL }
    },
};
