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

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct AverageBlurContext {
    const AVClass *class;

    int radius;
    int radiusV;
    int planes;

    int depth;
    int max;
    int area;
    int planewidth[4];
    int planeheight[4];
    void *buffer;
    uint16_t lut[256 * 256 * 256];
    int nb_planes;

    int (*filter[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} AverageBlurContext;

#define OFFSET(x) offsetof(AverageBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

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
    const void *ptr;
    void *dptr;
    int linesize, dlinesize;
} ThreadData;

#define LUT_DIV(sum, area) (lut[(sum)])
#define SLOW_DIV(sum, area) ((sum) / (area))

#define FILTER(name, type, btype, lutunused, areaunused, lutdiv)                  \
static int filter_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                 \
    AverageBlurContext *s = ctx->priv;                                            \
    ThreadData *td = arg;                                                         \
    areaunused const int area = s->area;                                          \
    lutunused const uint16_t *lut = s->lut;                                       \
    const int size_w = s->radius;                                                 \
    const int size_h = s->radiusV;                                                \
    btype *col_sum = (btype *)s->buffer + size_w;                                 \
    const int dlinesize = td->dlinesize / sizeof(type);                           \
    const int linesize = td->linesize / sizeof(type);                             \
    const int height = td->height;                                                \
    const int width = td->width;                                                  \
    const type *src = td->ptr;                                                    \
    type *dst = td->dptr;                                                         \
    btype sum = 0;                                                                \
                                                                                  \
    for (int x = -size_w; x < 0; x++) {                                           \
        sum = src[0] * size_h;                                                    \
        for (int y = 0; y <= size_h; y++)                                         \
            sum += src[y * linesize];                                             \
        av_assert2(sum >= 0);                                                     \
        col_sum[x] = sum;                                                         \
    }                                                                             \
                                                                                  \
    for (int x = 0; x < width; x++) {                                             \
        sum = src[x] * size_h;                                                    \
        for (int y = 0; y <= size_h; y++)                                         \
            sum += src[x + y * linesize];                                         \
        av_assert2(sum >= 0);                                                     \
        col_sum[x] = sum;                                                         \
    }                                                                             \
                                                                                  \
    for (int x = width; x < width + size_w; x++) {                                \
        sum = src[width - 1] * size_h;                                            \
        for (int y = 0; y <= size_h; y++)                                         \
            sum += src[width - 1 + y * linesize];                                 \
        av_assert2(sum >= 0);                                                     \
        col_sum[x] = sum;                                                         \
    }                                                                             \
                                                                                  \
    sum = 0;                                                                      \
    for (int x = -size_w; x <= size_w; x++)                                       \
        sum += col_sum[x];                                                        \
    av_assert2(sum >= 0);                                                         \
    dst[0] = lutdiv(sum, area);                                                   \
                                                                                  \
    for (int x = 1; x < width; x++) {                                             \
        sum = sum - col_sum[x - size_w - 1] + col_sum[x + size_w];                \
        av_assert2(sum >= 0);                                                     \
        dst[x] = lutdiv(sum, area);                                               \
    }                                                                             \
                                                                                  \
    src = td->ptr;                                                                \
    src += linesize;                                                              \
    dst += dlinesize;                                                             \
                                                                                  \
    for (int y = 1; y < height; y++) {                                            \
        const int syp = FFMIN(size_h, height - y - 1) * linesize;                 \
        const int syn = FFMIN(y, size_h + 1) * linesize;                          \
                                                                                  \
        sum = 0;                                                                  \
                                                                                  \
        for (int x = -size_w; x < 0; x++)                                         \
            col_sum[x] += src[0 + syp] - src[0 - syn];                            \
                                                                                  \
        for (int x = 0; x < width; x++)                                           \
            col_sum[x] += src[x + syp] - src[x - syn];                            \
                                                                                  \
        for (int x = width; x < width + size_w; x++)                              \
            col_sum[x] += src[width - 1 + syp] - src[width - 1 - syn];            \
                                                                                  \
        for (int x = -size_w; x <= size_w; x++)                                   \
            sum += col_sum[x];                                                    \
        av_assert2(sum >= 0);                                                     \
        dst[0] = lutdiv(sum, area);                                               \
                                                                                  \
        for (int x = 1; x < width; x++) {                                         \
            sum = sum - col_sum[x - size_w - 1] + col_sum[x + size_w];            \
            av_assert2(sum >= 0);                                                 \
            dst[x] = lutdiv(sum, area);                                           \
        }                                                                         \
                                                                                  \
        src += linesize;                                                          \
        dst += dlinesize;                                                         \
    }                                                                             \
                                                                                  \
    return 0;                                                                     \
}

