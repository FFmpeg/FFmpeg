/*
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

#include <float.h>
#include <math.h>

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct TrimContext {
    const AVClass *class;

    /*
     * AVOptions
     */
    int64_t duration;
    int64_t start_time, end_time;
    int64_t start_frame, end_frame;

    double duration_dbl;
    double start_time_dbl, end_time_dbl;
    /*
     * in the link timebase for video,
     * in 1/samplerate for audio
     */
    int64_t start_pts, end_pts;
    int64_t start_sample, end_sample;

    /*
     * number of video frames that arrived on this filter so far
     */
    int64_t nb_frames;
    /*
     * number of audio samples that arrived on this filter so far
     */
    int64_t nb_samples;
    /*
     * timestamp of the first frame in the output, in the timebase units
     */
    int64_t first_pts;
    /*
     * duration in the timebase units
     */
    int64_t duration_tb;

    int64_t next_pts;

    int eof;
} TrimContext;

static int init(AVFilterContext *ctx)
{
    TrimContext *s = ctx->priv;

    s->first_pts = AV_NOPTS_VALUE;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    TrimContext       *s = ctx->priv;
    AVRational tb = (inlink->type == AVMEDIA_TYPE_VIDEO) ?
                     inlink->time_base : (AVRational){ 1, inlink->sample_rate };

    if (s->start_time_dbl != DBL_MAX)
        s->start_time = s->start_time_dbl * 1e6;
    if (s->end_time_dbl != DBL_MAX)
        s->end_time = s->end_time_dbl * 1e6;
    if (s->duration_dbl != 0)
        s->duration = s->duration_dbl * 1e6;

    if (s->start_time != INT64_MAX) {
        int64_t start_pts = av_rescale_q(s->start_time, AV_TIME_BASE_Q, tb);
        if (s->start_pts == AV_NOPTS_VALUE || start_pts < s->start_pts)
            s->start_pts = start_pts;
    }
    if (s->end_time != INT64_MAX) {
        int64_t end_pts = av_rescale_q(s->end_time, AV_TIME_BASE_Q, tb);
        if (s->end_pts == AV_NOPTS_VALUE || end_pts > s->end_pts)
            s->end_pts = end_pts;
    }
    if (s->duration)
        s->duration_tb = av_rescale_q(s->duration, AV_TIME_BASE_Q, tb);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    return 0;
}

#define OFFSET(x) offsetof(TrimContext, x)
#define COMMON_OPTS                                                                                                                                                         \
    { "starti",      "Timestamp of the first frame that "                                                                                                        \
        "should be passed",                                              OFFSET(start_time),  AV_OPT_TYPE_DURATION, { .i64 = INT64_MAX },    INT64_MIN, INT64_MAX, FLAGS }, \
    { "endi",        "Timestamp of the first frame that "                                                                                                        \
        "should be dropped again",                                       OFFSET(end_time),    AV_OPT_TYPE_DURATION, { .i64 = INT64_MAX },    INT64_MIN, INT64_MAX, FLAGS }, \
    { "start_pts",   "Timestamp of the first frame that should be "                                                                                                         \
       " passed",                                                        OFFSET(start_pts),   AV_OPT_TYPE_INT64,  { .i64 = AV_NOPTS_VALUE }, INT64_MIN, INT64_MAX, FLAGS }, \
    { "end_pts",     "Timestamp of the first frame that should be "                                                                                                         \
        "dropped again",                                                 OFFSET(end_pts),     AV_OPT_TYPE_INT64,  { .i64 = AV_NOPTS_VALUE }, INT64_MIN, INT64_MAX, FLAGS }, \
    { "durationi",   "Maximum duration of the output",                   OFFSET(duration),    AV_OPT_TYPE_DURATION, { .i64 = 0 },                    0, INT64_MAX, FLAGS },

