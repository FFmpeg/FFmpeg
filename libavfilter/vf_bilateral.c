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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct BilateralContext {
    const AVClass *class;

    float sigmaS;
    float sigmaR;
    int planes;

    int nb_planes;
    int depth;
    int planewidth[4];
    int planeheight[4];

    float alpha;
    float range_table[65536];

    float *img_out_f;
    float *img_temp;
    float *map_factor_a;
    float *map_factor_b;
    float *slice_factor_a;
    float *slice_factor_b;
    float *line_factor_a;
    float *line_factor_b;
} BilateralContext;

#define OFFSET(x) offsetof(BilateralContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption bilateral_options[] = {
    { "sigmaS", "set spatial sigma",    OFFSET(sigmaS), AV_OPT_TYPE_FLOAT, {.dbl=0.1}, 0.0, 512, FLAGS },
    { "sigmaR", "set range sigma",      OFFSET(sigmaR), AV_OPT_TYPE_FLOAT, {.dbl=0.1}, 0.0,   1, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=1},     0, 0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(bilateral);

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
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_input(AVFilterLink *inlink)
{
    BilateralContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    float inv_sigma_range;

    s->depth = desc->comp[0].depth;
    inv_sigma_range = 1.0f / (s->sigmaR * ((1 << s->depth) - 1));
    s->alpha = expf(-sqrtf(2.f) / s->sigmaS);

    //compute a lookup table
    for (int i = 0; i < (1 << s->depth); i++)
        s->range_table[i] = s->alpha * expf(-i * inv_sigma_range);

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->img_out_f = av_calloc(inlink->w * inlink->h, sizeof(float));
    s->img_temp = av_calloc(inlink->w * inlink->h, sizeof(float));
    s->map_factor_a = av_calloc(inlink->w * inlink->h, sizeof(float));
    s->map_factor_b = av_calloc(inlink->w * inlink->h, sizeof(float));
    s->slice_factor_a = av_calloc(inlink->w, sizeof(float));
    s->slice_factor_b = av_calloc(inlink->w, sizeof(float));
    s->line_factor_a = av_calloc(inlink->w, sizeof(float));
    s->line_factor_b = av_calloc(inlink->w, sizeof(float));

    if (!s->img_out_f ||
        !s->img_temp ||
        !s->map_factor_a ||
        !s->map_factor_b ||
        !s->slice_factor_a ||
        !s->slice_factor_a ||
        !s->line_factor_a ||
        !s->line_factor_a)
        return AVERROR(ENOMEM);

    return 0;
}

