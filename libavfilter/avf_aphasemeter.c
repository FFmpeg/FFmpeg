/*
 * Copyright (c) 2015 Paul B Mahol
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
 * audio to video multimedia aphasemeter filter
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"
#include "float.h"

typedef struct AudioPhaseMeterContext {
    const AVClass *class;
    AVFrame *out;
    int do_video;
    int do_phasing_detection;
    int w, h;
    AVRational frame_rate;
    int contrast[4];
    uint8_t *mpc_str;
    uint8_t mpc[4];
    int draw_median_phase;
    int is_mono;
    int is_out_phase;
    int start_mono_presence;
    int start_out_phase_presence;
    float tolerance;
    float angle;
    float phase;
    AVRational time_base;
    int64_t duration;
    int64_t frame_end;
    int64_t mono_idx[2];
    int64_t out_phase_idx[2];
} AudioPhaseMeterContext;

#define MAX_DURATION (24*60*60*1000000LL)
#define OFFSET(x) offsetof(AudioPhaseMeterContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define get_duration(index) (index[1] - index[0])

static const AVOption aphasemeter_options[] = {
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="800x400"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="800x400"}, 0, 0, FLAGS },
    { "rc", "set red contrast",   OFFSET(contrast[0]), AV_OPT_TYPE_INT, {.i64=2}, 0, 255, FLAGS },
    { "gc", "set green contrast", OFFSET(contrast[1]), AV_OPT_TYPE_INT, {.i64=7}, 0, 255, FLAGS },
    { "bc", "set blue contrast",  OFFSET(contrast[2]), AV_OPT_TYPE_INT, {.i64=1}, 0, 255, FLAGS },
    { "mpc", "set median phase color", OFFSET(mpc_str), AV_OPT_TYPE_STRING, {.str = "none"}, 0, 0, FLAGS },
    { "video", "set video output", OFFSET(do_video), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "phasing", "set mono and out-of-phase detection output", OFFSET(do_phasing_detection), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { "tolerance", "set phase tolerance for mono detection", OFFSET(tolerance), AV_OPT_TYPE_FLOAT, {.dbl = 0.}, 0, 1, FLAGS },
    { "t",         "set phase tolerance for mono detection", OFFSET(tolerance), AV_OPT_TYPE_FLOAT, {.dbl = 0.}, 0, 1, FLAGS },
    { "angle", "set angle threshold for out-of-phase detection", OFFSET(angle), AV_OPT_TYPE_FLOAT, {.dbl = 170.}, 90, 180, FLAGS },
    { "a",     "set angle threshold for out-of-phase detection", OFFSET(angle), AV_OPT_TYPE_FLOAT, {.dbl = 170.}, 90, 180, FLAGS },
    { "duration", "set minimum mono or out-of-phase duration in seconds", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=2000000}, 0, MAX_DURATION, FLAGS },
    { "d",        "set minimum mono or out-of-phase duration in seconds", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=2000000}, 0, MAX_DURATION, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aphasemeter);

static int query_formats(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref         (formats, &inlink->outcfg.formats        )) < 0 ||
        (ret = ff_formats_ref         (formats, &outlink->incfg.formats        )) < 0 ||
        (ret = ff_add_channel_layout  (&layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO )) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &inlink->outcfg.channel_layouts)) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &outlink->incfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->incfg.samplerates)) < 0)
        return ret;

    if (s->do_video) {
        AVFilterLink *outlink = ctx->outputs[1];

        formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
            return ret;
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioPhaseMeterContext *s = ctx->priv;
    int nb_samples;
    s->duration = av_rescale(s->duration, inlink->sample_rate, AV_TIME_BASE);

    if (s->do_video) {
        nb_samples = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
        inlink->min_samples =
        inlink->max_samples = nb_samples;
    }

    return 0;
}

static int config_video_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioPhaseMeterContext *s = ctx->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    if (!strcmp(s->mpc_str, "none"))
        s->draw_median_phase = 0;
    else if (av_parse_color(s->mpc, s->mpc_str, -1, ctx) >= 0)
        s->draw_median_phase = 1;
    else
        return AVERROR(EINVAL);

    return 0;
}

static inline int get_x(float phase, int w)
{
  return (phase + 1.) / 2. * (w - 1);
}

static inline void add_metadata(AVFrame *insamples, const char *key, char *value)
{
    char buf[128];

    snprintf(buf, sizeof(buf), "lavfi.aphasemeter.%s", key);
    av_dict_set(&insamples->metadata, buf, value, 0);
}

static inline void update_mono_detection(AudioPhaseMeterContext *s, AVFrame *insamples, int mono_measurement)
{
    int64_t mono_duration;
    if (!s->is_mono && mono_measurement) {
        s->is_mono = 1;
        s->start_mono_presence = 1;
        s->mono_idx[0] = insamples->pts;
    }
    if (s->is_mono && mono_measurement && s->start_mono_presence) {
        s->mono_idx[1] = s->frame_end;
        mono_duration = get_duration(s->mono_idx);
        if (mono_duration >= s->duration) {
            add_metadata(insamples, "mono_start", av_ts2timestr(s->mono_idx[0], &s->time_base));
            av_log(s, AV_LOG_INFO, "mono_start: %s\n", av_ts2timestr(s->mono_idx[0], &s->time_base));
            s->start_mono_presence = 0;
        }
    }
    if (s->is_mono && !mono_measurement) {
        s->mono_idx[1] = insamples ? insamples->pts : s->frame_end;
        mono_duration = get_duration(s->mono_idx);
        if (mono_duration >= s->duration) {
            if (insamples) {
                add_metadata(insamples, "mono_end", av_ts2timestr(s->mono_idx[1], &s->time_base));
                add_metadata(insamples, "mono_duration", av_ts2timestr(mono_duration, &s->time_base));
            }
            av_log(s, AV_LOG_INFO, "mono_end: %s | mono_duration: %s\n", av_ts2timestr(s->mono_idx[1], &s->time_base), av_ts2timestr(mono_duration, &s->time_base));
        }
        s->is_mono = 0;
    }
}

static inline void update_out_phase_detection(AudioPhaseMeterContext *s, AVFrame *insamples, int out_phase_measurement)
{
    int64_t out_phase_duration;
    if (!s->is_out_phase && out_phase_measurement) {
        s->is_out_phase = 1;
        s->start_out_phase_presence = 1;
        s->out_phase_idx[0] = insamples->pts;
    }
    if (s->is_out_phase && out_phase_measurement && s->start_out_phase_presence) {
        s->out_phase_idx[1] = s->frame_end;
        out_phase_duration = get_duration(s->out_phase_idx);
        if (out_phase_duration >= s->duration) {
            add_metadata(insamples, "out_phase_start", av_ts2timestr(s->out_phase_idx[0], &s->time_base));
            av_log(s, AV_LOG_INFO, "out_phase_start: %s\n", av_ts2timestr(s->out_phase_idx[0], &s->time_base));
            s->start_out_phase_presence = 0;
        }
    }
    if (s->is_out_phase && !out_phase_measurement) {
        s->out_phase_idx[1] = insamples ? insamples->pts : s->frame_end;
        out_phase_duration = get_duration(s->out_phase_idx);
        if (out_phase_duration >= s->duration) {
            if (insamples) {
                add_metadata(insamples, "out_phase_end", av_ts2timestr(s->out_phase_idx[1], &s->time_base));
                add_metadata(insamples, "out_phase_duration", av_ts2timestr(out_phase_duration, &s->time_base));
            }
            av_log(s, AV_LOG_INFO, "out_phase_end: %s | out_phase_duration: %s\n", av_ts2timestr(s->out_phase_idx[1], &s->time_base), av_ts2timestr(out_phase_duration, &s->time_base));
        }
        s->is_out_phase = 0;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterLink *outlink = s->do_video ? ctx->outputs[1] : NULL;
    AVFilterLink *aoutlink = ctx->outputs[0];
    AVDictionary **metadata;
    const int rc = s->contrast[0];
    const int gc = s->contrast[1];
    const int bc = s->contrast[2];
    float fphase = 0;
    AVFrame *out;
    uint8_t *dst;
    int i, ret;
    int mono_measurement;
    int out_phase_measurement;
    float tolerance = 1.0f - s->tolerance;
    float angle = cosf(s->angle/180.0f*M_PI);

    if (s->do_video && (!s->out || s->out->width  != outlink->w ||
                                   s->out->height != outlink->h)) {
        av_frame_free(&s->out);
        s->out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        out = s->out;
        for (i = 0; i < outlink->h; i++)
            memset(out->data[0] + i * out->linesize[0], 0, outlink->w * 4);
    } else if (s->do_video) {
        ret = ff_inlink_make_frame_writable(outlink, &s->out);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }
        out = s->out;
        for (i = outlink->h - 1; i >= 10; i--)
            memmove(out->data[0] + (i  ) * out->linesize[0],
                    out->data[0] + (i-1) * out->linesize[0],
                    outlink->w * 4);
        for (i = 0; i < outlink->w; i++)
            AV_WL32(out->data[0] + i * 4, 0);
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *src = (float *)in->data[0] + i * 2;
        const float f = src[0] * src[1] / (src[0]*src[0] + src[1] * src[1]) * 2;
        const float phase = isnan(f) ? 1 : f;
        const int x = get_x(phase, s->w);

        if (s->do_video) {
            dst = out->data[0] + x * 4;
            dst[0] = FFMIN(255, dst[0] + rc);
            dst[1] = FFMIN(255, dst[1] + gc);
            dst[2] = FFMIN(255, dst[2] + bc);
            dst[3] = 255;
        }
        fphase += phase;
    }
    fphase /= in->nb_samples;
    s->phase = fphase;

    if (s->do_video) {
        if (s->draw_median_phase) {
            dst = out->data[0] + get_x(fphase, s->w) * 4;
            AV_WL32(dst, AV_RL32(s->mpc));
        }

        for (i = 1; i < 10 && i < outlink->h; i++)
            memcpy(out->data[0] + i * out->linesize[0], out->data[0], outlink->w * 4);
    }

    metadata = &in->metadata;
    if (metadata) {
        uint8_t value[128];

        snprintf(value, sizeof(value), "%f", fphase);
        add_metadata(in, "phase", value);
    }

    if (s->do_phasing_detection) {
        s->time_base = inlink->time_base;
        s->frame_end = in->pts + av_rescale_q(in->nb_samples,
            (AVRational){ 1, in->sample_rate }, inlink->time_base);

        mono_measurement = (tolerance - fphase) < FLT_EPSILON;
        out_phase_measurement = (angle - fphase) > FLT_EPSILON;

        update_mono_detection(s, in, mono_measurement);
        update_out_phase_detection(s, in, out_phase_measurement);
    }

    if (s->do_video) {
        AVFrame *clone;

        s->out->pts = in->pts;
        s->out->duration = av_rescale_q(1, av_inv_q(outlink->frame_rate), outlink->time_base);

        clone = av_frame_clone(s->out);
        if (!clone)
            return AVERROR(ENOMEM);
        ff_filter_frame(outlink, clone);
    }
    return ff_filter_frame(aoutlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;

    if (s->do_phasing_detection) {
        update_mono_detection(s, NULL, 0);
        update_out_phase_detection(s, NULL, 0);
    }
    av_frame_free(&s->out);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterPad pad;
    int ret;

    pad = (AVFilterPad){
        .name         = "out0",
        .type         = AVMEDIA_TYPE_AUDIO,
    };
    ret = ff_append_outpad(ctx, &pad);
    if (ret < 0)
        return ret;

    if (s->do_video) {
        pad = (AVFilterPad){
            .name         = "out1",
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video_output,
        };
        ret = ff_append_outpad(ctx, &pad);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_avf_aphasemeter = {
    .name          = "aphasemeter",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to phase meter video output."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(AudioPhaseMeterContext),
    FILTER_INPUTS(inputs),
    .outputs       = NULL,
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &aphasemeter_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
