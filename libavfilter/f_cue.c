/*
 * Copyright (c) 2018 Marton Balint
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

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avfilter.h"
#include "filters.h"
#include "framequeue.h"
#include "internal.h"

typedef struct CueContext {
    const AVClass *class;
    int64_t first_pts;
    int64_t cue;
    int64_t preroll;
    int64_t buffer;
    int status;
    FFFrameQueue queue;
} CueContext;

static av_cold int init(AVFilterContext *ctx)
{
    CueContext *s = ctx->priv;
    ff_framequeue_init(&s->queue, &ctx->graph->internal->frame_queues);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CueContext *s = ctx->priv;
    ff_framequeue_free(&s->queue);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    CueContext *s = ctx->priv;
    int64_t pts;
    AVFrame *frame = NULL;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->status < 3 || s->status == 5) {
        int ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (frame)
            pts = av_rescale_q(frame->pts, inlink->time_base, AV_TIME_BASE_Q);
    }

    if (!s->status && frame) {
        s->first_pts = pts;
        s->status++;
    }
    if (s->status == 1 && frame) {
        if (pts - s->first_pts < s->preroll)
            return ff_filter_frame(outlink, frame);
        s->first_pts = pts;
        s->status++;
    }
    if (s->status == 2 && frame) {
        int ret = ff_framequeue_add(&s->queue, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return ret;
        }
        frame = NULL;
        if (!(pts - s->first_pts < s->buffer && (av_gettime() - s->cue) < 0))
            s->status++;
    }
    if (s->status == 3) {
        int64_t diff;
        while ((diff = (av_gettime() - s->cue)) < 0)
            av_usleep(av_clip(-diff / 2, 100, 1000000));
        s->status++;
    }
    if (s->status == 4) {
        if (ff_framequeue_queued_frames(&s->queue))
            return ff_filter_frame(outlink, ff_framequeue_take(&s->queue));
        s->status++;
    }
    if (s->status == 5 && frame)
        return ff_filter_frame(outlink, frame);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

#define OFFSET(x) offsetof(CueContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "cue", "cue unix timestamp in microseconds", OFFSET(cue), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, FLAGS },
    { "preroll", "preroll duration in seconds", OFFSET(preroll), AV_OPT_TYPE_DURATION, { .i64 = 0 }, 0, INT64_MAX, FLAGS },
    { "buffer", "buffer duration in seconds", OFFSET(buffer), AV_OPT_TYPE_DURATION, { .i64 = 0 }, 0, INT64_MAX, FLAGS },
    { NULL }
};

#if CONFIG_CUE_FILTER
#define cue_options options
AVFILTER_DEFINE_CLASS(cue);

static const AVFilterPad cue_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVFilterPad cue_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_cue = {
    .name        = "cue",
    .description = NULL_IF_CONFIG_SMALL("Delay filtering to match a cue."),
    .priv_size   = sizeof(CueContext),
    .priv_class  = &cue_class,
    .init        = init,
    .uninit      = uninit,
    .inputs      = cue_inputs,
    .outputs     = cue_outputs,
    .activate    = activate,
};
#endif /* CONFIG_CUE_FILTER */

#if CONFIG_ACUE_FILTER
#define acue_options options
AVFILTER_DEFINE_CLASS(acue);

static const AVFilterPad acue_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

static const AVFilterPad acue_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_acue = {
    .name        = "acue",
    .description = NULL_IF_CONFIG_SMALL("Delay filtering to match a cue."),
    .priv_size   = sizeof(CueContext),
    .priv_class  = &acue_class,
    .init        = init,
    .uninit      = uninit,
    .inputs      = acue_inputs,
    .outputs     = acue_outputs,
    .activate    = activate,
};
#endif /* CONFIG_ACUE_FILTER */
