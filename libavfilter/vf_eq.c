/*
 * Original MPlayer filters by Richard Felker, Hampa Hug, Daniel Moreno,
 * and Michael Niedermeyer.
 *
 * Copyright (c) 2014 James Darnley <james.darnley@gmail.com>
 * Copyright (c) 2015 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * very simple video equalizer
 */

/**
 * TODO:
 * - Add support to process_command
 */

#include "libavfilter/internal.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "vf_eq.h"

static void create_lut(EQParameters *param)
{
    int i;
    double   g  = 1.0 / param->gamma;
    double   lw = 1.0 - param->gamma_weight;

    for (i = 0; i < 256; i++) {
        double v = i / 255.0;
        v = param->contrast * (v - 0.5) + 0.5 + param->brightness;

        if (v <= 0.0) {
            param->lut[i] = 0;
        } else {
            v = v * lw + pow(v, g) * param->gamma_weight;

            if (v >= 1.0)
                param->lut[i] = 255;
            else
                param->lut[i] = 256.0 * v;
        }
    }

    param->lut_clean = 1;
}

static void apply_lut(EQParameters *param, uint8_t *dst, int dst_stride,
                      const uint8_t *src, int src_stride, int w, int h)
{
    int x, y;

    if (!param->lut_clean)
        create_lut(param);

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[y * dst_stride + x] = param->lut[src[y * src_stride + x]];
        }
    }
}

static void process_c(EQParameters *param, uint8_t *dst, int dst_stride,
                      const uint8_t *src, int src_stride, int w, int h)
{
    int x, y, pel;

    int contrast = (int) (param->contrast * 256 * 16);
    int brightness = ((int) (100.0 * param->brightness + 100.0) * 511) / 200 - 128 - contrast / 32;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            pel = ((src[y * src_stride + x] * contrast) >> 12) + brightness;

            if (pel & ~255)
                pel = (-pel) >> 31;

            dst[y * dst_stride + x] = pel;
        }
    }
}

static void check_values(EQParameters *param, EQContext *eq)
{
    if (param->contrast == 1.0 && param->brightness == 0.0 && param->gamma == 1.0)
        param->adjust = NULL;
    else if (param->gamma == 1.0)
        param->adjust = eq->process;
    else
        param->adjust = apply_lut;
}

static void set_contrast(EQContext *eq)
{
    eq->param[0].contrast = eq->contrast;
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_brightness(EQContext *eq)
{
    eq->param[0].brightness = eq->brightness;
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_gamma(EQContext *eq)
{
    int i;
    eq->param[0].gamma = eq->gamma * eq->gamma_g;
    eq->param[1].gamma = sqrt(eq->gamma_b / eq->gamma_g);
    eq->param[2].gamma = sqrt(eq->gamma_r / eq->gamma_g);

    for (i = 0; i < 3; i++) {
        eq->param[i].gamma_weight = eq->gamma_weight;
        eq->param[i].lut_clean = 0;
        check_values(&eq->param[i], eq);
    }
}

static void set_saturation(EQContext *eq)
{
    int i;
    for (i = 1; i < 3; i++) {
        eq->param[i].contrast = eq->saturation;
        eq->param[i].lut_clean = 0;
        check_values(&eq->param[i], eq);
    }
}

static int initialize(AVFilterContext *ctx)
{
    EQContext *eq = ctx->priv;

    eq->process = process_c;

    if (ARCH_X86)
        ff_eq_init_x86(eq);

    set_gamma(eq);
    set_contrast(eq);
    set_brightness(eq);
    set_saturation(eq);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts_eq[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pixel_fmts_eq));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    EQContext *eq = ctx->priv;
    AVFrame *out;
    const AVPixFmtDescriptor *desc;
    int i;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, in);
    desc = av_pix_fmt_desc_get(inlink->format);

    for (i = 0; i < desc->nb_components; i++) {
        int w = inlink->w;
        int h = inlink->h;

        if (i == 1 || i == 2) {
            w = FF_CEIL_RSHIFT(w, desc->log2_chroma_w);
            h = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);
        }

        if (eq->param[i].adjust)
            eq->param[i].adjust(&eq->param[i], out->data[i], out->linesize[i],
                                 in->data[i], in->linesize[i], w, h);
        else
            av_image_copy_plane(out->data[i], out->linesize[i],
                                in->data[i], in->linesize[i], w, h);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}
static const AVFilterPad eq_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad eq_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(EQContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption eq_options[] = {
    { "contrast",     "set the contrast adjustment, negative values give a negative image",
        OFFSET(contrast),     AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, -2.0, 2.0, FLAGS },
    { "brightness",   "set the brightness adjustment",
        OFFSET(brightness),   AV_OPT_TYPE_DOUBLE, {.dbl = 0.0}, -1.0, 1.0, FLAGS },
    { "saturation",   "set the saturation adjustment",
        OFFSET(saturation),   AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.0, 3.0, FLAGS },
    { "gamma",        "set the initial gamma value",
        OFFSET(gamma),        AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.1, 10.0, FLAGS },
    { "gamma_r",      "gamma value for red",
        OFFSET(gamma_r),      AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.1, 10.0, FLAGS },
    { "gamma_g",      "gamma value for green",
        OFFSET(gamma_g),      AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.1, 10.0, FLAGS },
    { "gamma_b",      "gamma value for blue",
        OFFSET(gamma_b),      AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.1, 10.0, FLAGS },
    { "gamma_weight", "set the gamma weight which reduces the effect of gamma on bright areas",
        OFFSET(gamma_weight), AV_OPT_TYPE_DOUBLE, {.dbl = 1.0},  0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(eq);

AVFilter ff_vf_eq = {
    .name          = "eq",
    .description   = NULL_IF_CONFIG_SMALL("Adjust brightness, contrast, gamma, and saturation."),
    .priv_size     = sizeof(EQContext),
    .priv_class    = &eq_class,
    .inputs        = eq_inputs,
    .outputs       = eq_outputs,
    .query_formats = query_formats,
    .init          = initialize,
};
