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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#define FF_INTERNAL_FIELDS 1
#include "libavfilter/framequeue.h"

#include "avfilter.h"
#include "drawutils.h"
#include "internal.h"

#define PLANE_R 0x01
#define PLANE_G 0x02
#define PLANE_B 0x04
#define PLANE_A 0x08
#define PLANE_Y 0x10
#define PLANE_U 0x20
#define PLANE_V 0x40

typedef struct ExtractPlanesContext {
    const AVClass *class;
    int requested_planes;
    int map[4];
    int linesize[4];
    int is_packed;
    int depth;
    int step;
} ExtractPlanesContext;

#define OFFSET(x) offsetof(ExtractPlanesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption extractplanes_options[] = {
    { "planes", "set planes",  OFFSET(requested_planes), AV_OPT_TYPE_FLAGS, {.i64=1}, 1, 0xff, FLAGS, "flags"},
    {      "y", "set luma plane",  0, AV_OPT_TYPE_CONST, {.i64=PLANE_Y}, 0, 0, FLAGS, "flags"},
    {      "u", "set u plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_U}, 0, 0, FLAGS, "flags"},
    {      "v", "set v plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_V}, 0, 0, FLAGS, "flags"},
    {      "r", "set red plane",   0, AV_OPT_TYPE_CONST, {.i64=PLANE_R}, 0, 0, FLAGS, "flags"},
    {      "g", "set green plane", 0, AV_OPT_TYPE_CONST, {.i64=PLANE_G}, 0, 0, FLAGS, "flags"},
    {      "b", "set blue plane",  0, AV_OPT_TYPE_CONST, {.i64=PLANE_B}, 0, 0, FLAGS, "flags"},
    {      "a", "set alpha plane", 0, AV_OPT_TYPE_CONST, {.i64=PLANE_A}, 0, 0, FLAGS, "flags"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(extractplanes);

#define EIGHTBIT_FORMATS                           \
        AV_PIX_FMT_YUV410P,                        \
        AV_PIX_FMT_YUV411P,                        \
        AV_PIX_FMT_YUV440P,                        \
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P,   \
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA422P,   \
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,  \
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,  \
        AV_PIX_FMT_YUVJ411P,                       \
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,   \
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,       \
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,        \
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA,          \
        AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,          \
        AV_PIX_FMT_RGB0, AV_PIX_FMT_BGR0,          \
        AV_PIX_FMT_0RGB, AV_PIX_FMT_0BGR,          \
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP

#define HIGHDEPTH_FORMATS(suf)                                 \
        AV_PIX_FMT_YA16##suf, AV_PIX_FMT_GRAY16##suf,          \
        AV_PIX_FMT_YUV420P16##suf, AV_PIX_FMT_YUVA420P16##suf, \
        AV_PIX_FMT_YUV422P16##suf, AV_PIX_FMT_YUVA422P16##suf, \
        AV_PIX_FMT_YUV444P16##suf, AV_PIX_FMT_YUVA444P16##suf, \
        AV_PIX_FMT_RGB48##suf, AV_PIX_FMT_BGR48##suf,          \
        AV_PIX_FMT_RGBA64##suf, AV_PIX_FMT_BGRA64##suf,        \
        AV_PIX_FMT_GBRP16##suf, AV_PIX_FMT_GBRAP16##suf,       \
        AV_PIX_FMT_YUV420P10##suf,                             \
        AV_PIX_FMT_YUV422P10##suf,                             \
        AV_PIX_FMT_YUV444P10##suf,                             \
        AV_PIX_FMT_YUV440P10##suf,                             \
        AV_PIX_FMT_YUVA420P10##suf,                            \
        AV_PIX_FMT_YUVA422P10##suf,                            \
        AV_PIX_FMT_YUVA444P10##suf,                            \
        AV_PIX_FMT_YUV420P12##suf,                             \
        AV_PIX_FMT_YUV422P12##suf,                             \
        AV_PIX_FMT_YUV444P12##suf,                             \
        AV_PIX_FMT_YUV440P12##suf,                             \
        AV_PIX_FMT_GBRP10##suf, AV_PIX_FMT_GBRAP10##suf,       \
        AV_PIX_FMT_GBRP12##suf, AV_PIX_FMT_GBRAP12##suf,       \
        AV_PIX_FMT_YUV420P9##suf,                              \
        AV_PIX_FMT_YUV422P9##suf,                              \
        AV_PIX_FMT_YUV444P9##suf,                              \
        AV_PIX_FMT_YUVA420P9##suf,                             \
        AV_PIX_FMT_YUVA422P9##suf,                             \
        AV_PIX_FMT_YUVA444P9##suf,                             \
        AV_PIX_FMT_GBRP9##suf,                                 \
        AV_PIX_FMT_GBRP14##suf,                                \
        AV_PIX_FMT_YUV420P14##suf,                             \
        AV_PIX_FMT_YUV422P14##suf,                             \
        AV_PIX_FMT_YUV444P14##suf

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_pixfmts_le[] = {
        EIGHTBIT_FORMATS,
        HIGHDEPTH_FORMATS(LE),
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat in_pixfmts_be[] = {
        EIGHTBIT_FORMATS,
        HIGHDEPTH_FORMATS(BE),
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat out8_pixfmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out9le_pixfmts[] = { AV_PIX_FMT_GRAY9LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out9be_pixfmts[] = { AV_PIX_FMT_GRAY9BE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out10le_pixfmts[] = { AV_PIX_FMT_GRAY10LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out10be_pixfmts[] = { AV_PIX_FMT_GRAY10BE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out12le_pixfmts[] = { AV_PIX_FMT_GRAY12LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out12be_pixfmts[] = { AV_PIX_FMT_GRAY12BE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out14le_pixfmts[] = { AV_PIX_FMT_GRAY14LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out14be_pixfmts[] = { AV_PIX_FMT_GRAY14BE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out16le_pixfmts[] = { AV_PIX_FMT_GRAY16LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out16be_pixfmts[] = { AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_NONE };
    const enum AVPixelFormat *out_pixfmts, *in_pixfmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int i, ret, depth = 0, be = 0;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    depth = desc->comp[0].depth;
    be = desc->flags & AV_PIX_FMT_FLAG_BE;
    if (be) {
        in_pixfmts = in_pixfmts_be;
    } else {
        in_pixfmts = in_pixfmts_le;
    }
    if (!ctx->inputs[0]->out_formats)
        if ((ret = ff_formats_ref(ff_make_format_list(in_pixfmts), &ctx->inputs[0]->out_formats)) < 0)
            return ret;

    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (depth != desc->comp[0].depth ||
            be    != (desc->flags & AV_PIX_FMT_FLAG_BE)) {
            return AVERROR(EAGAIN);
        }
    }

    if (depth == 8)
        out_pixfmts = out8_pixfmts;
    else if (!be && depth == 9)
        out_pixfmts = out9le_pixfmts;
    else if (be && depth == 9)
        out_pixfmts = out9be_pixfmts;
    else if (!be && depth == 10)
        out_pixfmts = out10le_pixfmts;
    else if (be && depth == 10)
        out_pixfmts = out10be_pixfmts;
    else if (!be && depth == 12)
        out_pixfmts = out12le_pixfmts;
    else if (be && depth == 12)
        out_pixfmts = out12be_pixfmts;
    else if (!be && depth == 14)
        out_pixfmts = out14le_pixfmts;
    else if (be && depth == 14)
        out_pixfmts = out14be_pixfmts;
    else if (be)
        out_pixfmts = out16be_pixfmts;
    else
        out_pixfmts = out16le_pixfmts;

    for (i = 0; i < ctx->nb_outputs; i++)
        if ((ret = ff_formats_ref(ff_make_format_list(out_pixfmts), &ctx->outputs[i]->in_formats)) < 0)
            return ret;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ExtractPlanesContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int plane_avail, ret, i;
    uint8_t rgba_map[4];

    plane_avail = ((desc->flags & AV_PIX_FMT_FLAG_RGB) ? PLANE_R|PLANE_G|PLANE_B :
                                                 PLANE_Y |
                                ((desc->nb_components > 2) ? PLANE_U|PLANE_V : 0)) |
                  ((desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? PLANE_A : 0);
    if (s->requested_planes & ~plane_avail) {
        av_log(ctx, AV_LOG_ERROR, "Requested planes not available.\n");
        return AVERROR(EINVAL);
    }
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->depth = desc->comp[0].depth >> 3;
    s->step = av_get_padded_bits_per_pixel(desc) >> 3;
    s->is_packed = !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
                    (desc->nb_components > 1);
    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        ff_fill_rgba_map(rgba_map, inlink->format);
        for (i = 0; i < 4; i++)
            s->map[i] = rgba_map[s->map[i]];
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ExtractPlanesContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int output = outlink->srcpad - ctx->output_pads;

    if (s->map[output] == 1 || s->map[output] == 2) {
        outlink->h = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
        outlink->w = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    }

    return 0;
}

static void extract_from_packed(uint8_t *dst, int dst_linesize,
                                const uint8_t *src, int src_linesize,
                                int width, int height,
                                int depth, int step, int comp)
{
    int x, y;

    for (y = 0; y < height; y++) {
        switch (depth) {
        case 1:
            for (x = 0; x < width; x++)
                dst[x] = src[x * step + comp];
            break;
        case 2:
            for (x = 0; x < width; x++) {
                dst[x * 2    ] = src[x * step + comp * 2    ];
                dst[x * 2 + 1] = src[x * step + comp * 2 + 1];
            }
            break;
        }
        dst += dst_linesize;
        src += src_linesize;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ExtractPlanesContext *s = ctx->priv;
    int i, eof = 0, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFilterLink *outlink = ctx->outputs[i];
        const int idx = s->map[i];
        AVFrame *out;

        if (outlink->status_in)
            continue;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_frame_copy_props(out, frame);

        if (s->is_packed) {
            extract_from_packed(out->data[0], out->linesize[0],
                                frame->data[0], frame->linesize[0],
                                outlink->w, outlink->h,
                                s->depth,
                                s->step, idx);
        } else {
            av_image_copy_plane(out->data[0], out->linesize[0],
                                frame->data[idx], frame->linesize[idx],
                                s->linesize[idx], outlink->h);
        }

        ret = ff_filter_frame(outlink, out);
        if (ret == AVERROR_EOF)
            eof++;
        else if (ret < 0)
            break;
    }
    av_frame_free(&frame);

    if (eof == ctx->nb_outputs)
        ret = AVERROR_EOF;
    else if (ret == AVERROR_EOF)
        ret = 0;
    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    ExtractPlanesContext *s = ctx->priv;
    int planes = (s->requested_planes & 0xf) | (s->requested_planes >> 4);
    int i, ret;

    for (i = 0; i < 4; i++) {
        char *name;
        AVFilterPad pad = { 0 };

        if (!(planes & (1 << i)))
            continue;

        name = av_asprintf("out%d", ctx->nb_outputs);
        if (!name)
            return AVERROR(ENOMEM);
        s->map[ctx->nb_outputs] = i;
        pad.name = name;
        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.config_props = config_output;

        if ((ret = ff_insert_outpad(ctx, ctx->nb_outputs, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
}

static const AVFilterPad extractplanes_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

AVFilter ff_vf_extractplanes = {
    .name          = "extractplanes",
    .description   = NULL_IF_CONFIG_SMALL("Extract planes as grayscale frames."),
    .priv_size     = sizeof(ExtractPlanesContext),
    .priv_class    = &extractplanes_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = extractplanes_inputs,
    .outputs       = NULL,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};

#if CONFIG_ALPHAEXTRACT_FILTER

static av_cold int init_alphaextract(AVFilterContext *ctx)
{
    ExtractPlanesContext *s = ctx->priv;

    s->requested_planes = PLANE_A;

    return init(ctx);
}

AVFilter ff_vf_alphaextract = {
    .name           = "alphaextract",
    .description    = NULL_IF_CONFIG_SMALL("Extract an alpha channel as a "
                      "grayscale image component."),
    .priv_size      = sizeof(ExtractPlanesContext),
    .init           = init_alphaextract,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .inputs         = extractplanes_inputs,
    .outputs        = NULL,
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
#endif  /* CONFIG_ALPHAEXTRACT_FILTER */
