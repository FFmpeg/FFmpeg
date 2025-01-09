/*
 * Copyright (C) 2012 VLC authors and VideoLAN
 * Author : Sukrit Sangwan < sukritsangwan at gmail dot com >
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

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "formats.h"

typedef struct StereoWidenContext {
    const AVClass *class;

    float delay;
    float feedback;
    float crossfeed;
    float drymix;

    float *buffer;
    float *cur;
    int length;
} StereoWidenContext;

#define OFFSET(x) offsetof(StereoWidenContext, x)
#define A  AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption stereowiden_options[] = {
    { "delay",     "set delay time",    OFFSET(delay),     AV_OPT_TYPE_FLOAT, {.dbl=20}, 1, 100, A },
    { "feedback",  "set feedback gain", OFFSET(feedback),  AV_OPT_TYPE_FLOAT, {.dbl=.3}, 0, 0.9, AT },
    { "crossfeed", "set cross feed",    OFFSET(crossfeed), AV_OPT_TYPE_FLOAT, {.dbl=.3}, 0, 0.8, AT },
    { "drymix",    "set dry-mix",       OFFSET(drymix),    AV_OPT_TYPE_FLOAT, {.dbl=.8}, 0, 1.0, AT },
    { NULL }
};

AVFILTER_DEFINE_CLASS(stereowiden);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVSampleFormat formats[] = {
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_NONE,
    };
    static const AVChannelLayout layouts[] = {
        AV_CHANNEL_LAYOUT_STEREO,
        { .nb_channels = 0 },
    };

    int ret;

    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, formats);
    if (ret < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, layouts);
    if (ret < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    StereoWidenContext *s = ctx->priv;

    s->length = lrintf(s->delay * inlink->sample_rate / 1000);
    s->length *= 2;
    if (s->length == 0)
        return AVERROR(EINVAL);
    s->buffer = av_calloc(s->length, sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);
    s->cur = s->buffer;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    StereoWidenContext *s = ctx->priv;
    const float *src = (const float *)in->data[0];
    const float drymix = s->drymix;
    const float crossfeed = s->crossfeed;
    const float feedback = s->feedback;
    AVFrame *out;
    float *dst;
    int n;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }
    dst = (float *)out->data[0];

    for (n = 0; n < in->nb_samples; n++, src += 2, dst += 2, s->cur += 2) {
        const float left = src[0], right = src[1];

        if (s->cur == s->buffer + s->length)
            s->cur = s->buffer;

        if (ctx->is_disabled) {
            dst[0] = left;
            dst[1] = right;
        } else {
            dst[0] = drymix * left - crossfeed * right - feedback * s->cur[1];
            dst[1] = drymix * right - crossfeed * left - feedback * s->cur[0];
        }

        s->cur[0] = left;
        s->cur[1] = right;
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    StereoWidenContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const FFFilter ff_af_stereowiden = {
    .p.name         = "stereowiden",
    .p.description  = NULL_IF_CONFIG_SMALL("Apply stereo widening effect."),
    .p.priv_class   = &stereowiden_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size      = sizeof(StereoWidenContext),
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = ff_filter_process_command,
};
