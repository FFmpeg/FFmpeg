/*
 * Copyright (c) 2016 Paul B Mahol
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct AverageBlurContext {
    const AVClass *class;

    int radius;
    int radiusV;
    int planes;

    int depth;
    int planewidth[4];
    int planeheight[4];
    float *buffer;
    int nb_planes;

    int (*filter_horizontally)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int (*filter_vertically)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} AverageBlurContext;

#define OFFSET(x) offsetof(AverageBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption avgblur_options[] = {
    { "sizeX",  "set horizontal size",  OFFSET(radius),  AV_OPT_TYPE_INT, {.i64=1},   1, 1024, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes),  AV_OPT_TYPE_INT, {.i64=0xF}, 0,  0xF, FLAGS },
    { "sizeY",  "set vertical size",    OFFSET(radiusV), AV_OPT_TYPE_INT, {.i64=0},   0, 1024, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(avgblur);

typedef struct ThreadData {
    int height;
    int width;
    uint8_t *ptr;
    int linesize;
} ThreadData;

#define HORIZONTAL_FILTER(name, type)                                                         \
static int filter_horizontally_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)\
{                                                                                             \
    AverageBlurContext *s = ctx->priv;                                                        \
    ThreadData *td = arg;                                                                     \
    const int height = td->height;                                                            \
    const int width = td->width;                                                              \
    const int slice_start = (height *  jobnr   ) / nb_jobs;                                   \
    const int slice_end   = (height * (jobnr+1)) / nb_jobs;                                   \
    const int radius = FFMIN(s->radius, width / 2);                                           \
    const int linesize = td->linesize / sizeof(type);                                         \
    float *buffer = s->buffer;                                                                \
    const type *src;                                                                          \
    float *ptr;                                                                               \
    int y, x;                                                                                 \
                                                                                              \
    /* Filter horizontally along each row */                                                  \
    for (y = slice_start; y < slice_end; y++) {                                               \
        float acc = 0;                                                                        \
        int count = 0;                                                                        \
                                                                                              \
        src = (const type *)td->ptr + linesize * y;                                           \
        ptr = buffer + width * y;                                                             \
                                                                                              \
        for (x = 0; x < radius; x++) {                                                        \
            acc += src[x];                                                                    \
        }                                                                                     \
        count += radius;                                                                      \
                                                                                              \
        for (x = 0; x <= radius; x++) {                                                       \
            acc += src[x + radius];                                                           \
            count++;                                                                          \
            ptr[x] = acc / count;                                                             \
        }                                                                                     \
                                                                                              \
        for (; x < width - radius; x++) {                                                     \
            acc += src[x + radius] - src[x - radius - 1];                                     \
            ptr[x] = acc / count;                                                             \
        }                                                                                     \
                                                                                              \
        for (; x < width; x++) {                                                              \
            acc -= src[x - radius];                                                           \
            count--;                                                                          \
            ptr[x] = acc / count;                                                             \
        }                                                                                     \
    }                                                                                         \
                                                                                              \
    return 0;                                                                                 \
}

HORIZONTAL_FILTER(8, uint8_t)
HORIZONTAL_FILTER(16, uint16_t)

#define VERTICAL_FILTER(name, type)                                                           \
static int filter_vertically_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)  \
{                                                                                             \
    AverageBlurContext *s = ctx->priv;                                                        \
    ThreadData *td = arg;                                                                     \
    const int height = td->height;                                                            \
    const int width = td->width;                                                              \
    const int slice_start = (width *  jobnr   ) / nb_jobs;                                    \
    const int slice_end   = (width * (jobnr+1)) / nb_jobs;                                    \
    const int radius = FFMIN(s->radiusV, height / 2);                                         \
    const int linesize = td->linesize / sizeof(type);                                         \
    type *buffer = (type *)td->ptr;                                                           \
    const float *src;                                                                         \
    type *ptr;                                                                                \
    int i, x;                                                                                 \
                                                                                              \
    /* Filter vertically along each column */                                                 \
    for (x = slice_start; x < slice_end; x++) {                                               \
        float acc = 0;                                                                        \
        int count = 0;                                                                        \
                                                                                              \
        ptr = buffer + x;                                                                     \
        src = s->buffer + x;                                                                  \
                                                                                              \
        for (i = 0; i < radius; i++) {                                                        \
            acc += src[0];                                                                    \
            src += width;                                                                     \
        }                                                                                     \
        count += radius;                                                                      \
                                                                                              \
        src = s->buffer + x;                                                                  \
        ptr = buffer + x;                                                                     \
        for (i = 0; i <= radius; i++) {                                                       \
            acc += src[(i + radius) * width];                                                 \
            count++;                                                                          \
            ptr[i * linesize] = acc / count;                                                  \
        }                                                                                     \
                                                                                              \
        for (; i < height - radius; i++) {                                                    \
            acc += src[(i + radius) * width] - src[(i - radius - 1) * width];                 \
            ptr[i * linesize] = acc / count;                                                  \
        }                                                                                     \
                                                                                              \
        for (; i < height; i++) {                                                             \
            acc -= src[(i - radius) * width];                                                 \
            count--;                                                                          \
            ptr[i * linesize] = acc / count;                                                  \
        }                                                                                     \
    }                                                                                         \
                                                                                              \
    return 0;                                                                                 \
}

VERTICAL_FILTER(8, uint8_t)
VERTICAL_FILTER(16, uint16_t)

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AverageBlurContext *s = inlink->dst->priv;

    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->buffer = av_malloc_array(inlink->w, inlink->h * sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    if (s->radiusV <= 0) {
        s->radiusV = s->radius;
    }

    if (s->depth == 8) {
        s->filter_horizontally = filter_horizontally_8;
        s->filter_vertically = filter_vertically_8;
    } else {
        s->filter_horizontally = filter_horizontally_16;
        s->filter_vertically = filter_vertically_16;
    }

    return 0;
}

static void averageiir2d(AVFilterContext *ctx, AVFrame *in, AVFrame *out, int plane)
{
    AverageBlurContext *s = ctx->priv;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int nb_threads = ff_filter_get_nb_threads(ctx);
    ThreadData td;

    td.width = width;
    td.height = height;
    td.ptr = in->data[plane];
    td.linesize = in->linesize[plane];
    ctx->internal->execute(ctx, s->filter_horizontally, &td, NULL, FFMIN(height, nb_threads));
    td.ptr = out->data[plane];
    td.linesize = out->linesize[plane];
    ctx->internal->execute(ctx, s->filter_vertically, &td, NULL, FFMIN(width, nb_threads));
}

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
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AverageBlurContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int width = s->planewidth[plane];

        if (!(s->planes & (1 << plane))) {
            if (out != in)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    width * ((s->depth + 7) / 8), height);
            continue;
        }

        averageiir2d(ctx, in, out, plane);
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AverageBlurContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad avgblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avgblur_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_avgblur = {
    .name          = "avgblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Average Blur filter."),
    .priv_size     = sizeof(AverageBlurContext),
    .priv_class    = &avgblur_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avgblur_inputs,
    .outputs       = avgblur_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