#define BILATERAL(type, name)                                                           \
static void bilateral_##name(BilateralContext *s, const uint8_t *ssrc, uint8_t *ddst,   \
                             float sigma_spatial, float sigma_range,                    \
                             int width, int height, int src_linesize, int dst_linesize) \
{                                                                                       \
    type *dst = (type *)ddst;                                                           \
    const type *src = (const type *)ssrc;                                               \
    float *img_out_f = s->img_out_f, *img_temp = s->img_temp;                           \
    float *map_factor_a = s->map_factor_a, *map_factor_b = s->map_factor_b;             \
    float *slice_factor_a = s->slice_factor_a, *slice_factor_b = s->slice_factor_b;     \
    float *line_factor_a = s->line_factor_a, *line_factor_b = s->line_factor_b;         \
    const float *range_table = s->range_table;                                          \
    const float alpha = s->alpha;                                                       \
    float ypr, ycr, *ycy, *ypy, *xcy, fp, fc;                                           \
    const float inv_alpha_ = 1.f - alpha;                                               \
    float *ycf, *ypf, *xcf, *in_factor;                                                 \
    const type *tcy, *tpy;                                                                \
    int h1;                                                                               \
                                                                                          \
    for (int y = 0; y < height; y++) {                                                    \
        float *temp_factor_x, *temp_x = &img_temp[y * width];                             \
        const type *in_x = &src[y * src_linesize];                                        \
        const type *texture_x = &src[y * src_linesize];                                   \
        type tpr;                                                                         \
                                                                                          \
        *temp_x++ = ypr = *in_x++;                                                        \
        tpr = *texture_x++;                                                               \
                                                                                          \
        temp_factor_x = &map_factor_a[y * width];                                         \
        *temp_factor_x++ = fp = 1;                                                        \
                                                                                          \
        for (int x = 1; x < width; x++) {                                                 \
            float alpha_;                                                                 \
            int range_dist;                                                               \
            type tcr = *texture_x++;                                                      \
            type dr = abs(tcr - tpr);                                                     \
                                                                                          \
            range_dist = dr;                                                              \
            alpha_ = range_table[range_dist];                                             \
            *temp_x++ = ycr = inv_alpha_*(*in_x++) + alpha_*ypr;                          \
            tpr = tcr;                                                                    \
            ypr = ycr;                                                                    \
            *temp_factor_x++ = fc = inv_alpha_ + alpha_ * fp;                             \
            fp = fc;                                                                      \
        }                                                                                 \
        --temp_x; *temp_x = 0.5f*((*temp_x) + (*--in_x));                                 \
        tpr = *--texture_x;                                                               \
        ypr = *in_x;                                                                      \
                                                                                          \
        --temp_factor_x; *temp_factor_x = 0.5f*((*temp_factor_x) + 1);                    \
        fp = 1;                                                                           \
                                                                                          \
        for (int x = width - 2; x >= 0; x--) {                                            \
            type tcr = *--texture_x;                                                      \
            type dr = abs(tcr - tpr);                                                     \
            int range_dist = dr;                                                          \
            float alpha_ = range_table[range_dist];                                       \
                                                                                          \
            ycr = inv_alpha_ * (*--in_x) + alpha_ * ypr;                                  \
            --temp_x; *temp_x = 0.5f*((*temp_x) + ycr);                                   \
            tpr = tcr;                                                                    \
            ypr = ycr;                                                                    \
                                                                                          \
            fc = inv_alpha_ + alpha_*fp;                                                  \
            --temp_factor_x;                                                              \
            *temp_factor_x = 0.5f*((*temp_factor_x) + fc);                                \
            fp = fc;                                                                      \
        }                                                                                 \
    }                                                                                     \
    memcpy(img_out_f, img_temp, sizeof(float) * width);                                   \
                                                                                          \
    in_factor = map_factor_a;                                                             \
    memcpy(map_factor_b, in_factor, sizeof(float) * width);                               \
    for (int y = 1; y < height; y++) {                                                    \
        tpy = &src[(y - 1) * src_linesize];                                               \
        tcy = &src[y * src_linesize];                                                     \
        xcy = &img_temp[y * width];                                                       \
        ypy = &img_out_f[(y - 1) * width];                                                \
        ycy = &img_out_f[y * width];                                                      \
                                                                                          \
        xcf = &in_factor[y * width];                                                      \
        ypf = &map_factor_b[(y - 1) * width];                                             \
        ycf = &map_factor_b[y * width];                                                   \
        for (int x = 0; x < width; x++) {                                                 \
            type dr = abs((*tcy++) - (*tpy++));                                           \
            int range_dist = dr;                                                          \
            float alpha_ = range_table[range_dist];                                       \
                                                                                          \
            *ycy++ = inv_alpha_*(*xcy++) + alpha_*(*ypy++);                               \
            *ycf++ = inv_alpha_*(*xcf++) + alpha_*(*ypf++);                               \
        }                                                                                 \
    }                                                                                     \
    h1 = height - 1;                                                                      \
    ycf = line_factor_a;                                                                  \
    ypf = line_factor_b;                                                                  \
    memcpy(ypf, &in_factor[h1 * width], sizeof(float) * width);                           \
    for (int x = 0; x < width; x++)                                                       \
        map_factor_b[h1 * width + x] = 0.5f*(map_factor_b[h1 * width + x] + ypf[x]);      \
                                                                                          \
    ycy = slice_factor_a;                                                                 \
    ypy = slice_factor_b;                                                                 \
    memcpy(ypy, &img_temp[h1 * width], sizeof(float) * width);                            \
    for (int x = 0, k = 0; x < width; x++) {                                              \
        int idx = h1 * width + x;                                                         \
        img_out_f[idx] = 0.5f*(img_out_f[idx] + ypy[k++]) / map_factor_b[h1 * width + x]; \
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
        for (int x = 0; x < width; x++) {                                                 \
            type dr = abs((*tcy++) - (*tpy++));                                           \
            int range_dist = dr;                                                          \
            float alpha_ = range_table[range_dist];                                       \
            float ycc, fcc = inv_alpha_*(*xcf++) + alpha_*(*ypf_++);                      \
                                                                                          \
            *ycf_++ = fcc;                                                                \
            *factor_ = 0.5f * (*factor_ + fcc);                                           \
                                                                                          \
            ycc = inv_alpha_*(*xcy++) + alpha_*(*ypy_++);                                 \
            *ycy_++ = ycc;                                                                \
            *out_ = 0.5f * (*out_ + ycc) / (*factor_);                                    \
            out_++;                                                                       \
            factor_++;                                                                    \
        }                                                                                 \
                                                                                          \
        ypy = ycy;                                                                        \
        ypf = ycf;                                                                        \
    }                                                                                     \
                                                                                          \
    for (int i = 0; i < height; i++)                                                      \
        for (int j = 0; j < width; j++)                                                   \
            dst[j + i * dst_linesize] = img_out_f[i * width + j];                         \
}

BILATERAL(uint8_t, byte)
BILATERAL(uint16_t, word)

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    BilateralContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (!(s->planes & (1 << plane))) {
            av_image_copy_plane(out->data[plane], out->linesize[plane],
                                in->data[plane], in->linesize[plane],
                                s->planewidth[plane] * ((s->depth + 7) / 8), s->planeheight[plane]);
            continue;
        }

        if (s->depth <= 8)
           bilateral_byte(s, in->data[plane], out->data[plane], s->sigmaS, s->sigmaR,
                      s->planewidth[plane], s->planeheight[plane],
                      in->linesize[plane], out->linesize[plane]);
        else
           bilateral_word(s, in->data[plane], out->data[plane], s->sigmaS, s->sigmaR,
                      s->planewidth[plane], s->planeheight[plane],
                      in->linesize[plane] / 2, out->linesize[plane] / 2);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BilateralContext *s = ctx->priv;

    av_freep(&s->img_out_f);
    av_freep(&s->img_temp);
    av_freep(&s->map_factor_a);
    av_freep(&s->map_factor_b);
    av_freep(&s->slice_factor_a);
    av_freep(&s->slice_factor_b);
    av_freep(&s->line_factor_a);
    av_freep(&s->line_factor_b);
}

static const AVFilterPad bilateral_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad bilateral_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_bilateral = {
    .name          = "bilateral",
    .description   = NULL_IF_CONFIG_SMALL("Apply Bilateral filter."),
    .priv_size     = sizeof(BilateralContext),
    .priv_class    = &bilateral_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = bilateral_inputs,
    .outputs       = bilateral_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
