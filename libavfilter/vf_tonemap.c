/*
 * Copyright (c) 2017 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * tonemap algorithms
 */

#include <float.h>
#include <stdio.h>

#include "libavutil/csp.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "colorspace.h"
#include "filters.h"
#include "video.h"

enum TonemapAlgorithm {
    TONEMAP_NONE,
    TONEMAP_LINEAR,
    TONEMAP_GAMMA,
    TONEMAP_CLIP,
    TONEMAP_REINHARD,
    TONEMAP_HABLE,
    TONEMAP_MOBIUS,
    TONEMAP_MAX,
};

typedef struct TonemapContext {
    const AVClass *class;

    enum TonemapAlgorithm tonemap;
    double param;
    double desat;
    double peak;

    const AVLumaCoefficients *coeffs;
} TonemapContext;

static av_cold int init(AVFilterContext *ctx)
{
    TonemapContext *s = ctx->priv;

    switch(s->tonemap) {
    case TONEMAP_GAMMA:
        if (isnan(s->param))
            s->param = 1.8f;
        break;
    case TONEMAP_REINHARD:
        if (!isnan(s->param))
            s->param = (1.0f - s->param) / s->param;
        break;
    case TONEMAP_MOBIUS:
        if (isnan(s->param))
            s->param = 0.3f;
        break;
    }

    if (isnan(s->param))
        s->param = 1.0f;

    return 0;
}

static float hable(float in)
{
    float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;
    return (in * (in * a + b * c) + d * e) / (in * (in * a + b) + d * f) - e / f;
}

static float mobius(float in, float j, double peak)
{
    float a, b;

    if (in <= j)
        return in;

    a = -j * j * (peak - 1.0f) / (j * j - 2.0f * j + peak);
    b = (j * j - 2.0f * j * peak + peak) / FFMAX(peak - 1.0f, 1e-6);

    return (b * b + 2.0f * b * j + j * j) / (b - a) * (in + a) / (in + b);
}

#define MIX(x,y,a) (x) * (1 - (a)) + (y) * (a)
static void tonemap(TonemapContext *s, AVFrame *out, const AVFrame *in,
                    const AVPixFmtDescriptor *desc, int x, int y, double peak)
{
    int map[3] = { desc->comp[0].plane, desc->comp[1].plane, desc->comp[2].plane };
    const float *r_in = (const float *)(in->data[map[0]] + x * desc->comp[map[0]].step + y * in->linesize[map[0]]);
    const float *g_in = (const float *)(in->data[map[1]] + x * desc->comp[map[1]].step + y * in->linesize[map[1]]);
    const float *b_in = (const float *)(in->data[map[2]] + x * desc->comp[map[2]].step + y * in->linesize[map[2]]);
    float *r_out = (float *)(out->data[map[0]] + x * desc->comp[map[0]].step + y * out->linesize[map[0]]);
    float *g_out = (float *)(out->data[map[1]] + x * desc->comp[map[1]].step + y * out->linesize[map[1]]);
    float *b_out = (float *)(out->data[map[2]] + x * desc->comp[map[2]].step + y * out->linesize[map[2]]);
    float sig, sig_orig;

    /* load values */
    *r_out = *r_in;
    *g_out = *g_in;
    *b_out = *b_in;

    /* desaturate to prevent unnatural colors */
    if (s->desat > 0) {
        float luma = av_q2d(s->coeffs->cr) * *r_in + av_q2d(s->coeffs->cg) * *g_in + av_q2d(s->coeffs->cb) * *b_in;
        float overbright = FFMAX(luma - s->desat, 1e-6) / FFMAX(luma, 1e-6);
        *r_out = MIX(*r_in, luma, overbright);
        *g_out = MIX(*g_in, luma, overbright);
        *b_out = MIX(*b_in, luma, overbright);
    }

    /* pick the brightest component, reducing the value range as necessary
     * to keep the entire signal in range and preventing discoloration due to
     * out-of-bounds clipping */
    sig = FFMAX(FFMAX3(*r_out, *g_out, *b_out), 1e-6);
    sig_orig = sig;

    switch(s->tonemap) {
    default:
    case TONEMAP_NONE:
        // do nothing
        break;
    case TONEMAP_LINEAR:
        sig = sig * s->param / peak;
        break;
    case TONEMAP_GAMMA:
        sig = sig > 0.05f ? pow(sig / peak, 1.0f / s->param)
                          : sig * pow(0.05f / peak, 1.0f / s->param) / 0.05f;
        break;
    case TONEMAP_CLIP:
        sig = av_clipf(sig * s->param, 0, 1.0f);
        break;
    case TONEMAP_HABLE:
        sig = hable(sig) / hable(peak);
        break;
    case TONEMAP_REINHARD:
        sig = sig / (sig + s->param) * (peak + s->param) / peak;
        break;
    case TONEMAP_MOBIUS:
        sig = mobius(sig, s->param, peak);
        break;
    }

    /* apply the computed scale factor to the color,
     * linearly to prevent discoloration */
    *r_out *= sig / sig_orig;
    *g_out *= sig / sig_orig;
    *b_out *= sig / sig_orig;
}

