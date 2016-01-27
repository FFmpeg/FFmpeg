/*
 * Copyright (c) 2012-2013 Oka Motofumi (chikuzen.mo at gmail dot com)
 * Copyright (c) 2015 Paul B Mahol
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
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ConvolutionContext {
    const AVClass *class;

    char *matrix_str[4];
    float rdiv[4];
    float bias[4];

    int bstride;
    uint8_t *buffer;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];
    int matrix[4][25];
    int matrix_length[4];
    int copy[4];

    void (*filter[4])(struct ConvolutionContext *s, AVFrame *in, AVFrame *out, int plane);
} ConvolutionContext;

#define OFFSET(x) offsetof(ConvolutionContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption convolution_options[] = {
    { "0m", "set matrix for 1st plane", OFFSET(matrix_str[0]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "1m", "set matrix for 2nd plane", OFFSET(matrix_str[1]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "2m", "set matrix for 3rd plane", OFFSET(matrix_str[2]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "3m", "set matrix for 4th plane", OFFSET(matrix_str[3]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "0rdiv", "set rdiv for 1st plane", OFFSET(rdiv[0]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "1rdiv", "set rdiv for 2nd plane", OFFSET(rdiv[1]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "2rdiv", "set rdiv for 3rd plane", OFFSET(rdiv[2]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "3rdiv", "set rdiv for 4th plane", OFFSET(rdiv[3]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "0bias", "set bias for 1st plane", OFFSET(bias[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "1bias", "set bias for 2nd plane", OFFSET(bias[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "2bias", "set bias for 3rd plane", OFFSET(bias[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "3bias", "set bias for 4th plane", OFFSET(bias[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(convolution);

static const int same3x3[9] = {0, 0, 0,
                               0, 1, 0,
                               0, 0, 0};

static const int same5x5[25] = {0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0,
                                0, 0, 1, 0, 0,
                                0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0};

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_input(AVFilterLink *inlink)
{
    ConvolutionContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->planewidth, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->bstride = s->planewidth[0] + 32;
    s->buffer = av_malloc(5 * s->bstride);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    return 0;
}

static inline void line_copy8(uint8_t *line, const uint8_t *srcp, int width, int mergin)
{
    int i;

    memcpy(line, srcp, width);

    for (i = mergin; i > 0; i--) {
        line[-i] = line[i];
        line[width - 1 + i] = line[width - 1 - i];
    }
}

static void filter_3x3(ConvolutionContext *s, AVFrame *in, AVFrame *out, int plane)
{
    const uint8_t *src = in->data[plane];
    uint8_t *dst = out->data[plane];
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    uint8_t *p0 = s->buffer + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *orig = p0, *end = p2;
    const int *matrix = s->matrix[plane];
    const float rdiv = s->rdiv[plane];
    const float bias = s->bias[plane];
    int y, x;

    line_copy8(p0, src + stride, width, 1);
    line_copy8(p1, src, width, 1);

    for (y = 0; y < height; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy8(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int sum = p0[x - 1] * matrix[0] +
                      p0[x] *     matrix[1] +
                      p0[x + 1] * matrix[2] +
                      p1[x - 1] * matrix[3] +
                      p1[x] *     matrix[4] +
                      p1[x + 1] * matrix[5] +
                      p2[x - 1] * matrix[6] +
                      p2[x] *     matrix[7] +
                      p2[x + 1] * matrix[8];
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip_uint8(sum);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane];
    }
}

static void filter_5x5(ConvolutionContext *s, AVFrame *in, AVFrame *out, int plane)
{
    const uint8_t *src = in->data[plane];
    uint8_t *dst = out->data[plane];
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    uint8_t *p0 = s->buffer + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *p3 = p2 + bstride;
    uint8_t *p4 = p3 + bstride;
    uint8_t *orig = p0, *end = p4;
    const int *matrix = s->matrix[plane];
    float rdiv = s->rdiv[plane];
    float bias = s->bias[plane];
    int y, x, i;

    line_copy8(p0, src + 2 * stride, width, 2);
    line_copy8(p1, src + stride, width, 2);
    line_copy8(p2, src, width, 2);
    src += stride;
    line_copy8(p3, src, width, 2);


    for (y = 0; y < height; y++) {
        uint8_t *array[] = {
            p0 - 2, p0 - 1, p0, p0 + 1, p0 + 2,
            p1 - 2, p1 - 1, p1, p1 + 1, p1 + 2,
            p2 - 2, p2 - 1, p2, p2 + 1, p2 + 2,
            p3 - 2, p3 - 1, p3, p3 + 1, p3 + 2,
            p4 - 2, p4 - 1, p4, p4 + 1, p4 + 2
        };

        src += stride * (y < height - 2 ? 1 : -1);
        line_copy8(p4, src, width, 2);

        for (x = 0; x < width; x++) {
            int sum = 0;

            for (i = 0; i < 25; i++) {
                sum += *(array[i] + x) * matrix[i];
            }
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip_uint8(sum);
        }

        p0 = p1;
        p1 = p2;
        p2 = p3;
        p3 = p4;
        p4 = (p4 == end) ? orig: p4 + bstride;
        dst += out->linesize[plane];
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    ConvolutionContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < s->nb_planes; plane++) {
        if (s->copy[plane]) {
            av_image_copy_plane(out->data[plane], out->linesize[plane],
                                in->data[plane], in->linesize[plane],
                                s->planewidth[plane],
                                s->planeheight[plane]);
            continue;
        }

        s->filter[plane](s, in, out, plane);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    ConvolutionContext *s = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        int *matrix = (int *)s->matrix[i];
        char *p, *arg, *saveptr = NULL;

        p = s->matrix_str[i];
        while (s->matrix_length[i] < 25) {
            if (!(arg = av_strtok(p, " ", &saveptr)))
                break;

            p = NULL;
            sscanf(arg, "%d", &matrix[s->matrix_length[i]]);
            s->matrix_length[i]++;
        }

        if (s->matrix_length[i] == 9) {
            if (!memcmp(matrix, same3x3, sizeof(same3x3)))
                s->copy[i] = 1;
            else
                s->filter[i] = filter_3x3;
        } else if (s->matrix_length[i] == 25) {
            if (!memcmp(matrix, same5x5, sizeof(same5x5)))
                s->copy[i] = 1;
            else
                s->filter[i] = filter_5x5;
        } else {
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ConvolutionContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad convolution_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad convolution_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_convolution = {
    .name          = "convolution",
    .description   = NULL_IF_CONFIG_SMALL("Apply convolution filter."),
    .priv_size     = sizeof(ConvolutionContext),
    .priv_class    = &convolution_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
