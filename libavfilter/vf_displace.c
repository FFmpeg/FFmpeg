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

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

enum EdgeMode {
    EDGE_BLANK,
    EDGE_SMEAR,
    EDGE_WRAP,
    EDGE_MIRROR,
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

    int (*displace_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} DisplaceContext;

#define OFFSET(x) offsetof(DisplaceContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption displace_options[] = {
    { "edge", "set edge mode", OFFSET(edge), AV_OPT_TYPE_INT, {.i64=EDGE_SMEAR}, 0, EDGE_NB-1, FLAGS, .unit = "edge" },
    {   "blank",   "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_BLANK},  0, 0, FLAGS, .unit = "edge" },
    {   "smear",   "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_SMEAR},  0, 0, FLAGS, .unit = "edge" },
    {   "wrap" ,   "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_WRAP},   0, 0, FLAGS, .unit = "edge" },
    {   "mirror" , "", 0, AV_OPT_TYPE_CONST, {.i64=EDGE_MIRROR}, 0, 0, FLAGS, .unit = "edge" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(displace);

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

typedef struct ThreadData {
    AVFrame *in, *xin, *yin, *out;
} ThreadData;

static int displace_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DisplaceContext *s = ctx->priv;
    const ThreadData *td = arg;
    const AVFrame *in  = td->in;
    const AVFrame *xin = td->xin;
    const AVFrame *yin = td->yin;
    const AVFrame *out = td->out;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int h = s->height[plane];
        const int w = s->width[plane];
        const int slice_start = (h *  jobnr   ) / nb_jobs;
        const int slice_end   = (h * (jobnr+1)) / nb_jobs;
        const int dlinesize = out->linesize[plane];
        const int slinesize = in->linesize[plane];
        const int xlinesize = xin->linesize[plane];
        const int ylinesize = yin->linesize[plane];
        const uint8_t *src = in->data[plane];
        const uint8_t *ysrc = yin->data[plane] + slice_start * ylinesize;
        const uint8_t *xsrc = xin->data[plane] + slice_start * xlinesize;
        uint8_t *dst = out->data[plane] + slice_start * dlinesize;
        const uint8_t blank = s->blank[plane];

        for (int y = slice_start; y < slice_end; y++) {
            switch (s->edge) {
            case EDGE_BLANK:
                for (int x = 0; x < w; x++) {
                    int Y = y + ysrc[x] - 128;
                    int X = x + xsrc[x] - 128;

                    if (Y < 0 || Y >= h || X < 0 || X >= w)
                        dst[x] = blank;
                    else
                        dst[x] = src[Y * slinesize + X];
                }
                break;
            case EDGE_SMEAR:
                for (int x = 0; x < w; x++) {
                    int Y = av_clip(y + ysrc[x] - 128, 0, h - 1);
                    int X = av_clip(x + xsrc[x] - 128, 0, w - 1);
                    dst[x] = src[Y * slinesize + X];
                }
                break;
            case EDGE_WRAP:
                for (int x = 0; x < w; x++) {
                    int Y = (y + ysrc[x] - 128) % h;
                    int X = (x + xsrc[x] - 128) % w;

                    if (Y < 0)
                        Y += h;
                    if (X < 0)
                        X += w;
                    dst[x] = src[Y * slinesize + X];
                }
                break;
            case EDGE_MIRROR:
                for (int x = 0; x < w; x++) {
                    int Y = y + ysrc[x] - 128;
                    int X = x + xsrc[x] - 128;

                    if (Y < 0)
                        Y = (-Y) % h;
                    if (X < 0)
                        X = (-X) % w;
                    if (Y >= h)
                        Y = h - (Y % h) - 1;
                    if (X >= w)
                        X = w - (X % w) - 1;
                    dst[x] = src[Y * slinesize + X];
                }
                break;
            }

            ysrc += ylinesize;
            xsrc += xlinesize;
            dst  += dlinesize;
        }
    }
    return 0;
}

static int displace_packed(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DisplaceContext *s = ctx->priv;
    const ThreadData *td = arg;
    const AVFrame *in  = td->in;
    const AVFrame *xin = td->xin;
    const AVFrame *yin = td->yin;
    const AVFrame *out = td->out;
    const int step = s->step;
    const int h = s->height[0];
    const int w = s->width[0];
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    const int dlinesize = out->linesize[0];
    const int slinesize = in->linesize[0];
    const int xlinesize = xin->linesize[0];
    const int ylinesize = yin->linesize[0];
    const uint8_t *src = in->data[0];
    const uint8_t *ysrc = yin->data[0] + slice_start * ylinesize;
    const uint8_t *xsrc = xin->data[0] + slice_start * xlinesize;
    uint8_t *dst = out->data[0] + slice_start * dlinesize;
    const uint8_t *blank = s->blank;

    for (int y = slice_start; y < slice_end; y++) {
        switch (s->edge) {
        case EDGE_BLANK:
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < s->nb_components; c++) {
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
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < s->nb_components; c++) {
                    int Y = av_clip(y + (ysrc[x * step + c] - 128), 0, h - 1);
                    int X = av_clip(x + (xsrc[x * step + c] - 128), 0, w - 1);

                    dst[x * step + c] = src[Y * slinesize + X * step + c];
                }
            }
            break;
        case EDGE_WRAP:
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < s->nb_components; c++) {
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
        case EDGE_MIRROR:
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < s->nb_components; c++) {
                    int Y = y + ysrc[x * step + c] - 128;
                    int X = x + xsrc[x * step + c] - 128;

                    if (Y < 0)
                        Y = (-Y) % h;
                    if (X < 0)
                        X = (-X) % w;
                    if (Y >= h)
                        Y = h - (Y % h) - 1;
                    if (X >= w)
                        X = w - (X % w) - 1;
                    dst[x * step + c] = src[Y * slinesize + X * step + c];
                }
            }
            break;
        }

        ysrc += ylinesize;
        xsrc += xlinesize;
        dst  += dlinesize;
    }
    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    DisplaceContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in, *xin, *yin;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &in,  0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &xin, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &yin, 0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(in);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        ThreadData td;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in);

        td.in  = in;
        td.xin = xin;
        td.yin = yin;
        td.out = out;
        ff_filter_execute(ctx, s->displace_slice, &td, NULL,
                          FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

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
        s->displace_slice = displace_planar;
    else
        s->displace_slice = displace_packed;

    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        s->blank[1] = s->blank[2] = 128;
        s->blank[0] = 16;
    }

    s->step = av_get_padded_bits_per_pixel(desc) >> 3;
    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
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

    if (srclink->w != xlink->w ||
        srclink->h != xlink->h ||
        srclink->w != ylink->w ||
        srclink->h != ylink->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (%dx%d) "
               "and/or third input link %s parameters (%dx%d)\n",
               ctx->input_pads[0].name, srclink->w, srclink->h,
               ctx->input_pads[1].name, xlink->w, xlink->h,
               ctx->input_pads[2].name, ylink->w, ylink->h);
        return AVERROR(EINVAL);
    }

    outlink->w = srclink->w;
    outlink->h = srclink->h;
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

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    DisplaceContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
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
        .config_props = config_input,
    },
    {
        .name         = "xmap",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "ymap",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad displace_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_displace = {
    .name          = "displace",
    .description   = NULL_IF_CONFIG_SMALL("Displace pixels."),
    .priv_size     = sizeof(DisplaceContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(displace_inputs),
    FILTER_OUTPUTS(displace_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &displace_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
