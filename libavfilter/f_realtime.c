/*
 * Copyright (c) 2015 Nicolas George
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

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include <float.h>

typedef struct RealtimeContext {
    const AVClass *class;
    int64_t delta;
    int64_t limit;
    double speed;
    unsigned inited;
} RealtimeContext;

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    RealtimeContext *s = ctx->priv;

    if (frame->pts != AV_NOPTS_VALUE) {
        int64_t pts = av_rescale_q(frame->pts, inlink->time_base, AV_TIME_BASE_Q) / s->speed;
        int64_t now = av_gettime_relative();
        int64_t sleep = pts - now + s->delta;
        if (!s->inited) {
            s->inited = 1;
            sleep = 0;
            s->delta = now - pts;
        }
        if (FFABS(sleep) > s->limit / s->speed) {
            av_log(ctx, AV_LOG_WARNING,
                   "time discontinuity detected: %"PRIi64" us, resetting\n",
                   sleep);
            sleep = 0;
            s->delta = now - pts;
        }
        if (sleep > 0) {
            av_log(ctx, AV_LOG_DEBUG, "sleeping %"PRIi64" us\n", sleep);
            for (; sleep > 600000000; sleep -= 600000000)
                av_usleep(600000000);
            av_usleep(sleep);
        }
    }
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(RealtimeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption options[] = {
    { "limit", "sleep time limit", OFFSET(limit), AV_OPT_TYPE_DURATION, { .i64 = 2000000 }, 0, INT64_MAX, FLAGS },
    { "speed", "speed factor", OFFSET(speed), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, DBL_MIN, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(realtime, "(a)realtime", options);

#if CONFIG_REALTIME_FILTER

static const AVFilterPad avfilter_vf_realtime_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_realtime = {
    .p.name        = "realtime",
    .p.description = NULL_IF_CONFIG_SMALL("Slow down filtering to match realtime."),
    .p.priv_class  = &realtime_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,
    .priv_size   = sizeof(RealtimeContext),
    FILTER_INPUTS(avfilter_vf_realtime_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    .process_command = ff_filter_process_command,
};
#endif /* CONFIG_REALTIME_FILTER */

#if CONFIG_AREALTIME_FILTER

static const AVFilterPad arealtime_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_af_arealtime = {
    .p.name        = "arealtime",
    .p.description = NULL_IF_CONFIG_SMALL("Slow down filtering to match realtime."),
    .p.priv_class  = &realtime_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,
    .priv_size   = sizeof(RealtimeContext),
    FILTER_INPUTS(arealtime_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    .process_command = ff_filter_process_command,
};
#endif /* CONFIG_AREALTIME_FILTER */
