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
 * GNU Lesser General Public License for more details.
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
#include "libavutil/opt.h"
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
    const AVClass *class;
    AVExpr *expr;
    char *expr_str;
    double var_values[VAR_NB];
    struct buf_queue {
        AVFrame *buf[QUEUE_SIZE];
        unsigned tail, nb;
        /* buf[tail] is the oldest,
           buf[(tail + nb) % QUEUE_SIZE] is where the next is added */
    } queue[2];
    int req[2];
    int next_out;
    int eof; /* bitmask, one bit for each stream */
} AStreamSyncContext;

#define OFFSET(x) offsetof(AStreamSyncContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption astreamsync_options[] = {
    { "expr", "set stream selection expression", OFFSET(expr_str), AV_OPT_TYPE_STRING, { .str = "t1-t2" }, .flags = FLAGS },
    { "e",    "set stream selection expression", OFFSET(expr_str), AV_OPT_TYPE_STRING, { .str = "t1-t2" }, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(astreamsync);

static av_cold int init(AVFilterContext *ctx)
{
    AStreamSyncContext *as = ctx->priv;
    int r, i;

    r = av_expr_parse(&as->expr, as->expr_str, var_names,
                      NULL, NULL, NULL, NULL, 0, ctx);
    if (r < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error in expression \"%s\"\n", as->expr_str);
        return r;
    }
    for (i = 0; i < 42; i++)
        av_expr_eval(as->expr, as->var_values, NULL); /* exercize prng */
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    int i;
    AVFilterFormats *formats, *rates;
    AVFilterChannelLayouts *layouts;

    for (i = 0; i < 2; i++) {
        formats = ctx->inputs[i]->in_formats;
        ff_formats_ref(formats, &ctx->inputs[i]->out_formats);
        ff_formats_ref(formats, &ctx->outputs[i]->in_formats);
        rates = ff_all_samplerates();
        ff_formats_ref(rates, &ctx->inputs[i]->out_samplerates);
        ff_formats_ref(rates, &ctx->outputs[i]->in_samplerates);
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

static int send_out(AVFilterContext *ctx, int out_id)
{
    AStreamSyncContext *as = ctx->priv;
    struct buf_queue *queue = &as->queue[out_id];
    AVFrame *buf = queue->buf[queue->tail];
    int ret;

    queue->buf[queue->tail] = NULL;
    as->var_values[VAR_B1 + out_id]++;
    as->var_values[VAR_S1 + out_id] += buf->nb_samples;
    if (buf->pts != AV_NOPTS_VALUE)
        as->var_values[VAR_T1 + out_id] =
            av_q2d(ctx->outputs[out_id]->time_base) * buf->pts;
    as->var_values[VAR_T1 + out_id] += buf->nb_samples /
                                   (double)ctx->inputs[out_id]->sample_rate;
    ret = ff_filter_frame(ctx->outputs[out_id], buf);
    queue->nb--;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    if (as->req[out_id])
        as->req[out_id]--;
    return ret;
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
            ff_request_frame(ctx->inputs[as->next_out]);
            if (as->eof & (1 << as->next_out))
                as->next_out = !as->next_out;
        }
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AStreamSyncContext *as = ctx->priv;
    int id = inlink == ctx->inputs[1];

    as->queue[id].buf[(as->queue[id].tail + as->queue[id].nb++) % QUEUE_SIZE] =
        insamples;
    as->eof &= ~(1 << id);
    send_next(ctx);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AStreamSyncContext *as = ctx->priv;

    av_expr_free(as->expr);
    as->expr = NULL;
}

static const AVFilterPad astreamsync_inputs[] = {
    {
        .name         = "in1",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },{
        .name         = "in2",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad astreamsync_outputs[] = {
    {
        .name          = "out1",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },{
        .name          = "out2",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_af_astreamsync = {
    .name          = "astreamsync",
    .description   = NULL_IF_CONFIG_SMALL("Copy two streams of audio data "
                                          "in a configurable order."),
    .priv_size     = sizeof(AStreamSyncContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = astreamsync_inputs,
    .outputs       = astreamsync_outputs,
    .priv_class    = &astreamsync_class,
};
