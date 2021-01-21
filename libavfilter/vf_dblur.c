/*
 * Copyright (c) 2020 Paul B Mahol
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
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct DBlurContext {
    const AVClass *class;

    float angle;
    float radius;
    int planes;

    float b0, b1, q, c, R3;

    int depth;
    int planewidth[4];
    int planeheight[4];
    float *buffer;
    int nb_planes;
} DBlurContext;

#define OFFSET(x) offsetof(DBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption dblur_options[] = {
    { "angle",  "set angle",            OFFSET(angle),  AV_OPT_TYPE_FLOAT, {.dbl=45},  0.0,  360, FLAGS },
    { "radius", "set radius",           OFFSET(radius), AV_OPT_TYPE_FLOAT, {.dbl=5},     1, 8192, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=0xF},   0,  0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dblur);

#define f(n, m) (dst[(n) * width + (m)])

static int filter_horizontally(AVFilterContext *ctx, int width, int height)
{
    DBlurContext *s = ctx->priv;
    const float b0 = s->b0;
    const float b1 = s->b1;
    const float q = s->q;
    const float c = s->c;
    float *dst = s->buffer;
    float g;

    if (s->R3 > 0) {
        for (int y = 1; y < height - 1; y++) {
            g = q * f(0, 0) + c * f(0, 0);
            for (int x = 0; x < width; x++) {
                f(y, x) = b0 * f(y, x) + b1 * f(y - 1, x) + g;
                g = q * f(y, x) + c * f(y - 1, x);
            }
        }

        for (int y = height - 2; y >= 0; y--) {
            g = q * f(y, width - 1) + c * f(y, width - 1);
            for (int x = width - 1; x >= 0; x--) {
                f(y, x) = b0 * f(y, x) + b1 * f(y + 1, x) + g;
                g = q * f(y, x) + c * f(y + 1, x);
            }
        }
    } else {
        for (int y = 1; y < height - 1; y++) {
            g = q * f(0, width - 1) + c * f(0, width - 1);
            for (int x = width - 1; x >= 0; x--) {
                f(y, x) = b0 * f(y, x) + b1 * f(y - 1, x) + g;
                g = q * f(y, x) + c * f(y - 1, x);
            }
        }

        for (int y = height - 2; y >= 0; y--) {
            g = q * f(y, 0) + c * f(y, 0);
            for (int x = 0; x < width; x++) {
                f(y, x) = b0 * f(y, x) + b1 * f(y + 1, x) + g;
                g = q * f(y, x) + c * f(y + 1, x);
            }
        }
    }

    return 0;
}

static void diriir2d(AVFilterContext *ctx, int plane)
{
    DBlurContext *s = ctx->priv;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];

    filter_horizontally(ctx, width, height);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    DBlurContext *s = inlink->dst->priv;

    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->buffer = av_malloc_array(FFALIGN(inlink->w, 16), FFALIGN(inlink->h, 16) * sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    return 0;
}

static void set_params(DBlurContext *s, float angle, float r)
{
    float mu, nu, R1, R2, w1, w2;
    float a0, a1, a2, a3;

    angle = angle * M_PI / 180.f;

    mu = cosf(angle);
    nu = sinf(angle);
    R1 = (mu * r) * (mu * r);
    R2 = (nu * r) * (nu * r);
    s->R3 = mu * nu * r * r;
    w1 = sqrtf(0.25f + R1);
    w2 = sqrtf(0.25f + R2);
    a0 = (w1 + 0.5f) * (w2 + 0.5f) - fabsf(s->R3);
    a1 = 0.5f + w2 - a0;
    a2 = 0.5f + w1 - a0;
    a3 = a0 - w1 - w2;
    s->b0 = 1.f / a0;
    s->b1 = -a2 / a0;
    s->q = -a1 / a0;
    s->c = -a3 / a0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    DBlurContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    set_params(s, s->angle, s->radius);

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

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int width = s->planewidth[plane];
        float *bptr = s->buffer;
        const uint8_t *src = in->data[plane];
        const uint16_t *src16 = (const uint16_t *)in->data[plane];
        uint8_t *dst = out->data[plane];
        uint16_t *dst16 = (uint16_t *)out->data[plane];
        int y, x;

        if (!(s->planes & (1 << plane))) {
            if (out != in)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    width * ((s->depth + 7) / 8), height);
            continue;
        }

        if (s->depth == 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    bptr[x] = src[x];
                }
                bptr += width;
                src += in->linesize[plane];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    bptr[x] = src16[x];
                }
                bptr += width;
                src16 += in->linesize[plane] / 2;
            }
        }

        diriir2d(ctx, plane);

        bptr = s->buffer;
        if (s->depth == 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    dst[x] = bptr[x];
                }
                bptr += width;
                dst += out->linesize[plane];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    dst16[x] = bptr[x];
                }
                bptr += width;
                dst16 += out->linesize[plane] / 2;
            }
        }
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DBlurContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad dblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad dblur_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_dblur = {
    .name          = "dblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Directional Blur filter."),
    .priv_size     = sizeof(DBlurContext),
    .priv_class    = &dblur_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = dblur_inputs,
    .outputs       = dblur_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = ff_filter_process_command,
};