FILTER(lut8,   uint8_t,  int32_t, , av_unused, LUT_DIV)
FILTER(lut16,  uint16_t, int64_t, , av_unused, LUT_DIV)

FILTER(slow8,  uint8_t,  int32_t, av_unused, , SLOW_DIV)
FILTER(slow16, uint16_t, int64_t, av_unused, , SLOW_DIV)

static void build_lut(AVFilterContext *ctx, int max)
{
    AverageBlurContext *s = ctx->priv;
    const int area = (2 * s->radiusV + 1) * (2 * s->radius + 1);

    s->area = area;
    if (max * area >= FF_ARRAY_ELEMS(s->lut))
        return;

    for (int i = 0, j = 0, k = 0; i < max * area; i++, j++) {
        if (j == area) {
            k++;
            j = 0;
        }

        s->lut[i] = k;
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AverageBlurContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AverageBlurContext *s = ctx->priv;

    uninit(ctx);

    s->depth = desc->comp[0].depth;
    s->max = 1 << s->depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->buffer = av_calloc(inlink->w + (1024 * 2 + 1), 4 * ((s->depth + 7) / 8));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    if (s->radiusV <= 0)
        s->radiusV = s->radius;

    s->filter[0] = s->depth <= 8 ? filter_lut8  : filter_lut16;
    s->filter[1] = s->depth <= 8 ? filter_slow8 : filter_slow16;

    s->radius  = FFMIN(s->planewidth[1]  / 2, s->radius);
    s->radiusV = FFMIN(s->planeheight[1] / 2, s->radiusV);

    build_lut(ctx, s->max);

    return 0;
}

static void averageiir2d(AVFilterContext *ctx, AVFrame *in, AVFrame *out, int plane)
{
    AverageBlurContext *s = ctx->priv;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int slow = (s->max * s->area) >= FF_ARRAY_ELEMS(s->lut);
    ThreadData td;

    td.width = width;
    td.height = height;
    td.ptr = in->data[plane];
    td.linesize = in->linesize[plane];
    td.dptr = out->data[plane];
    td.dlinesize = out->linesize[plane];
    s->filter[slow](ctx, &td, 0, 0);
}

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
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AverageBlurContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int width = s->planewidth[plane];

        if (!(s->planes & (1 << plane))) {
            if (out->data[plane] != in->data[plane])
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    width * ((s->depth + 7) / 8), height);
            continue;
        }

        averageiir2d(ctx, in, out, plane);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AverageBlurContext *s = ctx->priv;
    const int area = s->area;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (s->radiusV <= 0)
        s->radiusV = s->radius;

    s->radius  = FFMIN(s->planewidth[1]  / 2, s->radius);
    s->radiusV = FFMIN(s->planeheight[1] / 2, s->radiusV);

    if (area != (2 * s->radiusV + 1) * (2 * s->radius + 1))
        build_lut(ctx, s->max);

    return 0;
}

static const AVFilterPad avgblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_avgblur = {
    .name          = "avgblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Average Blur filter."),
    .priv_size     = sizeof(AverageBlurContext),
    .priv_class    = &avgblur_class,
    .uninit        = uninit,
    FILTER_INPUTS(avgblur_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = process_command,
};
