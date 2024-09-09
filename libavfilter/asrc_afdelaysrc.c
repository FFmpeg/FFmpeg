/*
 * Copyright (c) 2023 Paul B Mahol
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

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

typedef struct AFDelaySrcContext {
    const AVClass *class;

    double delay;
    int sample_rate;
    int nb_samples;
    int nb_taps;
    AVChannelLayout chlayout;

    int64_t pts;
} AFDelaySrcContext;

static float sincf(float x)
{
    if (x == 0.f)
        return 1.f;
    return sinf(M_PI * x) / (M_PI * x);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    AFDelaySrcContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int nb_samples;
    float *dst;

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    nb_samples = FFMIN(s->nb_samples, s->nb_taps - s->pts);
    if (nb_samples <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!(frame = ff_get_audio_buffer(outlink, nb_samples)))
        return AVERROR(ENOMEM);

    dst = (float *)frame->extended_data[0];
    for (int n = 0; n < nb_samples; n++) {
        float x = s->pts + n;
        dst[n] = sincf(x - s->delay) * cosf(M_PI * (x - s->delay) / s->nb_taps) / sincf((x - s->delay) / s->nb_taps);
    }

    for (int ch = 1; ch < frame->ch_layout.nb_channels; ch++)
        memcpy(frame->extended_data[ch], dst, sizeof(*dst) * nb_samples);

    frame->pts = s->pts;
    s->pts    += nb_samples;

    return ff_filter_frame(outlink, frame);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AFDelaySrcContext *s = ctx->priv;
    AVChannelLayout chlayouts[] = { s->chlayout, { 0 } };
    int sample_rates[] = { s->sample_rate, -1 };
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP,
                                                       AV_SAMPLE_FMT_NONE };
    int ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, sample_fmts);
    if (ret < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, chlayouts);
    if (ret < 0)
        return ret;

    return ff_set_common_samplerates_from_list2(ctx, cfg_in, cfg_out, sample_rates);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AFDelaySrcContext *s = ctx->priv;

    outlink->sample_rate = s->sample_rate;
    s->pts = 0;
    if (s->nb_taps <= 0)
        s->nb_taps = s->delay * 8 + 1;

    return 0;
}

static const AVFilterPad afdelaysrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AFDelaySrcContext, x)

static const AVOption afdelaysrc_options[] = {
    { "delay",       "set fractional delay",                          OFFSET(delay),       AV_OPT_TYPE_DOUBLE,{.dbl=0},      0, INT16_MAX, AF },
    { "d",           "set fractional delay",                          OFFSET(delay),       AV_OPT_TYPE_DOUBLE,{.dbl=0},      0, INT16_MAX, AF },
    { "sample_rate", "set sample rate",                               OFFSET(sample_rate), AV_OPT_TYPE_INT,   {.i64=44100},  1, INT_MAX,   AF },
    { "r",           "set sample rate",                               OFFSET(sample_rate), AV_OPT_TYPE_INT,   {.i64=44100},  1, INT_MAX,   AF },
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples),  AV_OPT_TYPE_INT,   {.i64=1024},   1, INT_MAX,   AF },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples),  AV_OPT_TYPE_INT,   {.i64=1024},   1, INT_MAX,   AF },
    { "taps",        "set number of taps for delay filter",           OFFSET(nb_taps),     AV_OPT_TYPE_INT,   {.i64=0},      0,   32768,   AF },
    { "t",           "set number of taps for delay filter",           OFFSET(nb_taps),     AV_OPT_TYPE_INT,   {.i64=0},      0,   32768,   AF },
    { "channel_layout", "set channel layout",                         OFFSET(chlayout),    AV_OPT_TYPE_CHLAYOUT,{.str="stereo"},0,      0,   AF },
    { "c",              "set channel layout",                         OFFSET(chlayout),    AV_OPT_TYPE_CHLAYOUT,{.str="stereo"},0,      0,   AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afdelaysrc);

const AVFilter ff_asrc_afdelaysrc = {
    .name          = "afdelaysrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate a Fractional delay FIR coefficients."),
    .priv_size     = sizeof(AFDelaySrcContext),
    .priv_class    = &afdelaysrc_class,
    .activate      = activate,
    .inputs        = NULL,
    FILTER_OUTPUTS(afdelaysrc_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
