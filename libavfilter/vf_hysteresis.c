/*
 * Copyright (c) 2013 Oka Motofumi (chikuzen.mo at gmail dot com)
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

#define OFFSET(x) offsetof(HysteresisContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

typedef struct HysteresisContext {
    const AVClass *class;
    FFFrameSync fs;

    int planes;
    int threshold;

    int width[4], height[4];
    int nb_planes;
    int depth;

    uint8_t *map;
    uint32_t *xy;
    int index;

    void (*hysteresis)(struct HysteresisContext *s, const uint8_t *bsrc, const uint8_t *osrc, uint8_t *dst,
                       ptrdiff_t blinesize, ptrdiff_t olinesize,
                       ptrdiff_t destlinesize,
                       int w, int h);
} HysteresisContext;

static const AVOption hysteresis_options[] = {
    { "planes",    "set planes",    OFFSET(planes),    AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF, FLAGS },
    { "threshold", "set threshold", OFFSET(threshold), AV_OPT_TYPE_INT, {.i64=0},   0, UINT16_MAX, FLAGS },
    { NULL }
};

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
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    HysteresisContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *base, *alt;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &base, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &alt,  0)) < 0)
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
            } else {
                int y;

                for (y = 0; y < s->height[p]; y++) {
                    memset(out->data[p] + y * out->linesize[p], 0, s->width[p]);
                }
            }

            s->index = -1;
            memset(s->map, 0, s->width[0] * s->height[0]);
            memset(s->xy, 0, s->width[0] * s->height[0] * 4);

            s->hysteresis(s, base->data[p], alt->data[p],
                          out->data[p],
                          base->linesize[p], alt->linesize[p],
                          out->linesize[p],
                          s->width[p], s->height[p]);
        }
    }
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int passed(HysteresisContext *s, int x, int y, int w)
{
    return s->map[x + y * w];
}

static void push(HysteresisContext *s, int x, int y, int w)
{
    s->map[x + y * w] = 0xff;
    s->xy[++s->index] = (uint16_t)(x) << 16 | (uint16_t)y;
}

static void pop(HysteresisContext *s, int *x, int *y)
{
    uint32_t val = s->xy[s->index--];

    *x = val >> 16;
    *y = val & 0x0000FFFF;
}

static int is_empty(HysteresisContext *s)
{
    return s->index < 0;
}

static void hysteresis8(HysteresisContext *s, const uint8_t *bsrc, const uint8_t *asrc,
                        uint8_t *dst,
                        ptrdiff_t blinesize, ptrdiff_t alinesize,
                        ptrdiff_t dlinesize,
                        int w, int h)
{
    const int t = s->threshold;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if ((bsrc[x + y * blinesize] > t) && (asrc[x + y * alinesize] > t) && !passed(s, x, y, w)) {
                int posx, posy;

                dst[x + y * dlinesize] = asrc[x + y * alinesize];

                push(s, x, y, w);

                while (!is_empty(s)) {
                    int x_min, x_max, y_min, y_max, yy, xx;

                    pop(s, &posx, &posy);

                    x_min = posx > 0 ? posx - 1 : 0;
                    x_max = posx < w - 1 ? posx + 1 : posx;
                    y_min = posy > 0 ? posy - 1 : 0;
                    y_max = posy < h - 1 ? posy + 1 : posy;

                    for (yy = y_min; yy <= y_max; yy++) {
                        for (xx = x_min; xx <= x_max; xx++) {
                            if ((asrc[xx + yy * alinesize] > t) && !passed(s, xx, yy, w)) {
                                dst[xx + yy * dlinesize] = asrc[xx + yy * alinesize];
                                push(s, xx, yy, w);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void hysteresis16(HysteresisContext *s, const uint8_t *bbsrc, const uint8_t *aasrc,
                        uint8_t *ddst,
                        ptrdiff_t blinesize, ptrdiff_t alinesize,
                        ptrdiff_t dlinesize,
                        int w, int h)
{
    const uint16_t *bsrc = (const uint16_t *)bbsrc;
    const uint16_t *asrc = (const uint16_t *)aasrc;
    uint16_t *dst = (uint16_t *)ddst;
    const int t = s->threshold;
    int x, y;

    blinesize /= 2;
    alinesize /= 2;
    dlinesize /= 2;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if ((bsrc[x + y * blinesize] > t) && (asrc[x + y * alinesize] > t) && !passed(s, x, y, w)) {
                int posx, posy;

                dst[x + y * dlinesize] = asrc[x + y * alinesize];

                push(s, x, y, w);

                while (!is_empty(s)) {
                    int x_min, x_max, y_min, y_max, yy, xx;

                    pop(s, &posx, &posy);

                    x_min = posx > 0 ? posx - 1 : 0;
                    x_max = posx < w - 1 ? posx + 1 : posx;
                    y_min = posy > 0 ? posy - 1 : 0;
                    y_max = posy < h - 1 ? posy + 1 : posy;

                    for (yy = y_min; yy <= y_max; yy++) {
                        for (xx = x_min; xx <= x_max; xx++) {
                            if ((asrc[xx + yy * alinesize] > t) && !passed(s, xx, yy, w)) {
                                dst[xx + yy * dlinesize] = asrc[xx + yy * alinesize];
                                push(s, xx, yy, w);
                            }
                        }
                    }
                }
            }
        }
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    HysteresisContext *s = ctx->priv;
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
        s->hysteresis = hysteresis8;
    else
        s->hysteresis = hysteresis16;

    s->map = av_calloc(inlink->w, inlink->h * sizeof (*s->map));
    if (!s->map)
        return AVERROR(ENOMEM);

    s->xy = av_calloc(inlink->w, inlink->h * sizeof(*s->xy));
    if (!s->xy)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HysteresisContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *alt = ctx->inputs[1];
    FFFrameSyncIn *in;
    int ret;

    if (base->w != alt->w || base->h != alt->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, base->w, base->h,
               ctx->input_pads[1].name,
               alt->w, alt->h);
        return AVERROR(EINVAL);
    }

    outlink->w = base->w;
    outlink->h = base->h;
    outlink->sample_aspect_ratio = base->sample_aspect_ratio;
    outlink->frame_rate = base->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = base->time_base;
    in[1].time_base = alt->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    HysteresisContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HysteresisContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    av_freep(&s->map);
    av_freep(&s->xy);
}

FRAMESYNC_DEFINE_CLASS(hysteresis, HysteresisContext, fs);

static const AVFilterPad hysteresis_inputs[] = {
    {
        .name         = "base",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "alt",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad hysteresis_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_hysteresis = {
    .name          = "hysteresis",
    .description   = NULL_IF_CONFIG_SMALL("Grow first stream into second stream by connecting components."),
    .preinit       = hysteresis_framesync_preinit,
    .priv_size     = sizeof(HysteresisContext),
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(hysteresis_inputs),
    FILTER_OUTPUTS(hysteresis_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &hysteresis_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
