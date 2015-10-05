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
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

enum EdgeMode {
    EDGE_BLANK,
    EDGE_SMEAR,
    EDGE_WRAP,
    EDGE_NB
};

typedef struct DisplaceContext {
    const AVClass *class;
    int width[4], height[4];
    enum EdgeMode edge;
    int nb_planes;
    int nb_components;
    int step;
    uint8_t blank[4];
    FFFrameSync fs;

    void (*displace)(struct DisplaceContext *s, const AVFrame *in,
                     const AVFrame *xpic, const AVFrame *ypic, AVFrame *out);
} DisplaceContext;

#define OFFSET(x) offsetof(DisplaceContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption displace_options[] = {
    { "edge", "set edge mode", OFFSET(edge), AV_OPT_TYPE_INT, {.i64=EDGE_SMEAR}, 0, EDGE_NB-1, FLAGS, "edge" },
    {   "blank", "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_BLANK}, 0, 0, FLAGS, "edge" },
    {   "smear", "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_SMEAR}, 0, 0, FLAGS, "edge" },
    {   "wrap" , "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_WRAP},  0, 0, FLAGS, "edge" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(displace);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR, AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA,
        AV_PIX_FMT_0RGB, AV_PIX_FMT_0BGR, AV_PIX_FMT_RGB0, AV_PIX_FMT_BGR0,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static void displace_planar(DisplaceContext *s, const AVFrame *in,
                            const AVFrame *xpic, const AVFrame *ypic,
                            AVFrame *out)
{
    int plane, x, y;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int h = s->height[plane];
        const int w = s->width[plane];
        const int dlinesize = out->linesize[plane];
        const int slinesize = in->linesize[plane];
        const int xlinesize = xpic->linesize[plane];
        const int ylinesize = ypic->linesize[plane];
        const uint8_t *src = in->data[plane];
        const uint8_t *ysrc = ypic->data[plane];
        const uint8_t *xsrc = xpic->data[plane];
        uint8_t *dst = out->data[plane];
        const uint8_t blank = s->blank[plane];

        for (y = 0; y < h; y++) {
            switch (s->edge) {
            case EDGE_BLANK:
                for (x = 0; x < w; x++) {
                    int Y = y + ysrc[x] - 128;
                    int X = x + xsrc[x] - 128;

                    if (Y < 0 || Y >= h || X < 0 || X >= w)
                        dst[x] = blank;
                    else
                        dst[x] = src[Y * slinesize + X];
                }
                break;
            case EDGE_SMEAR:
                for (x = 0; x < w; x++) {
                    int Y = av_clip(y + ysrc[x] - 128, 0, h - 1);
                    int X = av_clip(x + xsrc[x] - 128, 0, w - 1);
                    dst[x] = src[Y * slinesize + X];
                }
                break;
            case EDGE_WRAP:
                for (x = 0; x < w; x++) {
                    int Y = (y + ysrc[x] - 128) % h;
                    int X = (x + xsrc[x] - 128) % w;

                    if (Y < 0)
                        Y += h;
                    if (X < 0)
                        X += w;
                    dst[x] = src[Y * slinesize + X];
                }
                break;
            }

            ysrc += ylinesize;
            xsrc += xlinesize;
            dst  += dlinesize;
        }
    }
}

static void displace_packed(DisplaceContext *s, const AVFrame *in,
                            const AVFrame *xpic, const AVFrame *ypic,
                            AVFrame *out)
{
    const int step = s->step;
    const int h = s->height[0];
    const int w = s->width[0];
    const int dlinesize = out->linesize[0];
    const int slinesize = in->linesize[0];
    const int xlinesize = xpic->linesize[0];
    const int ylinesize = ypic->linesize[0];
    const uint8_t *src = in->data[0];
    const uint8_t *ysrc = ypic->data[0];
    const uint8_t *xsrc = xpic->data[0];
    const uint8_t *blank = s->blank;
    uint8_t *dst = out->data[0];
    int c, x, y;

    for (y = 0; y < h; y++) {
        switch (s->edge) {
        case EDGE_BLANK:
            for (x = 0; x < w; x++) {
                for (c = 0; c < s->nb_components; c++) {
                    int Y = y + (ysrc[x * step + c] - 128);
                    int X = x + (xsrc[x * step + c] - 128);

                    if (Y < 0 || Y >= h || X < 0 || X >= w)
                        dst[x * step + c] = blank[c];
                    else
                        dst[x * step + c] = src[Y * slinesize + X * step + c];
                }
            }
            break;
        case EDGE_SMEAR:
            for (x = 0; x < w; x++) {
                for (c = 0; c < s->nb_components; c++) {
                    int Y = av_clip(y + (ysrc[x * step + c] - 128), 0, h - 1);
                    int X = av_clip(x + (xsrc[x * step + c] - 128), 0, w - 1);

                    dst[x * step + c] = src[Y * slinesize + X * step + c];
                }
            }
            break;
        case EDGE_WRAP:
            for (x = 0; x < w; x++) {
                for (c = 0; c < s->nb_components; c++) {
                    int Y = (y + (ysrc[x * step + c] - 128)) % h;
                    int X = (x + (xsrc[x * step + c] - 128)) % w;

                    if (Y < 0)
                        Y += h;
                    if (X < 0)
                        X += w;
                    dst[x * step + c] = src[Y * slinesize + X * step + c];
                }
            }
            break;
        }

        ysrc += ylinesize;
        xsrc += xlinesize;
        dst  += dlinesize;
    }
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    DisplaceContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in, *xpic, *ypic;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &in,   0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &xpic, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &ypic, 0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(in);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in);

        s->displace(s, in, xpic, ypic, out);
    }
    out->pts = av_rescale_q(in->pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DisplaceContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_components = desc->nb_components;

    if (s->nb_planes > 1 || s->nb_components == 1)
        s->displace = displace_planar;
    else
        s->displace = displace_packed;

    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        s->blank[1] = s->blank[2] = 128;
        s->blank[0] = 16;
    }

    s->step = av_get_padded_bits_per_pixel(desc) >> 3;
    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = FF_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = FF_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DisplaceContext *s = ctx->priv;
    AVFilterLink *srclink = ctx->inputs[0];
    AVFilterLink *xlink = ctx->inputs[1];
    AVFilterLink *ylink = ctx->inputs[2];
    FFFrameSyncIn *in;
    int ret;

    if (srclink->format != xlink->format ||
        srclink->format != ylink->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }
    if (srclink->w                       != xlink->w ||
        srclink->h                       != xlink->h ||
        srclink->sample_aspect_ratio.num != xlink->sample_aspect_ratio.num ||
        srclink->sample_aspect_ratio.den != xlink->sample_aspect_ratio.den ||
        srclink->w                       != ylink->w ||
        srclink->h                       != ylink->h ||
        srclink->sample_aspect_ratio.num != ylink->sample_aspect_ratio.num ||
        srclink->sample_aspect_ratio.den != ylink->sample_aspect_ratio.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d, SAR %d:%d) do not match the corresponding "
               "second input link %s parameters (%dx%d, SAR %d:%d) "
               "and/or third input link %s parameters (%dx%d, SAR %d:%d)\n",
               ctx->input_pads[0].name, srclink->w, srclink->h,
               srclink->sample_aspect_ratio.num,
               srclink->sample_aspect_ratio.den,
               ctx->input_pads[1].name, xlink->w, xlink->h,
               xlink->sample_aspect_ratio.num,
               xlink->sample_aspect_ratio.den,
               ctx->input_pads[2].name, ylink->w, ylink->h,
               ylink->sample_aspect_ratio.num,
               ylink->sample_aspect_ratio.den);
        return AVERROR(EINVAL);
    }

    outlink->w = srclink->w;
    outlink->h = srclink->h;
    outlink->time_base = srclink->time_base;
    outlink->sample_aspect_ratio = srclink->sample_aspect_ratio;
    outlink->frame_rate = srclink->frame_rate;

    ret = ff_framesync_init(&s->fs, ctx, 3);
    if (ret < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = srclink->time_base;
    in[1].time_base = xlink->time_base;
    in[2].time_base = ylink->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_STOP;
    in[1].sync   = 1;
    in[1].before = EXT_NULL;
    in[1].after  = EXT_INFINITY;
    in[2].sync   = 1;
    in[2].before = EXT_NULL;
    in[2].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    DisplaceContext *s = inlink->dst->priv;
    return ff_framesync_filter_frame(&s->fs, inlink, buf);
}

static int request_frame(AVFilterLink *outlink)
{
    DisplaceContext *s = outlink->src->priv;
    return ff_framesync_request_frame(&s->fs, outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DisplaceContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad displace_inputs[] = {
    {
        .name         = "source",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    {
        .name         = "xmap",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    {
        .name         = "ymap",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad displace_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_displace = {
    .name          = "displace",
    .description   = NULL_IF_CONFIG_SMALL("Displace pixels."),
    .priv_size     = sizeof(DisplaceContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = displace_inputs,
    .outputs       = displace_outputs,
    .priv_class    = &displace_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