#define COMPAT_OPTS \
    { "start",       "Timestamp in seconds of the first frame that "                                                                                                        \
        "should be passed",                                              OFFSET(start_time_dbl),AV_OPT_TYPE_DOUBLE, { .dbl = DBL_MAX },       -DBL_MAX, DBL_MAX,     FLAGS }, \
    { "end",         "Timestamp in seconds of the first frame that "                                                                                                        \
        "should be dropped again",                                       OFFSET(end_time_dbl),  AV_OPT_TYPE_DOUBLE, { .dbl = DBL_MAX },       -DBL_MAX, DBL_MAX,     FLAGS }, \
    { "duration",    "Maximum duration of the output in seconds",        OFFSET(duration_dbl),  AV_OPT_TYPE_DOUBLE, { .dbl = 0 },                      0,   DBL_MAX, FLAGS },


#if CONFIG_TRIM_FILTER
static int trim_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    TrimContext       *s = ctx->priv;
    int drop;

    /* drop everything if EOF has already been returned */
    if (s->eof) {
        av_frame_free(&frame);
        return 0;
    }

    if (s->start_frame >= 0 || s->start_pts != AV_NOPTS_VALUE) {
        drop = 1;
        if (s->start_frame >= 0 && s->nb_frames >= s->start_frame)
            drop = 0;
        if (s->start_pts != AV_NOPTS_VALUE && frame->pts != AV_NOPTS_VALUE &&
            frame->pts >= s->start_pts)
            drop = 0;
        if (drop)
            goto drop;
    }

    if (s->first_pts == AV_NOPTS_VALUE && frame->pts != AV_NOPTS_VALUE)
        s->first_pts = frame->pts;

    if (s->end_frame != INT64_MAX || s->end_pts != AV_NOPTS_VALUE || s->duration_tb) {
        drop = 1;

        if (s->end_frame != INT64_MAX && s->nb_frames < s->end_frame)
            drop = 0;
        if (s->end_pts != AV_NOPTS_VALUE && frame->pts != AV_NOPTS_VALUE &&
            frame->pts < s->end_pts)
            drop = 0;
        if (s->duration_tb && frame->pts != AV_NOPTS_VALUE &&
            frame->pts - s->first_pts < s->duration_tb)
            drop = 0;

        if (drop) {
            s->eof = inlink->closed = 1;
            goto drop;
        }
    }

    s->nb_frames++;

    return ff_filter_frame(ctx->outputs[0], frame);

drop:
    s->nb_frames++;
    av_frame_free(&frame);
    return 0;
}

#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption trim_options[] = {
    COMMON_OPTS
    { "start_frame", "Number of the first frame that should be passed "
        "to the output",                                                 OFFSET(start_frame), AV_OPT_TYPE_INT64,  { .i64 = -1 },       -1, INT64_MAX, FLAGS },
    { "end_frame",   "Number of the first frame that should be dropped "
        "again",                                                         OFFSET(end_frame),   AV_OPT_TYPE_INT64,  { .i64 = INT64_MAX }, 0, INT64_MAX, FLAGS },
    COMPAT_OPTS
    { NULL },
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(trim);

static const AVFilterPad trim_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = trim_filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad trim_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter avfilter_vf_trim = {
    .name        = "trim",
    .description = NULL_IF_CONFIG_SMALL("Pick one continuous section from the input, drop the rest."),

    .init        = init,

    .priv_size   = sizeof(TrimContext),
    .priv_class  = &trim_class,

    .inputs      = trim_inputs,
    .outputs     = trim_outputs,
};
#endif // CONFIG_TRIM_FILTER

