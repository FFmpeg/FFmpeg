/*
 * Copyright (c) 2017 Ming Yang
 * Copyright (c) 2019 Paul B Mahol
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct BilateralContext {
    const AVClass *class;

    float sigmaS;
    float sigmaR;
    int planes;

    int nb_threads;
    int nb_planes;
    int depth;
    int planewidth[4];
    int planeheight[4];

    float alpha;
    float range_table[65536];

    float *img_out_f[4];
    float *img_temp[4];
    float *map_factor_a[4];
    float *map_factor_b[4];
    float *slice_factor_a[4];
    float *slice_factor_b[4];
    float *line_factor_a[4];
    float *line_factor_b[4];
} BilateralContext;

#define OFFSET(x) offsetof(BilateralContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption bilateral_options[] = {
    { "sigmaS", "set spatial sigma",    OFFSET(sigmaS), AV_OPT_TYPE_FLOAT, {.dbl=0.1}, 0.0, 512, FLAGS },
    { "sigmaR", "set range sigma",      OFFSET(sigmaR), AV_OPT_TYPE_FLOAT, {.dbl=0.1}, 0.0,   1, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=1},     0, 0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(bilateral);

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

static int config_params(AVFilterContext *ctx)
{
    BilateralContext *s = ctx->priv;
    float inv_sigma_range;

    inv_sigma_range = 1.0f / (s->sigmaR * ((1 << s->depth) - 1));
    s->alpha = expf(-sqrtf(2.f) / s->sigmaS);

    //compute a lookup table
    for (int i = 0; i < (1 << s->depth); i++)
        s->range_table[i] = s->alpha * expf(-i * inv_sigma_range);

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BilateralContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    config_params(ctx);

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = ff_filter_get_nb_threads(ctx);

    for (int p = 0; p < s->nb_planes; p++) {
        const int w = s->planewidth[p];
        const int h = s->planeheight[p];

        s->img_out_f[p] = av_calloc(w * h, sizeof(float));
        s->img_temp[p] = av_calloc(w * h, sizeof(float));
        s->map_factor_a[p] = av_calloc(w * h, sizeof(float));
        s->map_factor_b[p] = av_calloc(w * h, sizeof(float));
        s->slice_factor_a[p] = av_calloc(w, sizeof(float));
        s->slice_factor_b[p] = av_calloc(w, sizeof(float));
        s->line_factor_a[p] = av_calloc(w, sizeof(float));
        s->line_factor_b[p] = av_calloc(w, sizeof(float));

        if (!s->img_out_f[p] ||
            !s->img_temp[p] ||
            !s->map_factor_a[p] ||
            !s->map_factor_b[p] ||
            !s->slice_factor_a[p] ||
            !s->slice_factor_a[p] ||
            !s->line_factor_a[p] ||
            !s->line_factor_a[p])
            return AVERROR(ENOMEM);
    }

    return 0;
}

#define BILATERAL_H(type, name)                                               \
static void bilateralh_##name(BilateralContext *s, AVFrame *out, AVFrame *in, \
                              int jobnr, int nb_jobs, int plane)              \
{                                                                             \
    const int width = s->planewidth[plane];                                   \
    const int height = s->planeheight[plane];                                 \
    const int slice_start = (height * jobnr) / nb_jobs;                       \
    const int slice_end = (height * (jobnr+1)) / nb_jobs;                     \
    const int src_linesize = in->linesize[plane] / sizeof(type);              \
    const type *src = (const type *)in->data[plane];                          \
    float *img_temp = s->img_temp[plane];                                     \
    float *map_factor_a = s->map_factor_a[plane];                             \
    const float *const range_table = s->range_table;                          \
    const float alpha = s->alpha;                                             \
    float ypr, ycr, fp, fc;                                                   \
    const float inv_alpha_ = 1.f - alpha;                                     \
                                                                              \
    for (int y = slice_start; y < slice_end; y++) {                           \
        float *temp_factor_x, *temp_x = &img_temp[y * width];                 \
        const type *in_x = &src[y * src_linesize];                            \
        const type *texture_x = &src[y * src_linesize];                       \
        type tpr;                                                             \
                                                                              \
        *temp_x++ = ypr = *in_x++;                                            \
        tpr = *texture_x++;                                                   \
                                                                              \
        temp_factor_x = &map_factor_a[y * width];                             \
        *temp_factor_x++ = fp = 1;                                            \
                                                                              \
        for (int x = 1; x < width; x++) {                                     \
            float alpha_;                                                     \
            int range_dist;                                                   \
            type tcr = *texture_x++;                                          \
            type dr = abs(tcr - tpr);                                         \
                                                                              \
            range_dist = dr;                                                  \
            alpha_ = range_table[range_dist];                                 \
            *temp_x++ = ycr = inv_alpha_*(*in_x++) + alpha_*ypr;              \
            tpr = tcr;                                                        \
            ypr = ycr;                                                        \
            *temp_factor_x++ = fc = inv_alpha_ + alpha_ * fp;                 \
            fp = fc;                                                          \
        }                                                                     \
        --temp_x; *temp_x = ((*temp_x) + (*--in_x));                          \
        tpr = *--texture_x;                                                   \
        ypr = *in_x;                                                          \
                                                                              \
        --temp_factor_x; *temp_factor_x = ((*temp_factor_x) + 1);             \
        fp = 1;                                                               \
                                                                              \
        for (int x = width - 2; x >= 0; x--) {                                \
            type tcr = *--texture_x;                                          \
            type dr = abs(tcr - tpr);                                         \
            int range_dist = dr;                                              \
            float alpha_ = range_table[range_dist];                           \
                                                                              \
            ycr = inv_alpha_ * (*--in_x) + alpha_ * ypr;                      \
            --temp_x; *temp_x = ((*temp_x) + ycr);                            \
            tpr = tcr;                                                        \
            ypr = ycr;                                                        \
                                                                              \
            fc = inv_alpha_ + alpha_*fp;                                      \
            --temp_factor_x;                                                  \
            *temp_factor_x = ((*temp_factor_x) + fc);                         \
            fp = fc;                                                          \
        }                                                                     \
    }                                                                         \
}

BILATERAL_H(uint8_t, byte)
BILATERAL_H(uint16_t, word)

#define BILATERAL_V(type, name)                                               \
static void bilateralv_##name(BilateralContext *s, AVFrame *out, AVFrame *in, \
                              int jobnr, int nb_jobs, int plane)              \
{                                                                             \
    const int width = s->planewidth[plane];                                   \
    const int height = s->planeheight[plane];                                 \
    const int slice_start = (width * jobnr) / nb_jobs;                        \
    const int slice_end = (width * (jobnr+1)) / nb_jobs;                      \
    const int src_linesize = in->linesize[plane] / sizeof(type);              \
    const type *src = (const type *)in->data[plane] + slice_start;            \
    float *img_out_f = s->img_out_f[plane] + slice_start;                     \
    float *img_temp = s->img_temp[plane] + slice_start;                       \
    float *map_factor_a = s->map_factor_a[plane] + slice_start;               \
    float *map_factor_b = s->map_factor_b[plane] + slice_start;               \
    float *slice_factor_a = s->slice_factor_a[plane] + slice_start;           \
    float *slice_factor_b = s->slice_factor_b[plane] + slice_start;           \
    float *line_factor_a = s->line_factor_a[plane] + slice_start;             \
    float *line_factor_b = s->line_factor_b[plane] + slice_start;             \
    const float *const range_table = s->range_table;                          \
    const float alpha = s->alpha;                                             \
    float *ycy, *ypy, *xcy;                                                   \
    const float inv_alpha_ = 1.f - alpha;                                     \
    float *ycf, *ypf, *xcf, *in_factor;                                       \
    const type *tcy, *tpy;                                                    \
    int h1;                                                                   \
                                                                              \
    memcpy(img_out_f, img_temp, sizeof(float) * (slice_end - slice_start));   \
                                                                              \
    in_factor = map_factor_a;                                                   \
    memcpy(map_factor_b, in_factor, sizeof(float) * (slice_end - slice_start)); \
    for (int y = 1; y < height; y++) {                                          \
        tpy = &src[(y - 1) * src_linesize];                                   \
        tcy = &src[y * src_linesize];                                         \
        xcy = &img_temp[y * width];                                           \
        ypy = &img_out_f[(y - 1) * width];                                    \
        ycy = &img_out_f[y * width];                                          \
                                                                              \
        xcf = &in_factor[y * width];                                          \
        ypf = &map_factor_b[(y - 1) * width];                                 \
        ycf = &map_factor_b[y * width];                                       \
        for (int x = 0; x < slice_end - slice_start; x++) {                   \
            type dr = abs((*tcy++) - (*tpy++));                               \
            int range_dist = dr;                                              \
            float alpha_ = range_table[range_dist];                           \
                                                                              \
            *ycy++ = inv_alpha_*(*xcy++) + alpha_*(*ypy++);                   \
            *ycf++ = inv_alpha_*(*xcf++) + alpha_*(*ypf++);                   \
        }                                                                     \
    }                                                                         \
    h1 = height - 1;                                                          \
    ycf = line_factor_a;                                                      \
    ypf = line_factor_b;                                                            \
    memcpy(ypf, &in_factor[h1 * width], sizeof(float) * (slice_end - slice_start)); \
    for (int x = 0, k = 0; x < slice_end - slice_start; x++)                        \
        map_factor_b[h1 * width + x] = (map_factor_b[h1 * width + x] + ypf[k++]); \
                                                                                  \
    ycy = slice_factor_a;                                                         \
    ypy = slice_factor_b;                                                         \
    memcpy(ypy, &img_temp[h1 * width], sizeof(float) * (slice_end - slice_start)); \
    for (int x = 0, k = 0; x < slice_end - slice_start; x++) {                \
        int idx = h1 * width + x;                                             \
        img_out_f[idx] = (img_out_f[idx] + ypy[k++]) / map_factor_b[h1 * width + x]; \
    }                                                                                     \
                                                                                          \
    for (int y = h1 - 1; y >= 0; y--) {                                                   \
        float *ycf_, *ypf_, *factor_;                                                     \
        float *ycy_, *ypy_, *out_;                                                        \
                                                                                          \
        tpy = &src[(y + 1) * src_linesize];                                               \
        tcy = &src[y * src_linesize];                                                     \
        xcy = &img_temp[y * width];                                                       \
        ycy_ = ycy;                                                                       \
        ypy_ = ypy;                                                                       \
        out_ = &img_out_f[y * width];                                                     \
                                                                                          \
        xcf = &in_factor[y * width];                                                      \
        ycf_ = ycf;                                                                       \
        ypf_ = ypf;                                                                       \
        factor_ = &map_factor_b[y * width];                                               \
        for (int x = 0; x < slice_end - slice_start; x++) {                               \
            type dr = abs((*tcy++) - (*tpy++));                                           \
            int range_dist = dr;                                                          \
            float alpha_ = range_table[range_dist];                                       \
            float ycc, fcc = inv_alpha_*(*xcf++) + alpha_*(*ypf_++);                      \
                                                                                          \
            *ycf_++ = fcc;                                                                \
            *factor_ = (*factor_ + fcc);                                                  \
                                                                                          \
            ycc = inv_alpha_*(*xcy++) + alpha_*(*ypy_++);                                 \
            *ycy_++ = ycc;                                                                \
            *out_ = (*out_ + ycc) / (*factor_);                                           \
            out_++;                                                                       \
            factor_++;                                                                    \
        }                                                                                 \
                                                                                          \
        ypy = ycy;                                                                        \
        ypf = ycf;                                                                        \
    }                                                                                     \
}

BILATERAL_V(uint8_t, byte)
BILATERAL_V(uint16_t, word)

#define BILATERAL_O(type, name)                                               \
static void bilateralo_##name(BilateralContext *s, AVFrame *out, AVFrame *in, \
                              int jobnr, int nb_jobs, int plane)              \
{                                                                             \
    const int width = s->planewidth[plane];                                   \
    const int height = s->planeheight[plane];                                 \
    const int slice_start = (height * jobnr) / nb_jobs;                       \
    const int slice_end = (height * (jobnr+1)) / nb_jobs;                     \
    const int dst_linesize = out->linesize[plane] / sizeof(type);             \
                                                                              \
    for (int i = slice_start; i < slice_end; i++) {                           \
        type *dst = (type *)out->data[plane] + i * dst_linesize;              \
        const float *const img_out_f = s->img_out_f[plane] + i * width;       \
        for (int j = 0; j < width; j++)                                       \
            dst[j] = lrintf(img_out_f[j]);                                    \
    }                                                                         \
}

BILATERAL_O(uint8_t, byte)
BILATERAL_O(uint16_t, word)

static int bilateralh_planes(AVFilterContext *ctx, void *arg,
                             int jobnr, int nb_jobs)
{
    BilateralContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (!(s->planes & (1 << plane)))
            continue;

        if (s->depth <= 8)
           bilateralh_byte(s, out, in, jobnr, nb_jobs, plane);
        else
           bilateralh_word(s, out, in, jobnr, nb_jobs, plane);
    }

    return 0;
}

static int bilateralv_planes(AVFilterContext *ctx, void *arg,
                             int jobnr, int nb_jobs)
{
    BilateralContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (!(s->planes & (1 << plane)))
            continue;

        if (s->depth <= 8)
           bilateralv_byte(s, out, in, jobnr, nb_jobs, plane);
        else
           bilateralv_word(s, out, in, jobnr, nb_jobs, plane);
    }

    return 0;
}

static int bilateralo_planes(AVFilterContext *ctx, void *arg,
                            int jobnr, int nb_jobs)
{
    BilateralContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (!(s->planes & (1 << plane))) {
            if (out != in) {
                const int height = s->planeheight[plane];
                const int slice_start = (height * jobnr) / nb_jobs;
                const int slice_end = (height * (jobnr+1)) / nb_jobs;
                const int width = s->planewidth[plane];
                const int linesize = in->linesize[plane];
                const int dst_linesize = out->linesize[plane];
                const uint8_t *src = in->data[plane];
                uint8_t *dst = out->data[plane];

                av_image_copy_plane(dst + slice_start * dst_linesize,
                                    dst_linesize,
                                    src + slice_start * linesize,
                                    linesize,
                                    width * ((s->depth + 7) / 8),
                                    slice_end - slice_start);
            }
            continue;
        }

        if (s->depth <= 8)
           bilateralo_byte(s, out, in, jobnr, nb_jobs, plane);
        else
           bilateralo_word(s, out, in, jobnr, nb_jobs, plane);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    BilateralContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

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

    td.in = in;
    td.out = out;
    ff_filter_execute(ctx, bilateralh_planes, &td, NULL, s->nb_threads);
    ff_filter_execute(ctx, bilateralv_planes, &td, NULL, s->nb_threads);
    ff_filter_execute(ctx, bilateralo_planes, &td, NULL, s->nb_threads);

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BilateralContext *s = ctx->priv;

    for (int p = 0; p < s->nb_planes; p++) {
        av_freep(&s->img_out_f[p]);
        av_freep(&s->img_temp[p]);
        av_freep(&s->map_factor_a[p]);
        av_freep(&s->map_factor_b[p]);
        av_freep(&s->slice_factor_a[p]);
        av_freep(&s->slice_factor_b[p]);
        av_freep(&s->line_factor_a[p]);
        av_freep(&s->line_factor_b[p]);
    }
}

static int process_command(AVFilterContext *ctx,
                           const char *cmd,
                           const char *arg,
                           char *res,
                           int res_len,
                           int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);

    if (ret < 0)
        return ret;

    return config_params(ctx);
}

static const AVFilterPad bilateral_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_bilateral = {
    .p.name        = "bilateral",
    .p.description = NULL_IF_CONFIG_SMALL("Apply Bilateral filter."),
    .p.priv_class  = &bilateral_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                     AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(BilateralContext),
    .uninit        = uninit,
    FILTER_INPUTS(bilateral_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = process_command,
};
