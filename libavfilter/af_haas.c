/*
 * Copyright (c) 2001-2010 Vladimir Sadovnikov
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

#define MAX_HAAS_DELAY 40

typedef struct HaasContext {
    const AVClass *class;

    int par_m_source;
    double par_delay0;
    double par_delay1;
    int par_phase0;
    int par_phase1;
    int par_middle_phase;
    double par_side_gain;
    double par_gain0;
    double par_gain1;
    double par_balance0;
    double par_balance1;
    double level_in;
    double level_out;

    double *buffer;
    size_t buffer_size;
    uint32_t write_ptr;
    uint32_t delay[2];
    double balance_l[2];
    double balance_r[2];
    double phase0;
    double phase1;
} HaasContext;

#define OFFSET(x) offsetof(HaasContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption haas_options[] = {
    { "level_in",      "set level in",      OFFSET(level_in),         AV_OPT_TYPE_DOUBLE,  {.dbl=1}, 0.015625,  64, A },
    { "level_out",     "set level out",     OFFSET(level_out),        AV_OPT_TYPE_DOUBLE,  {.dbl=1}, 0.015625,  64, A },
    { "side_gain",     "set side gain",     OFFSET(par_side_gain),    AV_OPT_TYPE_DOUBLE,  {.dbl=1}, 0.015625,  64, A },
    { "middle_source", "set middle source", OFFSET(par_m_source),     AV_OPT_TYPE_INT,     {.i64=2},        0,   3, A, .unit = "source" },
    {   "left",        0,                   0,                        AV_OPT_TYPE_CONST,   {.i64=0},        0,   0, A, .unit = "source" },
    {   "right",       0,                   0,                        AV_OPT_TYPE_CONST,   {.i64=1},        0,   0, A, .unit = "source" },
    {   "mid",         "L+R",               0,                        AV_OPT_TYPE_CONST,   {.i64=2},        0,   0, A, .unit = "source" },
    {   "side",        "L-R",               0,                        AV_OPT_TYPE_CONST,   {.i64=3},        0,   0, A, .unit = "source" },
    { "middle_phase",  "set middle phase",  OFFSET(par_middle_phase), AV_OPT_TYPE_BOOL,    {.i64=0},        0,   1, A },
    { "left_delay",    "set left delay",    OFFSET(par_delay0),       AV_OPT_TYPE_DOUBLE,  {.dbl=2.05},     0,  MAX_HAAS_DELAY, A },
    { "left_balance",  "set left balance",  OFFSET(par_balance0),     AV_OPT_TYPE_DOUBLE,  {.dbl=-1.0},    -1,   1, A },
    { "left_gain",     "set left gain",     OFFSET(par_gain0),        AV_OPT_TYPE_DOUBLE,  {.dbl=1}, 0.015625,  64, A },
    { "left_phase",    "set left phase",    OFFSET(par_phase0),       AV_OPT_TYPE_BOOL,    {.i64=0},        0,   1, A },
    { "right_delay",   "set right delay",   OFFSET(par_delay1),       AV_OPT_TYPE_DOUBLE,  {.dbl=2.12},     0,  MAX_HAAS_DELAY, A },
    { "right_balance", "set right balance", OFFSET(par_balance1),     AV_OPT_TYPE_DOUBLE,  {.dbl=1},       -1,   1, A },
    { "right_gain",    "set right gain",    OFFSET(par_gain1),        AV_OPT_TYPE_DOUBLE,  {.dbl=1}, 0.015625,  64, A },
    { "right_phase",   "set right phase",   OFFSET(par_phase1),       AV_OPT_TYPE_BOOL,    {.i64=1},        0,   1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(haas);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    int ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_DBL  )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout             )) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    HaasContext *s = ctx->priv;
    size_t min_buf_size = (size_t)(inlink->sample_rate * MAX_HAAS_DELAY * 0.001);
    size_t new_buf_size = 1;

    while (new_buf_size < min_buf_size)
        new_buf_size <<= 1;

    av_freep(&s->buffer);
    s->buffer = av_calloc(new_buf_size, sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    s->buffer_size = new_buf_size;
    s->write_ptr = 0;

    s->delay[0] = (uint32_t)(s->par_delay0 * 0.001 * inlink->sample_rate);
    s->delay[1] = (uint32_t)(s->par_delay1 * 0.001 * inlink->sample_rate);

    s->phase0 = s->par_phase0 ? 1.0 : -1.0;
    s->phase1 = s->par_phase1 ? 1.0 : -1.0;

    s->balance_l[0] = (s->par_balance0 + 1) / 2 * s->par_gain0 * s->phase0;
    s->balance_r[0] = (1.0 - (s->par_balance0 + 1) / 2) * (s->par_gain0) * s->phase0;
    s->balance_l[1] = (s->par_balance1 + 1) / 2 * s->par_gain1 * s->phase1;
    s->balance_r[1] = (1.0 - (s->par_balance1 + 1) / 2) * (s->par_gain1) * s->phase1;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    HaasContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const double level_in = s->level_in;
    const double level_out = s->level_out;
    const uint32_t mask = s->buffer_size - 1;
    double *buffer = s->buffer;
    AVFrame *out;
    double *dst;
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
    dst = (double *)out->data[0];

    for (n = 0; n < in->nb_samples; n++, src += 2, dst += 2) {
        double mid, side[2], side_l, side_r;
        uint32_t s0_ptr, s1_ptr;

        switch (s->par_m_source) {
        case 0: mid = src[0]; break;
        case 1: mid = src[1]; break;
        case 2: mid = (src[0] + src[1]) * 0.5; break;
        case 3: mid = (src[0] - src[1]) * 0.5; break;
        }

        mid *= level_in;

        buffer[s->write_ptr] = mid;

        s0_ptr = (s->write_ptr + s->buffer_size - s->delay[0]) & mask;
        s1_ptr = (s->write_ptr + s->buffer_size - s->delay[1]) & mask;

        if (s->par_middle_phase)
            mid = -mid;

        side[0] = buffer[s0_ptr] * s->par_side_gain;
        side[1] = buffer[s1_ptr] * s->par_side_gain;
        side_l  = side[0] * s->balance_l[0] - side[1] * s->balance_l[1];
        side_r  = side[1] * s->balance_r[1] - side[0] * s->balance_r[0];

        dst[0] = (mid + side_l) * level_out;
        dst[1] = (mid + side_r) * level_out;

        s->write_ptr = (s->write_ptr + 1) & mask;
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HaasContext *s = ctx->priv;

    av_freep(&s->buffer);
    s->buffer_size = 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_af_haas = {
    .name           = "haas",
    .description    = NULL_IF_CONFIG_SMALL("Apply Haas Stereo Enhancer."),
    .priv_size      = sizeof(HaasContext),
    .priv_class     = &haas_class,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
};
