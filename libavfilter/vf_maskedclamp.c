/*
 * Copyright (c) 2016 Paul B Mahol
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
#include "internal.h"
#include "video.h"
#include "framesync.h"

#define OFFSET(x) offsetof(MaskedClampContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

typedef struct MaskedClampContext {
    const AVClass *class;

    int planes;
    int undershoot;
    int overshoot;

    int width[4], height[4];
    int nb_planes;
    int depth;
    FFFrameSync fs;

    void (*maskedclamp)(const uint8_t *bsrc, const uint8_t *osrc,
                        const uint8_t *msrc, uint8_t *dst,
                        ptrdiff_t blinesize, ptrdiff_t darklinesize,
                        ptrdiff_t brightlinesize, ptrdiff_t destlinesize,
                        int w, int h, int undershoot, int overshoot);
} MaskedClampContext;

static const AVOption maskedclamp_options[] = {
    { "undershoot", "set undershoot", OFFSET(undershoot), AV_OPT_TYPE_INT, {.i64=0},   0, INT_MAX, FLAGS },
    { "overshoot",  "set overshoot",  OFFSET(overshoot),  AV_OPT_TYPE_INT, {.i64=0},   0, INT_MAX, FLAGS },
    { "planes",     "set planes",     OFFSET(planes),     AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF,     FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(maskedclamp);

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
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    MaskedClampContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *base, *dark, *bright;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &base,   0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &dark,   0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &bright, 0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(base);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        int p;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, base);

        for (p = 0; p < s->nb_planes; p++) {
            if (!((1 << p) & s->planes)) {
                av_image_copy_plane(out->data[p], out->linesize[p], base->data[p], base->linesize[p],
                                    s->width[p], s->height[p]);
                continue;
            }

            s->maskedclamp(base->data[p], dark->data[p],
                           bright->data[p], out->data[p],
                           base->linesize[p], dark->linesize[p],
                           bright->linesize[p], out->linesize[p],
                           s->width[p], s->height[p],
                           s->undershoot, s->overshoot);
        }
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static void maskedclamp8(const uint8_t *bsrc, const uint8_t *darksrc,
                         const uint8_t *brightsrc, uint8_t *dst,
                         ptrdiff_t blinesize, ptrdiff_t darklinesize,
                         ptrdiff_t brightlinesize, ptrdiff_t dlinesize,
                         int w, int h,
                         int undershoot, int overshoot)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (bsrc[x] < darksrc[x] - undershoot)
                dst[x] = darksrc[x] - undershoot;
            else if (bsrc[x] > brightsrc[x] + overshoot)
                dst[x] = brightsrc[x] + overshoot;
            else
                dst[x] = bsrc[x];
        }

        dst  += dlinesize;
        bsrc += blinesize;
        darksrc += darklinesize;
        brightsrc += brightlinesize;
    }
}

static void maskedclamp16(const uint8_t *bbsrc, const uint8_t *oosrc,
                          const uint8_t *mmsrc, uint8_t *ddst,
                          ptrdiff_t blinesize, ptrdiff_t darklinesize,
                          ptrdiff_t brightlinesize, ptrdiff_t dlinesize,
                          int w, int h,
                          int undershoot, int overshoot)
{
    const uint16_t *bsrc = (const uint16_t *)bbsrc;
    const uint16_t *darksrc = (const uint16_t *)oosrc;
    const uint16_t *brightsrc = (const uint16_t *)mmsrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    dlinesize /= 2;
    blinesize /= 2;
    darklinesize /= 2;
    brightlinesize /= 2;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (bsrc[x] < darksrc[x] - undershoot)
                dst[x] = darksrc[x] - undershoot;
            else if (bsrc[x] > brightsrc[x] + overshoot)
                dst[x] = brightsrc[x] + overshoot;
            else
                dst[x] = bsrc[x];
        }

        dst  += dlinesize;
        bsrc += blinesize;
        darksrc += darklinesize;
        brightsrc += brightlinesize;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MaskedClampContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->depth = desc->comp[0].depth;

    if (desc->comp[0].depth == 8)
        s->maskedclamp = maskedclamp8;
    else
        s->maskedclamp = maskedclamp16;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MaskedClampContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *dark = ctx->inputs[1];
    AVFilterLink *bright = ctx->inputs[2];
    FFFrameSyncIn *in;
    int ret;

    if (base->format != dark->format ||
        base->format != bright->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }
    if (base->w                       != dark->w ||
        base->h                       != dark->h ||
        base->sample_aspect_ratio.num != dark->sample_aspect_ratio.num ||
        base->sample_aspect_ratio.den != dark->sample_aspect_ratio.den ||
        base->w                       != bright->w ||
        base->h                       != bright->h ||
        base->sample_aspect_ratio.num != bright->sample_aspect_ratio.num ||
        base->sample_aspect_ratio.den != bright->sample_aspect_ratio.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d, SAR %d:%d) do not match the corresponding "
               "second input link %s parameters (%dx%d, SAR %d:%d) "
               "and/or third input link %s parameters (%dx%d, SAR %d:%d)\n",
               ctx->input_pads[0].name, base->w, base->h,
               base->sample_aspect_ratio.num,
               base->sample_aspect_ratio.den,
               ctx->input_pads[1].name, dark->w, dark->h,
               dark->sample_aspect_ratio.num,
               dark->sample_aspect_ratio.den,
               ctx->input_pads[2].name, bright->w, bright->h,
               bright->sample_aspect_ratio.num,
               bright->sample_aspect_ratio.den);
        return AVERROR(EINVAL);
    }

    outlink->w = base->w;
    outlink->h = base->h;
    outlink->time_base = base->time_base;
    outlink->sample_aspect_ratio = base->sample_aspect_ratio;
    outlink->frame_rate = base->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 3)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = base->time_base;
    in[1].time_base = dark->time_base;
    in[2].time_base = bright->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    in[2].sync   = 1;
    in[2].before = EXT_STOP;
    in[2].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    MaskedClampContext *s = inlink->dst->priv;
    return ff_framesync_filter_frame(&s->fs, inlink, buf);
}

static int request_frame(AVFilterLink *outlink)
{
    MaskedClampContext *s = outlink->src->priv;
    return ff_framesync_request_frame(&s->fs, outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MaskedClampContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad maskedclamp_inputs[] = {
    {
        .name         = "base",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    {
        .name         = "dark",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    {
        .name         = "bright",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad maskedclamp_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_maskedclamp = {
    .name          = "maskedclamp",
    .description   = NULL_IF_CONFIG_SMALL("Clamp first stream with second stream and third stream."),
    .priv_size     = sizeof(MaskedClampContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = maskedclamp_inputs,
    .outputs       = maskedclamp_outputs,
    .priv_class    = &maskedclamp_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
