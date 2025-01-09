/*
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen
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

typedef struct StereoToolsContext {
    const AVClass *class;

    int softclip;
    int mute_l;
    int mute_r;
    int phase_l;
    int phase_r;
    int mode;
    int bmode_in;
    int bmode_out;
    double slev;
    double sbal;
    double mlev;
    double mpan;
    double phase;
    double base;
    double delay;
    double balance_in;
    double balance_out;
    double phase_sin_coef;
    double phase_cos_coef;
    double sc_level;
    double inv_atan_shape;
    double level_in;
    double level_out;

    double *buffer;
    int length;
    int pos;
} StereoToolsContext;

#define OFFSET(x) offsetof(StereoToolsContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption stereotools_options[] = {
    { "level_in",    "set level in",     OFFSET(level_in),    AV_OPT_TYPE_DOUBLE, {.dbl=1},   0.015625,  64, A },
    { "level_out",   "set level out",    OFFSET(level_out),   AV_OPT_TYPE_DOUBLE, {.dbl=1},   0.015625,  64, A },
    { "balance_in",  "set balance in",   OFFSET(balance_in),  AV_OPT_TYPE_DOUBLE, {.dbl=0},  -1,          1, A },
    { "balance_out", "set balance out",  OFFSET(balance_out), AV_OPT_TYPE_DOUBLE, {.dbl=0},  -1,          1, A },
    { "softclip",    "enable softclip",  OFFSET(softclip),    AV_OPT_TYPE_BOOL,   {.i64=0},   0,          1, A },
    { "mutel",       "mute L",           OFFSET(mute_l),      AV_OPT_TYPE_BOOL,   {.i64=0},   0,          1, A },
    { "muter",       "mute R",           OFFSET(mute_r),      AV_OPT_TYPE_BOOL,   {.i64=0},   0,          1, A },
    { "phasel",      "phase L",          OFFSET(phase_l),     AV_OPT_TYPE_BOOL,   {.i64=0},   0,          1, A },
    { "phaser",      "phase R",          OFFSET(phase_r),     AV_OPT_TYPE_BOOL,   {.i64=0},   0,          1, A },
    { "mode",        "set stereo mode",  OFFSET(mode),        AV_OPT_TYPE_INT,    {.i64=0},   0,         10, A, .unit = "mode" },
    {     "lr>lr",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=0},   0,          0, A, .unit = "mode" },
    {     "lr>ms",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=1},   0,          0, A, .unit = "mode" },
    {     "ms>lr",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=2},   0,          0, A, .unit = "mode" },
    {     "lr>ll",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=3},   0,          0, A, .unit = "mode" },
    {     "lr>rr",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=4},   0,          0, A, .unit = "mode" },
    {     "lr>l+r",  0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=5},   0,          0, A, .unit = "mode" },
    {     "lr>rl",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=6},   0,          0, A, .unit = "mode" },
    {     "ms>ll",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=7},   0,          0, A, .unit = "mode" },
    {     "ms>rr",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=8},   0,          0, A, .unit = "mode" },
    {     "ms>rl",   0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=9},   0,          0, A, .unit = "mode" },
    {     "lr>l-r",  0,                  0,                   AV_OPT_TYPE_CONST,  {.i64=10},  0,          0, A, .unit = "mode" },
    { "slev",        "set side level",   OFFSET(slev),        AV_OPT_TYPE_DOUBLE, {.dbl=1},   0.015625,  64, A },
    { "sbal",        "set side balance", OFFSET(sbal),        AV_OPT_TYPE_DOUBLE, {.dbl=0},  -1,          1, A },
    { "mlev",        "set middle level", OFFSET(mlev),        AV_OPT_TYPE_DOUBLE, {.dbl=1},   0.015625,  64, A },
    { "mpan",        "set middle pan",   OFFSET(mpan),        AV_OPT_TYPE_DOUBLE, {.dbl=0},  -1,          1, A },
    { "base",        "set stereo base",  OFFSET(base),        AV_OPT_TYPE_DOUBLE, {.dbl=0},  -1,          1, A },
    { "delay",       "set delay",        OFFSET(delay),       AV_OPT_TYPE_DOUBLE, {.dbl=0}, -20,         20, A },
    { "sclevel",     "set S/C level",    OFFSET(sc_level),    AV_OPT_TYPE_DOUBLE, {.dbl=1},   1,        100, A },
    { "phase",       "set stereo phase", OFFSET(phase),       AV_OPT_TYPE_DOUBLE, {.dbl=0},   0,        360, A },
    { "bmode_in",    "set balance in mode", OFFSET(bmode_in), AV_OPT_TYPE_INT,    {.i64=0},   0,          2, A, .unit = "bmode" },
    {     "balance",   0,                0,                   AV_OPT_TYPE_CONST,  {.i64=0},   0,          0, A, .unit = "bmode" },
    {     "amplitude", 0,                0,                   AV_OPT_TYPE_CONST,  {.i64=1},   0,          0, A, .unit = "bmode" },
    {     "power",     0,                0,                   AV_OPT_TYPE_CONST,  {.i64=2},   0,          0, A, .unit = "bmode" },
    { "bmode_out", "set balance out mode", OFFSET(bmode_out), AV_OPT_TYPE_INT,    {.i64=0},   0,          2, A, .unit = "bmode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(stereotools);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVSampleFormat formats[] = {
        AV_SAMPLE_FMT_DBL,
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
    StereoToolsContext *s = ctx->priv;

    s->length = FFALIGN((inlink->sample_rate + 9) / 10, 2);
    if (!s->buffer)
        s->buffer = av_calloc(s->length, sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    s->inv_atan_shape = 1.0 / atan(s->sc_level);
    s->phase_cos_coef = cos(s->phase / 180 * M_PI);
    s->phase_sin_coef = sin(s->phase / 180 * M_PI);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    StereoToolsContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const double sb = s->base < 0 ? s->base * 0.5 : s->base;
    const double sbal = 1 + s->sbal;
    const double mpan = 1 + s->mpan;
    const double slev = s->slev;
    const double mlev = s->mlev;
    const double balance_in = s->balance_in;
    const double balance_out = s->balance_out;
    const double level_in = s->level_in;
    const double level_out = s->level_out;
    const double sc_level = s->sc_level;
    const double delay = s->delay;
    const int length = s->length;
    const int mute_l = s->mute_l;
    const int mute_r = s->mute_r;
    const int phase_l = s->phase_l;
    const int phase_r = s->phase_r;
    double *buffer = s->buffer;
    AVFrame *out;
    double *dst;
    int nbuf = inlink->sample_rate * (fabs(delay) / 1000.);
    int n;

    nbuf -= nbuf % 2;
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
        double L = src[0], R = src[1], l, r, m, S, gl, gr, gd;

        L *= level_in;
        R *= level_in;

        gl = 1. - FFMAX(0., balance_in);
        gr = 1. + FFMIN(0., balance_in);
        switch (s->bmode_in) {
        case 1:
            gd = gl - gr;
            gl = 1. + gd;
            gr = 1. - gd;
            break;
        case 2:
            if (balance_in < 0.) {
                gr = FFMAX(0.5, gr);
                gl = 1. / gr;
            } else if (balance_in > 0.) {
                gl = FFMAX(0.5, gl);
                gr = 1. / gl;
            }
            break;
        }
        L *= gl;
        R *= gr;

        if (s->softclip) {
            R = s->inv_atan_shape * atan(R * sc_level);
            L = s->inv_atan_shape * atan(L * sc_level);
        }

        switch (s->mode) {
        case 0:
            m = (L + R) * 0.5;
            S = (L - R) * 0.5;
            l = m * mlev * FFMIN(1., 2. - mpan) + S * slev * FFMIN(1., 2. - sbal);
            r = m * mlev * FFMIN(1., mpan)      - S * slev * FFMIN(1., sbal);
            L = l;
            R = r;
            break;
        case 1:
            l = L * FFMIN(1., 2. - sbal);
            r = R * FFMIN(1., sbal);
            L = 0.5 * (l + r) * mlev;
            R = 0.5 * (l - r) * slev;
            break;
        case 2:
            l = L * mlev * FFMIN(1., 2. - mpan) + R * slev * FFMIN(1., 2. - sbal);
            r = L * mlev * FFMIN(1., mpan)      - R * slev * FFMIN(1., sbal);
            L = l;
            R = r;
            break;
        case 3:
            R = L;
            break;
        case 4:
            L = R;
            break;
        case 5:
            L = (L + R) * 0.5;
            R = L;
            break;
        case 6:
            l = L;
            L = R;
            R = l;
            m = (L + R) * 0.5;
            S = (L - R) * 0.5;
            l = m * mlev * FFMIN(1., 2. - mpan) + S * slev * FFMIN(1., 2. - sbal);
            r = m * mlev * FFMIN(1., mpan)      - S * slev * FFMIN(1., sbal);
            L = l;
            R = r;
            break;
        case 7:
            l = L * mlev * FFMIN(1., 2. - mpan) + R * slev * FFMIN(1., 2. - sbal);
            L = l;
            R = l;
            break;
        case 8:
            r = L * mlev * FFMIN(1., mpan)      - R * slev * FFMIN(1., sbal);
            L = r;
            R = r;
            break;
        case 9:
            l = L * mlev * FFMIN(1., 2. - mpan) + R * slev * FFMIN(1., 2. - sbal);
            r = L * mlev * FFMIN(1., mpan)      - R * slev * FFMIN(1., sbal);
            L = r;
            R = l;
            break;
        case 10:
            L = (L - R) * 0.5;
            R = L;
            break;
        }

        L *= 1. - mute_l;
        R *= 1. - mute_r;

        L *= (2. * (1. - phase_l)) - 1.;
        R *= (2. * (1. - phase_r)) - 1.;

        buffer[s->pos  ] = L;
        buffer[s->pos+1] = R;

        if (delay > 0.) {
            R = buffer[(s->pos - (int)nbuf + 1 + length) % length];
        } else if (delay < 0.) {
            L = buffer[(s->pos - (int)nbuf + length)     % length];
        }

        l = L + sb * L - sb * R;
        r = R + sb * R - sb * L;

        L = l;
        R = r;

        l = L * s->phase_cos_coef - R * s->phase_sin_coef;
        r = L * s->phase_sin_coef + R * s->phase_cos_coef;

        L = l;
        R = r;

        s->pos = (s->pos + 2) % s->length;

        gl = 1. - FFMAX(0., balance_out);
        gr = 1. + FFMIN(0., balance_out);
        switch (s->bmode_out) {
        case 1:
            gd = gl - gr;
            gl = 1. + gd;
            gr = 1. - gd;
            break;
        case 2:
            if (balance_out < 0.) {
                gr = FFMAX(0.5, gr);
                gl = 1. / gr;
            } else if (balance_out > 0.) {
                gl = FFMAX(0.5, gl);
                gr = 1. / gl;
            }
            break;
        }
        L *= gl;
        R *= gr;


        L *= level_out;
        R *= level_out;

        if (ctx->is_disabled) {
            dst[0] = src[0];
            dst[1] = src[1];
        } else {
            dst[0] = L;
            dst[1] = R;
        }
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    StereoToolsContext *s = ctx->priv;

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

const FFFilter ff_af_stereotools = {
    .p.name         = "stereotools",
    .p.description  = NULL_IF_CONFIG_SMALL("Apply various stereo tools."),
    .p.priv_class   = &stereotools_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size      = sizeof(StereoToolsContext),
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = process_command,
};
