/*
 * Copyright (c) 2015 The FFmpeg Project
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
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct ExtraStereoContext {
    const AVClass *class;
    float mult;
    int clip;
} ExtraStereoContext;

#define OFFSET(x) offsetof(ExtraStereoContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption extrastereo_options[] = {
    { "m", "set the difference coefficient", OFFSET(mult), AV_OPT_TYPE_FLOAT, {.dbl=2.5}, -10, 10, A },
    { "c", "enable clipping",                OFFSET(clip), AV_OPT_TYPE_BOOL,  {.i64=1},     0,  1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(extrastereo);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    int ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_FLT  )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout             )) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ExtraStereoContext *s = ctx->priv;
    const float *src = (const float *)in->data[0];
    const float mult = s->mult;
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

    for (n = 0; n < in->nb_samples; n++) {
        float average, left, right;

        left    = src[n * 2    ];
        right   = src[n * 2 + 1];
        average = (left + right) / 2.;
        left    = average + mult * (left  - average);
        right   = average + mult * (right - average);

        if (s->clip) {
            left  = av_clipf(left,  -1, 1);
            right = av_clipf(right, -1, 1);
        }

        dst[n * 2    ] = left;
        dst[n * 2 + 1] = right;
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_extrastereo = {
    .name           = "extrastereo",
    .description    = NULL_IF_CONFIG_SMALL("Increase difference between stereo audio channels."),
    .priv_size      = sizeof(ExtraStereoContext),
    .priv_class     = &extrastereo_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = ff_filter_process_command,
};
