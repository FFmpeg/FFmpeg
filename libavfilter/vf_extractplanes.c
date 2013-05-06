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
} ExtractPlanesContext;

#define OFFSET(x) offsetof(ExtractPlanesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption extractplanes_options[] = {
    { "planes", "set planes",  OFFSET(requested_planes), AV_OPT_TYPE_FLAGS, {.i64=1}, 1, 0xff, FLAGS, "flags"},
    {      "y", "set luma plane",  0, AV_OPT_TYPE_CONST, {.i64=PLANE_Y}, 0, 0, FLAGS, "flags"},
    {      "u", "set u plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_U}, 0, 0, FLAGS, "flags"},
    {      "v", "set v plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_V}, 0, 0, FLAGS, "flags"},
    {      "g", "set green plane", 0, AV_OPT_TYPE_CONST, {.i64=PLANE_G}, 0, 0, FLAGS, "flags"},
    {      "r", "set red plane",   0, AV_OPT_TYPE_CONST, {.i64=PLANE_R}, 0, 0, FLAGS, "flags"},
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
        !ctx->inputs[0]->in_formats->format_count) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats)
        ff_formats_ref(ff_make_format_list(in_pixfmts), &ctx->inputs[0]->out_formats);

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    depth = desc->comp[0].depth_minus1;
    be = desc->flags & PIX_FMT_BE;
    for (i = 1; i < avff->format_count; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (depth != desc->comp[0].depth_minus1 ||
            be    != (desc->flags & PIX_FMT_BE)) {
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
    int plane_avail, ret;

    plane_avail = ((desc->flags & PIX_FMT_RGB) ? PLANE_R|PLANE_G|PLANE_B :
                                                 PLANE_Y |
                                ((desc->nb_components > 2) ? PLANE_U|PLANE_V : 0)) |
                  ((desc->flags & PIX_FMT_ALPHA) ? PLANE_A : 0);
    if (e->requested_planes & ~plane_avail) {
        av_log(ctx, AV_LOG_ERROR, "Requested planes not available.\n");
        return AVERROR(EINVAL);
    }
    if ((ret = av_image_fill_linesizes(e->linesize, inlink->format, inlink->w)) < 0)
        return ret;

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
        outlink->h = inlink->h >> desc->log2_chroma_h;
        outlink->w = inlink->w >> desc->log2_chroma_w;
    }

    return 0;
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

        if (outlink->closed || !frame->data[idx])
            continue;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_frame_copy_props(out, frame);

        av_image_copy_plane(out->data[0], out->linesize[0],
                            frame->data[idx], frame->linesize[idx],
                            e->linesize[idx], outlink->h);
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

static int init(AVFilterContext *ctx)
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

AVFilter avfilter_vf_extractplanes = {
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
