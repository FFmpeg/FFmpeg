/*
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct EntropyContext {
    const AVClass *class;

    int mode;

    int nb_planes;
    int planeheight[4];
    int planewidth[4];
    int depth;
    int is_rgb;
    uint8_t rgba_map[4];
    char planenames[4];
    int64_t *histogram;
} EntropyContext;

#define OFFSET(x) offsetof(EntropyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption entropy_options[] = {
    { "mode", "set kind of histogram entropy measurement",  OFFSET(mode), AV_OPT_TYPE_INT,   {.i64=0}, 0, 1, FLAGS, "mode" },
    { "normal", NULL,                                       0,            AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mode" },
    { "diff",   NULL,                                       0,            AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(entropy);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixfmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
        AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *formats = ff_make_format_list(pixfmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    EntropyContext *s = ctx->priv;

    s->nb_planes = desc->nb_components;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;

    s->planenames[0] = s->is_rgb ? 'R' : 'Y';
    s->planenames[1] = s->is_rgb ? 'G' : 'U';
    s->planenames[2] = s->is_rgb ? 'B' : 'V';
    s->planenames[3] = 'A';

    s->histogram = av_malloc_array(1 << s->depth, sizeof(*s->histogram));
    if (!s->histogram)
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    EntropyContext *s = ctx->priv;
    int plane, y, x;

    for (plane = 0; plane < s->nb_planes; plane++) {
        int cidx = s->is_rgb ? s->rgba_map[plane] : plane;
        const uint8_t *src8 = in->data[plane];
        const uint16_t *src16 = (const uint16_t *)in->data[plane];
        float total = s->planewidth[plane] * s->planeheight[plane];
        float entropy = 0;
        char metabuf[128];
        char key[128];

        memset(s->histogram, 0, (1 << s->depth) * sizeof(*s->histogram));

        if (s->depth <= 8) {
            for (y = 0; y < s->planeheight[plane]; y++) {
                for (x = 0; x < s->planewidth[plane]; x++) {
                    s->histogram[src8[x]]++;
                }

                src8 += in->linesize[plane];
            }
        } else {
            for (y = 0; y < s->planeheight[plane]; y++) {
                for (x = 0; x < s->planewidth[plane]; x++) {
                    s->histogram[src16[x]]++;
                }

                src16 += in->linesize[plane] / 2;
            }
        }

        for (y = 0; y < 1 << s->depth; y++) {
            if (s->mode == 0) {
                if (s->histogram[y]) {
                    float p = s->histogram[y] / total;
                    entropy += -log2(p) * p;
                }
            } else if (s->mode == 1) {
                if (y && (s->histogram[y] - s->histogram[y - 1]) != 0) {
                    float p = FFABS(s->histogram[y] - s->histogram[y - 1]) / total;
                    entropy += -log2(p) * p;
                }
            }
        }

        snprintf(key, sizeof(key), "lavfi.entropy.entropy.%s.%c", s->mode ? "diff" : "normal", s->planenames[cidx]);
        snprintf(metabuf, sizeof(metabuf), "%f", entropy);
        av_dict_set(&in->metadata, key, metabuf, 0);
        snprintf(key, sizeof(key), "lavfi.entropy.normalized_entropy.%s.%c", s->mode ? "diff" : "normal", s->planenames[cidx]);
        snprintf(metabuf, sizeof(metabuf), "%f", entropy / log2(1 << s->depth));
        av_dict_set(&in->metadata, key, metabuf, 0);
    }

    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    EntropyContext *s = ctx->priv;

    av_freep(&s->histogram);
}

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_entropy = {
    .name           = "entropy",
    .description    = NULL_IF_CONFIG_SMALL("Measure video frames entropy."),
    .priv_size      = sizeof(EntropyContext),
    .uninit         = uninit,
    .query_formats  = query_formats,
    .inputs         = inputs,
    .outputs        = outputs,
    .priv_class     = &entropy_class,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
