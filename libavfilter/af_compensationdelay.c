/*
 * Copyright (c) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen, Vladimir Sadovnikov and others
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

#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct CompensationDelayContext {
    const AVClass *class;
    int distance_mm;
    int distance_cm;
    int distance_m;
    double dry, wet;
    int temp;

    unsigned delay;
    unsigned w_ptr;
    unsigned buf_size;
    AVFrame *delay_frame;
} CompensationDelayContext;

#define OFFSET(x) offsetof(CompensationDelayContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption compensationdelay_options[] = {
    { "mm",   "set mm distance",    OFFSET(distance_mm), AV_OPT_TYPE_INT,    {.i64=0},    0,  10, A },
    { "cm",   "set cm distance",    OFFSET(distance_cm), AV_OPT_TYPE_INT,    {.i64=0},    0, 100, A },
    { "m",    "set meter distance", OFFSET(distance_m),  AV_OPT_TYPE_INT,    {.i64=0},    0, 100, A },
    { "dry",  "set dry amount",     OFFSET(dry),         AV_OPT_TYPE_DOUBLE, {.dbl=0},    0,   1, A },
    { "wet",  "set wet amount",     OFFSET(wet),         AV_OPT_TYPE_DOUBLE, {.dbl=1},    0,   1, A },
    { "temp", "set temperature °C", OFFSET(temp),        AV_OPT_TYPE_INT,    {.i64=20}, -50,  50, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(compensationdelay);

// The maximum distance for options
#define COMP_DELAY_MAX_DISTANCE            (100.0 * 100.0 + 100.0 * 1.0 + 1.0)
// The actual speed of sound in normal conditions
#define COMP_DELAY_SOUND_SPEED_KM_H(temp)  1.85325 * (643.95 * sqrt(((temp + 273.15) / 273.15)))
#define COMP_DELAY_SOUND_SPEED_CM_S(temp)  (COMP_DELAY_SOUND_SPEED_KM_H(temp) * (1000.0 * 100.0) /* cm/km */ / (60.0 * 60.0) /* s/h */)
#define COMP_DELAY_SOUND_FRONT_DELAY(temp) (1.0 / COMP_DELAY_SOUND_SPEED_CM_S(temp))
// The maximum delay may be reached by this filter
#define COMP_DELAY_MAX_DELAY               (COMP_DELAY_MAX_DISTANCE * COMP_DELAY_SOUND_FRONT_DELAY(50))

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CompensationDelayContext *s = ctx->priv;
    unsigned min_size, new_size = 1;

    s->delay = (s->distance_m * 100. + s->distance_cm * 1. + s->distance_mm * .1) *
               COMP_DELAY_SOUND_FRONT_DELAY(s->temp) * inlink->sample_rate;
    min_size = inlink->sample_rate * COMP_DELAY_MAX_DELAY;

    while (new_size < min_size)
        new_size <<= 1;

    s->delay_frame = av_frame_alloc();
    if (!s->delay_frame)
        return AVERROR(ENOMEM);

    s->buf_size                    = new_size;
    s->delay_frame->format         = inlink->format;
    s->delay_frame->nb_samples     = new_size;
    s->delay_frame->channel_layout = inlink->channel_layout;

    return av_frame_get_buffer(s->delay_frame, 0);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CompensationDelayContext *s = ctx->priv;
    const unsigned b_mask = s->buf_size - 1;
    const unsigned buf_size = s->buf_size;
    const unsigned delay = s->delay;
    const double dry = s->dry;
    const double wet = s->wet;
    unsigned r_ptr, w_ptr = 0;
    AVFrame *out;
    int n, ch;

    out = ff_get_audio_buffer(ctx->outputs[0], in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (ch = 0; ch < inlink->channels; ch++) {
        const double *src = (const double *)in->extended_data[ch];
        double *dst = (double *)out->extended_data[ch];
        double *buffer = (double *)s->delay_frame->extended_data[ch];

        w_ptr =  s->w_ptr;
        r_ptr = (w_ptr + buf_size - delay) & b_mask;

        for (n = 0; n < in->nb_samples; n++) {
            const double sample = src[n];

            buffer[w_ptr] = sample;
            dst[n] = dry * sample + wet * buffer[r_ptr];
            w_ptr = (w_ptr + 1) & b_mask;
            r_ptr = (r_ptr + 1) & b_mask;
        }
    }
    s->w_ptr = w_ptr;

    av_frame_free(&in);
    return ff_filter_frame(ctx->outputs[0], out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CompensationDelayContext *s = ctx->priv;

    av_frame_free(&s->delay_frame);
}

static const AVFilterPad compensationdelay_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad compensationdelay_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_compensationdelay = {
    .name          = "compensationdelay",
    .description   = NULL_IF_CONFIG_SMALL("Audio Compensation Delay Line."),
    .query_formats = query_formats,
    .priv_size     = sizeof(CompensationDelayContext),
    .priv_class    = &compensationdelay_class,
    .uninit        = uninit,
    .inputs        = compensationdelay_inputs,
    .outputs       = compensationdelay_outputs,
};
