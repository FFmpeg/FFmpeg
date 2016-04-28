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

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct {
    double in_min, in_max;
    double out_min, out_max;
} Range;

typedef struct {
    const AVClass *class;
    Range range[4];
    int nb_comp;
    int bpp;
    int step;
    uint8_t rgba_map[4];
    int linesize;
} ColorLevelsContext;

#define OFFSET(x) offsetof(ColorLevelsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption colorlevels_options[] = {
    { "rimin", "set input red black point",    OFFSET(range[R].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "gimin", "set input green black point",  OFFSET(range[G].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "bimin", "set input blue black point",   OFFSET(range[B].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "aimin", "set input alpha black point",  OFFSET(range[A].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "rimax", "set input red white point",    OFFSET(range[R].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "gimax", "set input green white point",  OFFSET(range[G].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "bimax", "set input blue white point",   OFFSET(range[B].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "aimax", "set input alpha white point",  OFFSET(range[A].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "romin", "set output red black point",   OFFSET(range[R].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "gomin", "set output green black point", OFFSET(range[G].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "bomin", "set output blue black point",  OFFSET(range[B].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "aomin", "set output alpha black point", OFFSET(range[A].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "romax", "set output red white point",   OFFSET(range[R].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "gomax", "set output green white point", OFFSET(range[G].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "bomax", "set output blue white point",  OFFSET(range[B].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "aomax", "set output alpha white point", OFFSET(range[A].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorlevels);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_0RGB,  AV_PIX_FMT_0BGR,
        AV_PIX_FMT_ARGB,  AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB48, AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
        AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorLevelsContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->nb_comp = desc->nb_components;
    s->bpp = desc->comp[0].depth >> 3;
    s->step = (av_get_padded_bits_per_pixel(desc) >> 3) / s->bpp;
    s->linesize = inlink->w * s->step;
    ff_fill_rgba_map(s->rgba_map, inlink->format);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorLevelsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const int step = s->step;
    AVFrame *out;
    int x, y, i;

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

    switch (s->bpp) {
    case 1:
        for (i = 0; i < s->nb_comp; i++) {
            Range *r = &s->range[i];
            const uint8_t offset = s->rgba_map[i];
            const uint8_t *srcrow = in->data[0];
            uint8_t *dstrow = out->data[0];
            int imin = lrint(r->in_min  * UINT8_MAX);
            int imax = lrint(r->in_max  * UINT8_MAX);
            int omin = lrint(r->out_min * UINT8_MAX);
            int omax = lrint(r->out_max * UINT8_MAX);
            double coeff;

            if (imin < 0) {
                imin = UINT8_MAX;
                for (y = 0; y < inlink->h; y++) {
                    const uint8_t *src = srcrow;

                    for (x = 0; x < s->linesize; x += step)
                        imin = FFMIN(imin, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }
            if (imax < 0) {
                srcrow = in->data[0];
                imax = 0;
                for (y = 0; y < inlink->h; y++) {
                    const uint8_t *src = srcrow;

                    for (x = 0; x < s->linesize; x += step)
                        imax = FFMAX(imax, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }

            srcrow = in->data[0];
            coeff = (omax - omin) / (double)(imax - imin);
            for (y = 0; y < inlink->h; y++) {
                const uint8_t *src = srcrow;
                uint8_t *dst = dstrow;

                for (x = 0; x < s->linesize; x += step)
                    dst[x + offset] = av_clip_uint8((src[x + offset] - imin) * coeff + omin);
                dstrow += out->linesize[0];
                srcrow += in->linesize[0];
            }
        }
        break;
    case 2:
        for (i = 0; i < s->nb_comp; i++) {
            Range *r = &s->range[i];
            const uint8_t offset = s->rgba_map[i];
            const uint8_t *srcrow = in->data[0];
            uint8_t *dstrow = out->data[0];
            int imin = lrint(r->in_min  * UINT16_MAX);
            int imax = lrint(r->in_max  * UINT16_MAX);
            int omin = lrint(r->out_min * UINT16_MAX);
            int omax = lrint(r->out_max * UINT16_MAX);
            double coeff;

            if (imin < 0) {
                imin = UINT16_MAX;
                for (y = 0; y < inlink->h; y++) {
                    const uint16_t *src = (const uint16_t *)srcrow;

                    for (x = 0; x < s->linesize; x += step)
                        imin = FFMIN(imin, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }
            if (imax < 0) {
                srcrow = in->data[0];
                imax = 0;
                for (y = 0; y < inlink->h; y++) {
                    const uint16_t *src = (const uint16_t *)srcrow;

                    for (x = 0; x < s->linesize; x += step)
                        imax = FFMAX(imax, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }

            srcrow = in->data[0];
            coeff = (omax - omin) / (double)(imax - imin);
            for (y = 0; y < inlink->h; y++) {
                const uint16_t *src = (const uint16_t*)srcrow;
                uint16_t *dst = (uint16_t *)dstrow;

                for (x = 0; x < s->linesize; x += step)
                    dst[x + offset] = av_clip_uint16((src[x + offset] - imin) * coeff + omin);
                dstrow += out->linesize[0];
                srcrow += in->linesize[0];
            }
        }
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad colorlevels_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad colorlevels_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_colorlevels = {
    .name          = "colorlevels",
    .description   = NULL_IF_CONFIG_SMALL("Adjust the color levels."),
    .priv_size     = sizeof(ColorLevelsContext),
    .priv_class    = &colorlevels_class,
    .query_formats = query_formats,
    .inputs        = colorlevels_inputs,
    .outputs       = colorlevels_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