typedef struct ThreadData {
    AVFrame *in, *out;
    const AVPixFmtDescriptor *desc;
    double peak;
} ThreadData;

static int tonemap_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    TonemapContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const AVPixFmtDescriptor *desc = td->desc;
    const int slice_start = (in->height * jobnr) / nb_jobs;
    const int slice_end = (in->height * (jobnr+1)) / nb_jobs;
    double peak = td->peak;

    for (int y = slice_start; y < slice_end; y++)
        for (int x = 0; x < out->width; x++)
            tonemap(s, out, in, desc, x, y, peak);

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    TonemapContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    const AVPixFmtDescriptor *odesc = av_pix_fmt_desc_get(outlink->format);
    int ret, x, y;
    double peak = s->peak;

    if (!desc || !odesc) {
        av_frame_free(&in);
        return AVERROR_BUG;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0) {
        av_frame_free(&in);
        av_frame_free(&out);
        return ret;
    }

    /* input and output transfer will be linear */
    if (in->color_trc == AVCOL_TRC_UNSPECIFIED) {
        av_log(s, AV_LOG_WARNING, "Untagged transfer, assuming linear light\n");
        out->color_trc = AVCOL_TRC_LINEAR;
    } else if (in->color_trc != AVCOL_TRC_LINEAR)
        av_log(s, AV_LOG_WARNING, "Tonemapping works on linear light only\n");

    /* read peak from side data if not passed in */
    if (!peak) {
        peak = ff_determine_signal_peak(in);
        av_log(s, AV_LOG_DEBUG, "Computed signal peak: %f\n", peak);
    }

    /* load original color space even if pixel format is RGB to compute overbrights */
    s->coeffs = av_csp_luma_coeffs_from_avcsp(in->colorspace);
    if (s->desat > 0 && (in->colorspace == AVCOL_SPC_UNSPECIFIED || !s->coeffs)) {
        if (in->colorspace == AVCOL_SPC_UNSPECIFIED)
            av_log(s, AV_LOG_WARNING, "Missing color space information, ");
        else if (!s->coeffs)
            av_log(s, AV_LOG_WARNING, "Unsupported color space '%s', ",
                   av_color_space_name(in->colorspace));
        av_log(s, AV_LOG_WARNING, "desaturation is disabled\n");
        s->desat = 0;
    }

    /* do the tone map */
    td.out = out;
    td.in = in;
    td.desc = desc;
    td.peak = peak;
    ff_filter_execute(ctx, tonemap_slice, &td, NULL,
                      FFMIN(in->height, ff_filter_get_nb_threads(ctx)));

    /* copy/generate alpha if needed */
    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA && odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        av_image_copy_plane(out->data[3], out->linesize[3],
                            in->data[3], in->linesize[3],
                            out->linesize[3], outlink->h);
    } else if (odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        for (y = 0; y < out->height; y++) {
            for (x = 0; x < out->width; x++) {
                AV_WN32(out->data[3] + x * odesc->comp[3].step + y * out->linesize[3],
                        av_float2int(1.0f));
            }
        }
    }

    av_frame_free(&in);

    ff_update_hdr_metadata(out, peak);

    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(TonemapContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption tonemap_options[] = {
    { "tonemap",      "tonemap algorithm selection", OFFSET(tonemap), AV_OPT_TYPE_INT, {.i64 = TONEMAP_NONE}, TONEMAP_NONE, TONEMAP_MAX - 1, FLAGS, .unit = "tonemap" },
    {     "none",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_NONE},              0, 0, FLAGS, .unit = "tonemap" },
    {     "linear",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_LINEAR},            0, 0, FLAGS, .unit = "tonemap" },
    {     "gamma",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_GAMMA},             0, 0, FLAGS, .unit = "tonemap" },
    {     "clip",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_CLIP},              0, 0, FLAGS, .unit = "tonemap" },
    {     "reinhard", 0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_REINHARD},          0, 0, FLAGS, .unit = "tonemap" },
    {     "hable",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_HABLE},             0, 0, FLAGS, .unit = "tonemap" },
    {     "mobius",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_MOBIUS},            0, 0, FLAGS, .unit = "tonemap" },
    { "param",        "tonemap parameter", OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl = NAN}, DBL_MIN, DBL_MAX, FLAGS },
    { "desat",        "desaturation strength", OFFSET(desat), AV_OPT_TYPE_DOUBLE, {.dbl = 2}, 0, DBL_MAX, FLAGS },
    { "peak",         "signal peak override", OFFSET(peak), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tonemap);

static const AVFilterPad tonemap_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_tonemap = {
    .p.name          = "tonemap",
    .p.description   = NULL_IF_CONFIG_SMALL("Conversion to/from different dynamic ranges."),
    .p.priv_class    = &tonemap_class,
    .p.flags         = AVFILTER_FLAG_SLICE_THREADS,
    .init            = init,
    .priv_size       = sizeof(TonemapContext),
    FILTER_INPUTS(tonemap_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS(AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32),
};
