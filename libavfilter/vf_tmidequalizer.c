/*
 * Copyright (c) 2020 Paul B Mahol
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

typedef struct TMidEqualizerContext {
    const AVClass *class;

    int planes;
    int radius;
    float sigma;

    int plane_width[4], plane_height[4];
    int nb_frames;
    int depth;
    int f_frames;
    int l_frames;
    int del_frame;
    int cur_frame;
    int nb_planes;
    int histogram_size;
    float  kernel[127];
    float *histogram[4][256];
    float *change[4];

    AVFrame **frames;

    void (*compute_histogram)(const uint8_t *ssrc, ptrdiff_t linesize,
                              int w, int h, float *histogram, size_t hsize);
    void (*apply_contrast_change)(const uint8_t *src, ptrdiff_t src_linesize,
                                  uint8_t *dst, ptrdiff_t dst_linesize,
                                  int w, int h, float *change, float *orig);
} TMidEqualizerContext;

#define OFFSET(x) offsetof(TMidEqualizerContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption tmidequalizer_options[] = {
    { "radius", "set radius", OFFSET(radius), AV_OPT_TYPE_INT,   {.i64=5},   1, 127, FLAGS },
    { "sigma",  "set sigma",  OFFSET(sigma),  AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,   1, FLAGS },
    { "planes", "set planes", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=0xF}, 0, 0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tmidequalizer);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_YUV420P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static void compute_contrast_function(const float *const histograms[256],
                                      const float *const kernel,
                                      int nb_frames, int radius, int hsize,
                                      float *f, int idx)
{
    const float *const h1 = histograms[idx];
    int p2[256] = { 0 };

    for (int p1 = 0; p1 < hsize; p1++) {
        float weight = 1.f;
        float sum = p1 * weight;

        for (int j = 0; j < radius; j++) {
            const int nidx = ((idx - radius + j) % nb_frames);
            const float *const h2 = histograms[nidx < 0 ? nidx + nb_frames: nidx];
            int k = j;

            for (; p2[k] < hsize && h2[p2[k]] < h1[p1]; p2[k]++);
            if (p2[k] == hsize)
                p2[k]--;

            weight += kernel[j];
            sum += kernel[j] * p2[k];
        }

        for (int j = radius + 1; j < nb_frames; j++) {
            const int nidx = (idx - radius + j) % nb_frames;
            const float *const h2 = histograms[nidx < 0 ? nidx + nb_frames: nidx];
            int k = j;

            for (; p2[k] < hsize && h2[p2[k]] < h1[p1]; p2[k]++);
            if (p2[k] == hsize)
                p2[k]--;

            weight += kernel[j - radius - 1];
            sum += kernel[j - radius - 1] * p2[k];
        }

        f[p1] = sum / weight;
    }
}

static void apply_contrast_change8(const uint8_t *src, ptrdiff_t src_linesize,
                                   uint8_t *dst, ptrdiff_t dst_linesize,
                                   int w, int h, float *change, float *orig)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            dst[x] = lrintf(change[src[x]]);

        dst += dst_linesize;
        src += src_linesize;
    }
}

static void apply_contrast_change16(const uint8_t *ssrc, ptrdiff_t src_linesize,
                                    uint8_t *ddst, ptrdiff_t dst_linesize,
                                    int w, int h, float *change, float *orig)
{
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            dst[x] = lrintf(change[src[x]]);

        dst += dst_linesize / 2;
        src += src_linesize / 2;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    TMidEqualizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int eof = 0;

    if (!in) {
        int idx = s->f_frames < s->nb_frames ? s->radius : s->del_frame ? s->del_frame - 1 : s->nb_frames - 1;

        if (s->f_frames < s->nb_frames) {
            s->l_frames = s->nb_frames - s->f_frames;
        } else {
            s->l_frames++;
        }
        in = av_frame_clone(s->frames[idx]);
        if (!in)
            return AVERROR(ENOMEM);
        eof = 1;
    }

    if (s->f_frames < s->nb_frames) {
        s->frames[s->f_frames] = in;

        for (int p = 0; p < s->nb_planes; p++) {
            s->compute_histogram(in->data[p], in->linesize[p],
                                 s->plane_width[p], s->plane_height[p],
                                 s->histogram[p][s->f_frames],
                                 s->histogram_size);
        }

        s->f_frames++;

        while (s->f_frames <= s->radius) {
            s->frames[s->f_frames] = av_frame_clone(in);
            if (!s->frames[s->f_frames])
                return AVERROR(ENOMEM);
            for (int p = 0; p < s->nb_planes; p++) {
                memcpy(s->histogram[p][s->f_frames],
                       s->histogram[p][s->f_frames - 1],
                       s->histogram_size * sizeof(float));
            }
            s->f_frames++;
        }

        if (!eof && s->f_frames < s->nb_frames) {
            return 0;
        } else {
            while (s->f_frames < s->nb_frames) {
                s->frames[s->f_frames] = av_frame_clone(in);
                if (!s->frames[s->f_frames])
                    return AVERROR(ENOMEM);
                for (int p = 0; p < s->nb_planes; p++) {
                    memcpy(s->histogram[p][s->f_frames],
                           s->histogram[p][s->f_frames - 1],
                           s->histogram_size * sizeof(float));
                }
                s->f_frames++;
            }
        }
        s->cur_frame = s->radius;
        s->del_frame = 0;
    } else {
        av_frame_free(&s->frames[s->del_frame]);
        s->frames[s->del_frame] = in;

        for (int p = 0; p < s->nb_planes; p++) {
            s->compute_histogram(in->data[p], in->linesize[p],
                                 s->plane_width[p], s->plane_height[p],
                                 s->histogram[p][s->del_frame],
                                 s->histogram_size);
        }

        s->del_frame++;
        if (s->del_frame >= s->nb_frames)
            s->del_frame = 0;
    }

    if (ctx->is_disabled) {
        const int idx = s->cur_frame;

        out = av_frame_clone(s->frames[idx]);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        const int idx = s->cur_frame;

        in = s->frames[idx];
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in);

        for (int p = 0; p < s->nb_planes; p++) {
            if (!((1 << p) & s->planes)) {
                av_image_copy_plane(out->data[p], out->linesize[p], in->data[p], in->linesize[p],
                                    s->plane_width[p] * (1 + (s->depth > 8)), s->plane_height[p]);
                continue;
            }

            compute_contrast_function((const float *const *)s->histogram[p], s->kernel,
                                      s->nb_frames, s->radius, s->histogram_size, s->change[p], idx);

            s->apply_contrast_change(in->data[p], in->linesize[p],
                                     out->data[p], out->linesize[p],
                                     s->plane_width[p], s->plane_height[p],
                                     s->change[p], s->histogram[p][idx]);
        }
    }

    s->cur_frame++;
    if (s->cur_frame >= s->nb_frames)
        s->cur_frame = 0;

    return ff_filter_frame(outlink, out);
}

static void compute_histogram8(const uint8_t *src, ptrdiff_t linesize,
                               int w, int h, float *histogram, size_t hsize)
{
    memset(histogram, 0, hsize * sizeof(*histogram));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            histogram[src[x]] += 1;
        src += linesize;
    }

    for (int x = 0; x < hsize; x++)
        histogram[x] /= hsize;

    for (int x = 1; x < hsize; x++)
        histogram[x] += histogram[x-1];
}

static void compute_histogram16(const uint8_t *ssrc, ptrdiff_t linesize,
                                int w, int h, float *histogram, size_t hsize)
{
    const uint16_t *src = (const uint16_t *)ssrc;

    memset(histogram, 0, hsize * sizeof(*histogram));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            histogram[src[x]] += 1;
        src += linesize / 2;
    }

    for (int x = 0; x < hsize; x++)
        histogram[x] /= hsize;

    for (int x = 1; x < hsize; x++)
        histogram[x] += histogram[x-1];
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    TMidEqualizerContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    float sigma = s->radius * s->sigma;
    int vsub, hsub;

    s->depth = desc->comp[0].depth;
    s->nb_frames = s->radius * 2 + 1;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;

    s->plane_height[0] = s->plane_height[3] = inlink->h;
    s->plane_width[0]  = s->plane_width[3]  = inlink->w;
    s->plane_height[1] = s->plane_height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->plane_width[1]  = s->plane_width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);

    s->histogram_size = 1 << s->depth;

    for (int n = 0; n < s->radius; n++)
        s->kernel[n] = expf(-0.5 * (n + 1) * (n + 1) / (sigma * sigma));

    for (int p = 0; p < s->nb_planes; p++) {
        for (int n = 0; n < s->nb_frames; n++) {
            s->histogram[p][n] = av_calloc(s->histogram_size, sizeof(float));
            if (!s->histogram[p][n])
                return AVERROR(ENOMEM);
        }

        s->change[p] = av_calloc(s->histogram_size, sizeof(float));
        if (!s->change[p])
            return AVERROR(ENOMEM);
    }

    if (!s->frames)
        s->frames = av_calloc(s->nb_frames, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    s->compute_histogram = s->depth <= 8 ? compute_histogram8 : compute_histogram16;
    s->apply_contrast_change = s->depth <= 8 ? apply_contrast_change8 : apply_contrast_change16;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TMidEqualizerContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->l_frames < s->radius) {
        ret = filter_frame(ctx->inputs[0], NULL);
    }

    return ret;
}

static void free_histograms(AVFilterContext *ctx, int x, int nb_frames)
{
    TMidEqualizerContext *s = ctx->priv;

    for (int n = 0; n < nb_frames; n++)
        av_freep(&s->histogram[x][n]);
    av_freep(&s->change[x]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TMidEqualizerContext *s = ctx->priv;

    free_histograms(ctx, 0, s->nb_frames);
    free_histograms(ctx, 1, s->nb_frames);
    free_histograms(ctx, 2, s->nb_frames);
    free_histograms(ctx, 3, s->nb_frames);

    for (int i = 0; i < s->nb_frames && s->frames; i++)
        av_frame_free(&s->frames[i]);
    av_freep(&s->frames);
}

static const AVFilterPad tmidequalizer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad tmidequalizer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_tmidequalizer = {
    .name          = "tmidequalizer",
    .description   = NULL_IF_CONFIG_SMALL("Apply Temporal Midway Equalization."),
    .priv_size     = sizeof(TMidEqualizerContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = tmidequalizer_inputs,
    .outputs       = tmidequalizer_outputs,
    .priv_class    = &tmidequalizer_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
