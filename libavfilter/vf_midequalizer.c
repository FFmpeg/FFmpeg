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
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "framesync.h"

typedef struct MidEqualizerContext {
    const AVClass *class;
    int width[2][4], height[2][4];
    int nb_planes;
    int planes;
    int histogram_size;
    float *histogram[2];
    unsigned *cchange;
    FFFrameSync fs;

    void (*midequalizer)(const uint8_t *in0, const uint8_t *in1,
                         uint8_t *dst,
                         ptrdiff_t linesize1, ptrdiff_t linesize2,
                         ptrdiff_t dlinesize,
                         int w0, int h0,
                         int w1, int h1,
                         float *histogram1, float *histogram2,
                         unsigned *cchange, size_t hsize);
} MidEqualizerContext;

#define OFFSET(x) offsetof(MidEqualizerContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption midequalizer_options[] = {
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(midequalizer);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    MidEqualizerContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in0, *in1;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &in0, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &in1, 0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(in0);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        int p;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in0);

        for (p = 0; p < s->nb_planes; p++) {
            if (!((1 << p) & s->planes)) {
                av_image_copy_plane(out->data[p], out->linesize[p], in0->data[p], in0->linesize[p],
                                    s->width[0][p] * (1 + (s->histogram_size > 256)), s->height[0][p]);
                continue;
            }

            s->midequalizer(in0->data[p], in1->data[p],
                            out->data[p],
                            in0->linesize[p], in1->linesize[p],
                            out->linesize[p],
                            s->width[0][p], s->height[0][p],
                            s->width[1][p], s->height[1][p],
                            s->histogram[0], s->histogram[1],
                            s->cchange, s->histogram_size);
        }
    }
    out->pts = av_rescale_q(in0->pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static void compute_histogram8(const uint8_t *src, ptrdiff_t linesize,
                               int w, int h, float *histogram, size_t hsize)
{
    int y, x;

    memset(histogram, 0, hsize * sizeof(*histogram));

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            histogram[src[x]] += 1;
        }
        src += linesize;
    }

    for (x = 0; x < hsize - 1; x++) {
        histogram[x + 1] += histogram[x];
        histogram[x] /= hsize;
    }
    histogram[x] /= hsize;
}

static void compute_histogram16(const uint16_t *src, ptrdiff_t linesize,
                                int w, int h, float *histogram, size_t hsize)
{
    int y, x;

    memset(histogram, 0, hsize * sizeof(*histogram));

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            histogram[src[x]] += 1;
        }
        src += linesize;
    }

    for (x = 0; x < hsize - 1; x++) {
        histogram[x + 1] += histogram[x];
        histogram[x] /= hsize;
    }
    histogram[x] /= hsize;
}

static void compute_contrast_change(float *histogram1, float *histogram2,
                                    unsigned *cchange, size_t hsize)
{
    int i;

    for (i = 0; i < hsize; i++) {
        int j;

        for (j = 0; j < hsize && histogram2[j] < histogram1[i]; j++);

        cchange[i] = (i + j) / 2;
    }
}

static void midequalizer8(const uint8_t *in0, const uint8_t *in1,
                          uint8_t *dst,
                          ptrdiff_t linesize1, ptrdiff_t linesize2,
                          ptrdiff_t dlinesize,
                          int w0, int h0,
                          int w1, int h1,
                          float *histogram1, float *histogram2,
                          unsigned *cchange,
                          size_t hsize)
{
    int x, y;

    compute_histogram8(in0, linesize1, w0, h0, histogram1, hsize);
    compute_histogram8(in1, linesize2, w1, h1, histogram2, hsize);

    compute_contrast_change(histogram1, histogram2, cchange, hsize);

    for (y = 0; y < h0; y++) {
        for (x = 0; x < w0; x++) {
            dst[x] = av_clip_uint8(cchange[in0[x]]);
        }
        dst += dlinesize;
        in0 += linesize1;
    }
}

static void midequalizer16(const uint8_t *in0, const uint8_t *in1,
                           uint8_t *dst,
                           ptrdiff_t linesize1, ptrdiff_t linesize2,
                           ptrdiff_t dlinesize,
                           int w0, int h0,
                           int w1, int h1,
                           float *histogram1, float *histogram2,
                           unsigned *cchange,
                           size_t hsize)
{
    const uint16_t *i = (const uint16_t *)in0;
    uint16_t *d = (uint16_t *)dst;
    int x, y;

    compute_histogram16(i, linesize1 / 2, w0, h0, histogram1, hsize);
    compute_histogram16((const uint16_t *)in1, linesize2 / 2, w1, h1, histogram2, hsize);

    compute_contrast_change(histogram1, histogram2, cchange, hsize);

    for (y = 0; y < h0; y++) {
        for (x = 0; x < w0; x++) {
            d[x] = cchange[i[x]];
        }
        d += dlinesize / 2;
        i += linesize1 / 2;
    }
}

static int config_input0(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MidEqualizerContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;

    s->height[0][0] = s->height[0][3] = inlink->h;
    s->width[0][0]  = s->width[0][3]  = inlink->w;
    s->height[0][1] = s->height[0][2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->width[0][1]  = s->width[0][2]  = AV_CEIL_RSHIFT(inlink->w, hsub);

    s->histogram_size = 1 << desc->comp[0].depth;

    s->histogram[0] = av_calloc(s->histogram_size, sizeof(float));
    s->histogram[1] = av_calloc(s->histogram_size, sizeof(float));
    s->cchange      = av_calloc(s->histogram_size, sizeof(unsigned));
    if (!s->histogram[0] || !s->histogram[1] || !s->cchange)
        return AVERROR(ENOMEM);

    if (s->histogram_size == 256) {
        s->midequalizer = midequalizer8;
    } else {
        s->midequalizer = midequalizer16;
    }

    return 0;
}

static int config_input1(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MidEqualizerContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;

    s->height[1][0] = s->height[1][3] = inlink->h;
    s->width[1][0]  = s->width[1][3]  = inlink->w;
    s->height[1][1] = s->height[1][2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->width[1][1]  = s->width[1][2]  = AV_CEIL_RSHIFT(inlink->w, hsub);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MidEqualizerContext *s = ctx->priv;
    AVFilterLink *in0 = ctx->inputs[0];
    AVFilterLink *in1 = ctx->inputs[1];
    FFFrameSyncIn *in;
    int ret;

    if (in0->format != in1->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }

    outlink->w = in0->w;
    outlink->h = in0->h;
    outlink->time_base = in0->time_base;
    outlink->sample_aspect_ratio = in0->sample_aspect_ratio;
    outlink->frame_rate = in0->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = in0->time_base;
    in[1].time_base = in1->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    MidEqualizerContext *s = inlink->dst->priv;
    return ff_framesync_filter_frame(&s->fs, inlink, buf);
}

static int request_frame(AVFilterLink *outlink)
{
    MidEqualizerContext *s = outlink->src->priv;
    return ff_framesync_request_frame(&s->fs, outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MidEqualizerContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    av_freep(&s->histogram[0]);
    av_freep(&s->histogram[1]);
    av_freep(&s->cchange);
}

static const AVFilterPad midequalizer_inputs[] = {
    {
        .name         = "in0",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input0,
    },
    {
        .name         = "in1",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input1,
    },
    { NULL }
};

static const AVFilterPad midequalizer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_midequalizer = {
    .name          = "midequalizer",
    .description   = NULL_IF_CONFIG_SMALL("Apply Midway Equalization."),
    .priv_size     = sizeof(MidEqualizerContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = midequalizer_inputs,
    .outputs       = midequalizer_outputs,
    .priv_class    = &midequalizer_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