#if CONFIG_ATRIM_FILTER
static int atrim_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    TrimContext       *s = ctx->priv;
    int64_t start_sample, end_sample = frame->nb_samples;
    int64_t pts;
    int drop;

    /* drop everything if EOF has already been returned */
    if (s->eof) {
        av_frame_free(&frame);
        return 0;
    }

    if (frame->pts != AV_NOPTS_VALUE)
        pts = av_rescale_q(frame->pts, inlink->time_base,
                           (AVRational){ 1, inlink->sample_rate });
    else
        pts = s->next_pts;
    s->next_pts = pts + frame->nb_samples;

    /* check if at least a part of the frame is after the start time */
    if (s->start_sample < 0 && s->start_pts == AV_NOPTS_VALUE) {
        start_sample = 0;
    } else {
        drop = 1;
        start_sample = frame->nb_samples;

        if (s->start_sample >= 0 &&
            s->nb_samples + frame->nb_samples > s->start_sample) {
            drop         = 0;
            start_sample = FFMIN(start_sample, s->start_sample - s->nb_samples);
        }

        if (s->start_pts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE &&
            pts + frame->nb_samples > s->start_pts) {
            drop = 0;
            start_sample = FFMIN(start_sample, s->start_pts - pts);
        }

        if (drop)
            goto drop;
    }

    if (s->first_pts == AV_NOPTS_VALUE)
        s->first_pts = pts + start_sample;

    /* check if at least a part of the frame is before the end time */
    if (s->end_sample == INT64_MAX && s->end_pts == AV_NOPTS_VALUE && !s->duration_tb) {
        end_sample = frame->nb_samples;
    } else {
        drop       = 1;
        end_sample = 0;

        if (s->end_sample != INT64_MAX &&
            s->nb_samples < s->end_sample) {
            drop       = 0;
            end_sample = FFMAX(end_sample, s->end_sample - s->nb_samples);
        }

        if (s->end_pts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE &&
            pts < s->end_pts) {
            drop       = 0;
            end_sample = FFMAX(end_sample, s->end_pts - pts);
        }

        if (s->duration_tb && pts - s->first_pts < s->duration_tb) {
            drop       = 0;
            end_sample = FFMAX(end_sample, s->first_pts + s->duration_tb - pts);
        }

        if (drop) {
            s->eof = inlink->closed = 1;
            goto drop;
        }
    }

    s->nb_samples += frame->nb_samples;
    start_sample   = FFMAX(0, start_sample);
    end_sample     = FFMIN(frame->nb_samples, end_sample);
    av_assert0(start_sample < end_sample || (start_sample == end_sample && !frame->nb_samples));

    if (start_sample) {
        AVFrame *out = ff_get_audio_buffer(ctx->outputs[0], end_sample - start_sample);
        if (!out) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, frame);
        av_samples_copy(out->extended_data, frame->extended_data, 0, start_sample,
                        out->nb_samples, inlink->channels,
                        frame->format);
        if (out->pts != AV_NOPTS_VALUE)
            out->pts += av_rescale_q(start_sample, (AVRational){ 1, out->sample_rate },
                                     inlink->time_base);

        av_frame_free(&frame);
        frame = out;
    } else
        frame->nb_samples = end_sample;

    return ff_filter_frame(ctx->outputs[0], frame);

drop:
    s->nb_samples += frame->nb_samples;
    av_frame_free(&frame);
    return 0;
}

#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption atrim_options[] = {
    COMMON_OPTS
    { "start_sample", "Number of the first audio sample that should be "
        "passed to the output",                                          OFFSET(start_sample), AV_OPT_TYPE_INT64,  { .i64 = -1 },       -1, INT64_MAX, FLAGS },
    { "end_sample",   "Number of the first audio sample that should be "
        "dropped again",                                                 OFFSET(end_sample),   AV_OPT_TYPE_INT64,  { .i64 = INT64_MAX }, 0, INT64_MAX, FLAGS },
    COMPAT_OPTS
    { NULL },
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(atrim);

static const AVFilterPad atrim_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = atrim_filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad atrim_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter avfilter_af_atrim = {
    .name        = "atrim",
    .description = NULL_IF_CONFIG_SMALL("Pick one continuous section from the input, drop the rest."),

    .init        = init,

    .priv_size   = sizeof(TrimContext),
    .priv_class  = &atrim_class,

    .inputs      = atrim_inputs,
    .outputs     = atrim_outputs,
};
#endif // CONFIG_ATRIM_FILTER
