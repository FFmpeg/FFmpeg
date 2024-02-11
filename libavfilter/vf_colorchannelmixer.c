/*
 * Copyright (c) 2013 Paul B Mahol
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

#include <float.h>

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "internal.h"
#include "video.h"
#include "preserve_color.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

typedef struct ColorChannelMixerContext {
    const AVClass *class;
    double rr, rg, rb, ra;
    double gr, gg, gb, ga;
    double br, bg, bb, ba;
    double ar, ag, ab, aa;
    double preserve_amount;
    int preserve_color;

    int *lut[4][4];

    int *buffer;

    uint8_t rgba_map[4];

    int (*filter_slice[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ColorChannelMixerContext;

static float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static void preservel(float *r, float *g, float *b, float lin, float lout, float max)
{
    if (lout <= 0.f)
        lout = 1.f / (max * 2.f);
    *r *= lin / lout;
    *g *= lin / lout;
    *b *= lin / lout;
}

#define DEPTH 8
#include "colorchannelmixer_template.c"

#undef DEPTH
#define DEPTH 16
#include "colorchannelmixer_template.c"

#undef DEPTH
#define DEPTH 32
#include "colorchannelmixer_template.c"

#define OFFSET(x) offsetof(ColorChannelMixerContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colorchannelmixer_options[] = {
    { "rr", "set the red gain for the red channel",     OFFSET(rr), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "rg", "set the green gain for the red channel",   OFFSET(rg), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "rb", "set the blue gain for the red channel",    OFFSET(rb), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ra", "set the alpha gain for the red channel",   OFFSET(ra), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "gr", "set the red gain for the green channel",   OFFSET(gr), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "gg", "set the green gain for the green channel", OFFSET(gg), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "gb", "set the blue gain for the green channel",  OFFSET(gb), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ga", "set the alpha gain for the green channel", OFFSET(ga), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "br", "set the red gain for the blue channel",    OFFSET(br), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "bg", "set the green gain for the blue channel",  OFFSET(bg), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "bb", "set the blue gain for the blue channel",   OFFSET(bb), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "ba", "set the alpha gain for the blue channel",  OFFSET(ba), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ar", "set the red gain for the alpha channel",   OFFSET(ar), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ag", "set the green gain for the alpha channel", OFFSET(ag), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ab", "set the blue gain for the alpha channel",  OFFSET(ab), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "aa", "set the alpha gain for the alpha channel", OFFSET(aa), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "pc", "set the preserve color mode",  OFFSET(preserve_color), AV_OPT_TYPE_INT,    {.i64=0},  0, NB_PRESERVE-1, FLAGS, .unit = "preserve" },
    { "none",  "disabled",                     0,                        AV_OPT_TYPE_CONST,  {.i64=P_NONE}, 0, 0, FLAGS, .unit = "preserve" },
    { "lum",   "luminance",                    0,                        AV_OPT_TYPE_CONST,  {.i64=P_LUM},  0, 0, FLAGS, .unit = "preserve" },
    { "max",   "max",                          0,                        AV_OPT_TYPE_CONST,  {.i64=P_MAX},  0, 0, FLAGS, .unit = "preserve" },
    { "avg",   "average",                      0,                        AV_OPT_TYPE_CONST,  {.i64=P_AVG},  0, 0, FLAGS, .unit = "preserve" },
    { "sum",   "sum",                          0,                        AV_OPT_TYPE_CONST,  {.i64=P_SUM},  0, 0, FLAGS, .unit = "preserve" },
    { "nrm",   "norm",                         0,                        AV_OPT_TYPE_CONST,  {.i64=P_NRM},  0, 0, FLAGS, .unit = "preserve" },
    { "pwr",   "power",                        0,                        AV_OPT_TYPE_CONST,  {.i64=P_PWR},  0, 0, FLAGS, .unit = "preserve" },
    { "pa", "set the preserve color amount",    OFFSET(preserve_amount), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorchannelmixer);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
    AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
    AV_PIX_FMT_GBRP14,
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};

static int filter_slice_gbrp(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_8(ctx, arg, jobnr, nb_jobs, 0, 8, 0);
}

static int filter_slice_gbrap(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_8(ctx, arg, jobnr, nb_jobs, 1, 8, 0);
}

static int filter_slice_gbrp_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_8(ctx, arg, jobnr, nb_jobs, 0, 8, 1);
}

static int filter_slice_gbrap_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_8(ctx, arg, jobnr, nb_jobs, 1, 8, 1);
}

static int filter_slice_gbrp9(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 9, 0);
}

static int filter_slice_gbrp10(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 10, 0);
}

static int filter_slice_gbrap10(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 10, 0);
}

static int filter_slice_gbrp12(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 12, 0);
}

static int filter_slice_gbrap12(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 12, 0);
}

static int filter_slice_gbrp14(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 14, 0);
}

static int filter_slice_gbrp16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 16, 0);
}

static int filter_slice_gbrap16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 16, 0);
}

static int filter_slice_gbrp9_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 9, 1);
}

static int filter_slice_gbrp10_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 10, 1);
}

static int filter_slice_gbrap10_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 10, 1);
}

static int filter_slice_gbrp12_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 12, 1);
}

static int filter_slice_gbrap12_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 12, 1);
}

static int filter_slice_gbrp14_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 14, 1);
}

static int filter_slice_gbrp16_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 0, 16, 1);
}

static int filter_slice_gbrap16_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_16(ctx, arg, jobnr, nb_jobs, 1, 16, 1);
}

static int filter_slice_rgba64(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_16(ctx, arg, jobnr, nb_jobs, 1, 4, 0, 16);
}

static int filter_slice_rgb48(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_16(ctx, arg, jobnr, nb_jobs, 0, 3, 0, 16);
}

static int filter_slice_rgba64_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_16(ctx, arg, jobnr, nb_jobs, 1, 4, 1, 16);
}

static int filter_slice_rgb48_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_16(ctx, arg, jobnr, nb_jobs, 0, 3, 1, 16);
}

static int filter_slice_rgba(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, 1, 4, 0, 8);
}

static int filter_slice_rgb24(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, 0, 3, 0, 8);
}

static int filter_slice_rgb0(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, -1, 4, 0, 8);
}

static int filter_slice_rgba_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, 1, 4, 1, 8);
}

static int filter_slice_rgb24_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, 0, 3, 1, 8);
}

static int filter_slice_rgb0_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_packed_8(ctx, arg, jobnr, nb_jobs, -1, 4, 1, 8);
}

static int filter_slice_gbrp32(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_32(ctx, arg, jobnr, nb_jobs, 0, 1, 0);
}

static int filter_slice_gbrap32(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_32(ctx, arg, jobnr, nb_jobs, 1, 1, 0);
}

static int filter_slice_gbrp32_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_32(ctx, arg, jobnr, nb_jobs, 0, 1, 1);
}

static int filter_slice_gbrap32_pl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return filter_slice_rgba_planar_32(ctx, arg, jobnr, nb_jobs, 1, 1, 1);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ColorChannelMixerContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    const int depth = desc->comp[0].depth;
    int i, j, size, *buffer = s->buffer;

    ff_fill_rgba_map(s->rgba_map, outlink->format);

    size = 1 << depth;
    if (!s->buffer) {
        s->buffer = buffer = av_malloc(16 * size * sizeof(*s->buffer));
        if (!s->buffer)
            return AVERROR(ENOMEM);

        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++, buffer += size)
                s->lut[i][j] = buffer;
    }

    for (i = 0; i < size; i++) {
        s->lut[R][R][i] = lrint(i * s->rr);
        s->lut[R][G][i] = lrint(i * s->rg);
        s->lut[R][B][i] = lrint(i * s->rb);
        s->lut[R][A][i] = lrint(i * s->ra);

        s->lut[G][R][i] = lrint(i * s->gr);
        s->lut[G][G][i] = lrint(i * s->gg);
        s->lut[G][B][i] = lrint(i * s->gb);
        s->lut[G][A][i] = lrint(i * s->ga);

        s->lut[B][R][i] = lrint(i * s->br);
        s->lut[B][G][i] = lrint(i * s->bg);
        s->lut[B][B][i] = lrint(i * s->bb);
        s->lut[B][A][i] = lrint(i * s->ba);

        s->lut[A][R][i] = lrint(i * s->ar);
        s->lut[A][G][i] = lrint(i * s->ag);
        s->lut[A][B][i] = lrint(i * s->ab);
        s->lut[A][A][i] = lrint(i * s->aa);
    }

    switch (outlink->format) {
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
        s->filter_slice[0] = filter_slice_rgb24;
        s->filter_slice[1] = filter_slice_rgb24_pl;
        break;
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGB0:
        s->filter_slice[0] = filter_slice_rgb0;
        s->filter_slice[1] = filter_slice_rgb0_pl;
        break;
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGBA:
        s->filter_slice[0] = filter_slice_rgba;
        s->filter_slice[1] = filter_slice_rgba_pl;
        break;
    case AV_PIX_FMT_BGR48:
    case AV_PIX_FMT_RGB48:
        s->filter_slice[0] = filter_slice_rgb48;
        s->filter_slice[1] = filter_slice_rgb48_pl;
        break;
    case AV_PIX_FMT_BGRA64:
    case AV_PIX_FMT_RGBA64:
        s->filter_slice[0] = filter_slice_rgba64;
        s->filter_slice[1] = filter_slice_rgba64_pl;
        break;
    case AV_PIX_FMT_GBRP:
        s->filter_slice[0] = filter_slice_gbrp;
        s->filter_slice[1] = filter_slice_gbrp_pl;
        break;
    case AV_PIX_FMT_GBRAP:
        s->filter_slice[0] = filter_slice_gbrap;
        s->filter_slice[1] = filter_slice_gbrap_pl;
        break;
    case AV_PIX_FMT_GBRP9:
        s->filter_slice[0] = filter_slice_gbrp9;
        s->filter_slice[1] = filter_slice_gbrp9_pl;
        break;
    case AV_PIX_FMT_GBRP10:
        s->filter_slice[0] = filter_slice_gbrp10;
        s->filter_slice[1] = filter_slice_gbrp10_pl;
        break;
    case AV_PIX_FMT_GBRAP10:
        s->filter_slice[0] = filter_slice_gbrap10;
        s->filter_slice[1] = filter_slice_gbrap10_pl;
        break;
    case AV_PIX_FMT_GBRP12:
        s->filter_slice[0] = filter_slice_gbrp12;
        s->filter_slice[1] = filter_slice_gbrp12_pl;
        break;
    case AV_PIX_FMT_GBRAP12:
        s->filter_slice[0] = filter_slice_gbrap12;
        s->filter_slice[1] = filter_slice_gbrap12_pl;
        break;
    case AV_PIX_FMT_GBRP14:
        s->filter_slice[0] = filter_slice_gbrp14;
        s->filter_slice[1] = filter_slice_gbrp14_pl;
        break;
    case AV_PIX_FMT_GBRP16:
        s->filter_slice[0] = filter_slice_gbrp16;
        s->filter_slice[1] = filter_slice_gbrp16_pl;
        break;
    case AV_PIX_FMT_GBRAP16:
        s->filter_slice[0] = filter_slice_gbrap16;
        s->filter_slice[1] = filter_slice_gbrap16_pl;
        break;
    case AV_PIX_FMT_GBRPF32:
        s->filter_slice[0] = filter_slice_gbrp32;
        s->filter_slice[1] = filter_slice_gbrp32_pl;
        break;
    case AV_PIX_FMT_GBRAPF32:
        s->filter_slice[0] = filter_slice_gbrap32;
        s->filter_slice[1] = filter_slice_gbrap32_pl;
        break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorChannelMixerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const int pc = s->preserve_color > 0;
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.in = in;
    td.out = out;
    ff_filter_execute(ctx, s->filter_slice[pc], &td, NULL,
                      FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);

    if (ret < 0)
        return ret;

    return config_output(ctx->outputs[0]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorChannelMixerContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad colorchannelmixer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad colorchannelmixer_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_vf_colorchannelmixer = {
    .name          = "colorchannelmixer",
    .description   = NULL_IF_CONFIG_SMALL("Adjust colors by mixing color channels."),
    .priv_size     = sizeof(ColorChannelMixerContext),
    .priv_class    = &colorchannelmixer_class,
    .uninit        = uninit,
    FILTER_INPUTS(colorchannelmixer_inputs),
    FILTER_OUTPUTS(colorchannelmixer_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
