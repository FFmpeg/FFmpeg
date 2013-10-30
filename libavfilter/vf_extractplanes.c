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

typedef struct {
    const AVClass *class;
    int requested_planes;
    int map[4];
    int linesize[4];
    int is_packed_rgb;
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

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_pixfmts[] = {
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUV420P16LE, AV_PIX_FMT_YUVA420P16LE,
        AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUVA420P16BE,
        AV_PIX_FMT_YUV422P16LE, AV_PIX_FMT_YUVA422P16LE,
        AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUVA422P16BE,
        AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUVA444P16LE,
        AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUVA444P16BE,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,
        AV_PIX_FMT_GRAY16LE, AV_PIX_FMT_GRAY16BE,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGB48LE, AV_PIX_FMT_BGR48LE,
        AV_PIX_FMT_RGB48BE, AV_PIX_FMT_BGR48BE,
        AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_BGRA64LE,
        AV_PIX_FMT_RGBA64BE, AV_PIX_FMT_BGRA64BE,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP16LE, AV_PIX_FMT_GBRP16BE,
        AV_PIX_FMT_GBRAP16LE, AV_PIX_FMT_GBRAP16BE,
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat out8_pixfmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out16le_pixfmts[] = { AV_PIX_FMT_GRAY16LE, AV_PIX_FMT_NONE };
    static const enum AVPixelFormat out16be_pixfmts[] = { AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_NONE };
    const enum AVPixelFormat *out_pixfmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int i, depth = 0, be = 0;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats)
        ff_formats_ref(ff_make_format_list(in_pixfmts), &ctx->inputs[0]->out_formats);

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    depth = desc->comp[0].depth_minus1;
    be = desc->flags & AV_PIX_FMT_FLAG_BE;
    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (depth != desc->comp[0].depth_minus1 ||
            be    != (desc->flags & AV_PIX_FMT_FLAG_BE)) {
            return AVERROR(EAGAIN);
        }
    }

    if (depth == 7)
        out_pixfmts = out8_pixfmts;
    else if (be)
        out_pixfmts = out16be_pixfmts;
    else
        out_pixfmts = out16le_pixfmts;

    for (i = 0; i < ctx->nb_outputs; i++)
        ff_formats_ref(ff_make_format_list(out_pixfmts), &ctx->outputs[i]->in_formats);
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ExtractPlanesContext *e = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int plane_avail, ret, i;
    uint8_t rgba_map[4];

    plane_avail = ((desc->flags & AV_PIX_FMT_FLAG_RGB) ? PLANE_R|PLANE_G|PLANE_B :
                                                 PLANE_Y |
                                ((desc->nb_components > 2) ? PLANE_U|PLANE_V : 0)) |
                  ((desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? PLANE_A : 0);
    if (e->requested_planes & ~plane_avail) {
        av_log(ctx, AV_LOG_ERROR, "Requested planes not available.\n");
        return AVERROR(EINVAL);
    }
    if ((ret = av_image_fill_linesizes(e->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    e->depth = (desc->comp[0].depth_minus1 + 1) >> 3;
    e->step = av_get_padded_bits_per_pixel(desc) >> 3;
    e->is_packed_rgb = !(desc->flags & AV_PIX_FMT_FLAG_PLANAR);
    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        ff_fill_rgba_map(rgba_map, inlink->format);
        for (i = 0; i < 4; i++)
            e->map[i] = rgba_map[e->map[i]];
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ExtractPlanesContext *e = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int output = outlink->srcpad - ctx->output_pads;

    if (e->map[output] == 1 || e->map[output] == 2) {
        outlink->h = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
        outlink->w = FF_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
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
    ExtractPlanesContext *e = ctx->priv;
    int i, eof = 0, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFilterLink *outlink = ctx->outputs[i];
        const int idx = e->map[i];
        AVFrame *out;

        if (outlink->closed)
            continue;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_frame_copy_props(out, frame);

        if (e->is_packed_rgb) {
            extract_from_packed(out->data[0], out->linesize[0],
                                frame->data[0], frame->linesize[0],
                                outlink->w, outlink->h,
                                e->depth,
                                e->step, idx);
        } else {
            av_image_copy_plane(out->data[0], out->linesize[0],
                                frame->data[idx], frame->linesize[idx],
                                e->linesize[idx], outlink->h);
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
    ExtractPlanesContext *e = ctx->priv;
    int planes = (e->requested_planes & 0xf) | (e->requested_planes >> 4);
    int i;

    for (i = 0; i < 4; i++) {
        char *name;
        AVFilterPad pad = { 0 };

        if (!(planes & (1 << i)))
            continue;

        name = av_asprintf("out%d", ctx->nb_outputs);
        if (!name)
            return AVERROR(ENOMEM);
        e->map[ctx->nb_outputs] = i;
        pad.name = name;
        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.config_props = config_output;

        ff_insert_outpad(ctx, ctx->nb_outputs, &pad);
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
    ExtractPlanesContext *e = ctx->priv;

    e->requested_planes = PLANE_A;

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
