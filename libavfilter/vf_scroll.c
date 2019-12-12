/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct ScrollContext {
    const AVClass *class;

    float h_speed, v_speed;
    float h_pos, v_pos;
    float h_ipos, v_ipos;

    int pos_h[4], pos_v[4];

    const AVPixFmtDescriptor *desc;
    int nb_planes;
    int bytes;

    int planewidth[4];
    int planeheight[4];
} ScrollContext;

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
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int scroll_slice(AVFilterContext *ctx, void *arg, int jobnr,
                        int nb_jobs)
{
    ScrollContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    for (int p = 0; p < s->nb_planes; p++) {
        const uint8_t *src = in->data[p];
        const int h = s->planeheight[p];
        const int w = s->planewidth[p] * s->bytes;
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        uint8_t *dst = out->data[p] + slice_start * out->linesize[p];

        for (int y = slice_start; y < slice_end; y++) {
            int yy = (y + s->pos_v[p]) % h;
            const uint8_t *ssrc = src + yy * in->linesize[p];

            if (s->pos_h[p] < w)
                memcpy(dst, ssrc + s->pos_h[p], w - s->pos_h[p]);
            if (s->pos_h[p] > 0)
                memcpy(dst + w - s->pos_h[p], ssrc, s->pos_h[p]);

            dst += out->linesize[p];
        }
    }

    return 0;
}

static void scroll(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    ScrollContext *s = ctx->priv;
    ThreadData td;
    int h_pos, v_pos;

    s->h_pos = fmodf(s->h_pos, in->width);
    s->v_pos = fmodf(s->v_pos, in->height);

    h_pos = s->h_pos;
    v_pos = s->v_pos;

    if (h_pos < 0)
        h_pos += in->width;
    if (v_pos < 0)
        v_pos += in->height;

    s->pos_v[1] = s->pos_v[2] = AV_CEIL_RSHIFT(v_pos, s->desc->log2_chroma_h);
    s->pos_v[0] = s->pos_v[3] = v_pos;
    s->pos_h[1] = s->pos_h[2] = AV_CEIL_RSHIFT(h_pos, s->desc->log2_chroma_w) * s->bytes;
    s->pos_h[0] = s->pos_h[3] = h_pos * s->bytes;

    td.in = in; td.out = out;
    ctx->internal->execute(ctx, scroll_slice, &td, NULL,
                           FFMIN(out->height, ff_filter_get_nb_threads(ctx)));

    s->h_pos += s->h_speed * in->width;
    s->v_pos += s->v_speed * in->height;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    scroll(ctx, in, out);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ScrollContext *s = ctx->priv;

    s->desc = av_pix_fmt_desc_get(inlink->format);
    s->nb_planes = s->desc->nb_components;
    s->bytes = (s->desc->comp[0].depth + 7) >> 3;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->h_pos = (1.f - s->h_ipos) * inlink->w;
    s->v_pos = (1.f - s->v_ipos) * inlink->h;

    return 0;
}

#define OFFSET(x) offsetof(ScrollContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define VFT AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption scroll_options[] = {
    { "horizontal", "set the horizontal scrolling speed", OFFSET(h_speed), AV_OPT_TYPE_FLOAT, {.dbl=0.}, -1., 1., VFT },
    { "h",          "set the horizontal scrolling speed", OFFSET(h_speed), AV_OPT_TYPE_FLOAT, {.dbl=0.}, -1., 1., VFT },
    { "vertical",   "set the vertical scrolling speed",   OFFSET(v_speed), AV_OPT_TYPE_FLOAT, {.dbl=0.}, -1., 1., VFT },
    { "v",          "set the vertical scrolling speed",   OFFSET(v_speed), AV_OPT_TYPE_FLOAT, {.dbl=0.}, -1., 1., VFT },
    { "hpos",       "set initial horizontal position",    OFFSET(h_ipos),  AV_OPT_TYPE_FLOAT, {.dbl=0.},   0, 1., FLAGS },
    { "vpos",       "set initial vertical position",      OFFSET(v_ipos),  AV_OPT_TYPE_FLOAT, {.dbl=0.},   0, 1., FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(scroll);

static const AVFilterPad scroll_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad scroll_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_scroll = {
    .name          = "scroll",
    .description   = NULL_IF_CONFIG_SMALL("Scroll input video."),
    .priv_size     = sizeof(ScrollContext),
    .priv_class    = &scroll_class,
    .query_formats = query_formats,
    .inputs        = scroll_inputs,
    .outputs       = scroll_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
