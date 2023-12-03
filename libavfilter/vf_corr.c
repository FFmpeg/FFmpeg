/*
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

/**
 * @file
 * Calculate the correlation between two input videos.
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "framesync.h"
#include "internal.h"

typedef struct Sums {
    uint64_t s[2];
} Sums;

typedef struct QSums {
    float s[3];
} QSums;

typedef struct CorrContext {
    const AVClass *class;
    FFFrameSync fs;
    double score, min_score, max_score, score_comp[4];
    uint64_t nb_frames;
    int nb_threads;
    int is_rgb;
    uint8_t rgba_map[4];
    int max[4];
    char comps[4];
    float mean[4][2];
    Sums *sums;
    QSums *qsums;
    int nb_components;
    int planewidth[4];
    int planeheight[4];
    int (*sum_slice)(AVFilterContext *ctx, void *arg,
                     int jobnr, int nb_jobs);
    int (*corr_slice)(AVFilterContext *ctx, void *arg,
                      int jobnr, int nb_jobs);
} CorrContext;

typedef struct ThreadData {
    AVFrame *master, *ref;
} ThreadData;

#define OFFSET(x) offsetof(CorrContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static void set_meta(AVFilterContext *ctx,
                     AVDictionary **metadata, const char *key, char comp, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    if (comp) {
        char key2[128];
        snprintf(key2, sizeof(key2), "lavfi.%s.%s%s%c",
                 ctx->filter->name, ctx->filter->name, key, comp);
        av_dict_set(metadata, key2, value, 0);
    } else {
        char key2[128];
        snprintf(key2, sizeof(key2), "lavfi.%s.%s%s",
                 ctx->filter->name, ctx->filter->name, key);
        av_dict_set(metadata, key2, value, 0);
    }
}

#define SUM(type, name)                                      \
static int sum_##name(AVFilterContext *ctx, void *arg,       \
                      int jobnr, int nb_jobs)                \
{                                                            \
    CorrContext *s = ctx->priv;                              \
    ThreadData *td = arg;                                    \
    AVFrame *master = td->master;                            \
    AVFrame *ref = td->ref;                                  \
                                                             \
    for (int c = 0; c < s->nb_components; c++) {             \
        const ptrdiff_t linesize1 = master->linesize[c] /    \
                                    sizeof(type);            \
        const ptrdiff_t linesize2 = ref->linesize[c] /       \
                                    sizeof(type);            \
        const int h = s->planeheight[c];                     \
        const int w = s->planewidth[c];                      \
        const int slice_start = (h * jobnr) / nb_jobs;       \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;     \
        const type *src1 = (const type *)master->data[c] +   \
                            linesize1 * slice_start;         \
        const type *src2 = (const type *)ref->data[c] +      \
                            linesize2 * slice_start;         \
        uint64_t sum1 = 0, sum2 = 0;                         \
                                                             \
        for (int y = slice_start; y < slice_end; y++) {      \
            for (int x = 0; x < w; x++) {                    \
                sum1 += src1[x];                             \
                sum2 += src2[x];                             \
            }                                                \
                                                             \
            src1 += linesize1;                               \
            src2 += linesize2;                               \
        }                                                    \
                                                             \
        s->sums[jobnr * s->nb_components + c].s[0] = sum1;   \
        s->sums[jobnr * s->nb_components + c].s[1] = sum2;   \
    }                                                        \
                                                             \
    return 0;                                                \
}

SUM(uint8_t, slice8)
SUM(uint16_t, slice16)

#define CORR(type, name)                                     \
static int corr_##name(AVFilterContext *ctx, void *arg,      \
                       int jobnr, int nb_jobs)               \
{                                                            \
    CorrContext *s = ctx->priv;                              \
    ThreadData *td = arg;                                    \
    AVFrame *master = td->master;                            \
    AVFrame *ref = td->ref;                                  \
                                                             \
    for (int c = 0; c < s->nb_components; c++) {             \
        const ptrdiff_t linesize1 = master->linesize[c] /    \
                                    sizeof(type);            \
        const ptrdiff_t linesize2 = ref->linesize[c] /       \
                                    sizeof(type);            \
        const type *src1 = (const type *)master->data[c];    \
        const type *src2 = (const type *)ref->data[c];       \
        const int h = s->planeheight[c];                     \
        const int w = s->planewidth[c];                      \
        const int slice_start = (h * jobnr) / nb_jobs;       \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;     \
        const float scale = 1.f / s->max[c];                 \
        const float mean1 = s->mean[c][0];                   \
        const float mean2 = s->mean[c][1];                   \
        float sum12 = 0.f, sum1q = 0.f, sum2q = 0.f;         \
                                                             \
        src1 = (const type *)master->data[c] +               \
                     slice_start * linesize1;                \
        src2 = (const type *)ref->data[c] +                  \
                     slice_start * linesize2;                \
                                                             \
        for (int y = slice_start; y < slice_end; y++) {      \
            for (int x = 0; x < w; x++) {                    \
                const float f1 = scale * src1[x] - mean1;    \
                const float f2 = scale * src2[x] - mean2;    \
                                                             \
                sum12 += f1 * f2;                            \
                sum1q += f1 * f1;                            \
                sum2q += f2 * f2;                            \
            }                                                \
                                                             \
            src1 += linesize1;                               \
            src2 += linesize2;                               \
        }                                                    \
                                                             \
        s->qsums[jobnr * s->nb_components + c].s[0] = sum12; \
        s->qsums[jobnr * s->nb_components + c].s[1] = sum1q; \
        s->qsums[jobnr * s->nb_components + c].s[2] = sum2q; \
    }                                                        \
                                                             \
    return 0;                                                \
}

CORR(uint8_t, slice8)
CORR(uint16_t, slice16)

static int do_corr(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    CorrContext *s = ctx->priv;
    AVFrame *master, *ref;
    double comp_score[4], score = 0.;
    AVDictionary **metadata;
    ThreadData td;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;
    if (ctx->is_disabled || !ref)
        return ff_filter_frame(ctx->outputs[0], master);
    metadata = &master->metadata;

    td.master = master;
    td.ref = ref;
    ff_filter_execute(ctx, s->sum_slice, &td, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    for (int c = 0; c < s->nb_components; c++) {
        const double scale = 1.f / s->max[c];
        uint64_t sum1 = 0, sum2 = 0;

        for (int n = 0; n < s->nb_threads; n++) {
            sum1 += s->sums[n * s->nb_components + c].s[0];
            sum2 += s->sums[n * s->nb_components + c].s[1];
        }

        s->mean[c][0] = scale * (sum1 /(double)(s->planewidth[c] * s->planeheight[c]));
        s->mean[c][1] = scale * (sum2 /(double)(s->planewidth[c] * s->planeheight[c]));
    }

    ff_filter_execute(ctx, s->corr_slice, &td, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    for (int c = 0; c < s->nb_components; c++) {
        double sumq, sum12 = 0.0, sum1q = 0.0, sum2q = 0.0;

        for (int n = 0; n < s->nb_threads; n++) {
            sum12 += s->qsums[n * s->nb_components + c].s[0];
            sum1q += s->qsums[n * s->nb_components + c].s[1];
            sum2q += s->qsums[n * s->nb_components + c].s[2];
        }

        sumq = sqrt(sum1q * sum2q);
        if (sumq > 0.0) {
            comp_score[c] = av_clipd(sum12 / sumq,-1.0,1.0);
        } else {
            comp_score[c] = 0.f;
        }
    }

    for (int c = 0; c < s->nb_components; c++)
        score += comp_score[c];
    score /= s->nb_components;
    s->score += score;

    s->min_score = fmin(s->min_score, score);
    s->max_score = fmax(s->max_score, score);

    for (int c = 0; c < s->nb_components; c++)
        s->score_comp[c] += comp_score[c];
    s->nb_frames++;

    for (int j = 0; j < s->nb_components; j++) {
        int c = s->is_rgb ? s->rgba_map[j] : j;
        set_meta(ctx, metadata, ".", s->comps[j], comp_score[c]);
    }
    set_meta(ctx, metadata, "_avg", 0, score);

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    CorrContext *s = ctx->priv;

    s->fs.on_event = do_corr;

    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
#define PF_NOALPHA(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
#define PF_ALPHA(suf)   AV_PIX_FMT_YUVA420##suf, AV_PIX_FMT_YUVA422##suf, AV_PIX_FMT_YUVA444##suf
#define PF(suf)         PF_NOALPHA(suf), PF_ALPHA(suf)
    PF(P), PF(P9), PF(P10), PF_NOALPHA(P12), PF_NOALPHA(P14), PF(P16),
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int config_input_ref(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx  = inlink->dst;
    CorrContext *s = ctx->priv;

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->nb_components = desc->nb_components;
    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
        return AVERROR(EINVAL);
    }

    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;
    s->comps[0] = s->is_rgb ? 'R' : 'Y' ;
    s->comps[1] = s->is_rgb ? 'G' : 'U' ;
    s->comps[2] = s->is_rgb ? 'B' : 'V' ;
    s->comps[3] = 'A';

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->sums = av_calloc(s->nb_threads * s->nb_components, sizeof(*s->sums));
    s->qsums = av_calloc(s->nb_threads * s->nb_components, sizeof(*s->qsums));
    if (!s->qsums || !s->sums)
        return AVERROR(ENOMEM);

    s->min_score = +INFINITY;
    s->max_score = -INFINITY;

    s->max[0] = (1 << desc->comp[0].depth) - 1;
    s->max[1] = (1 << desc->comp[1].depth) - 1;
    s->max[2] = (1 << desc->comp[2].depth) - 1;
    s->max[3] = (1 << desc->comp[3].depth) - 1;

    s->sum_slice = desc->comp[0].depth > 8 ? sum_slice16 : sum_slice8;
    s->corr_slice = desc->comp[0].depth > 8 ? corr_slice16 : corr_slice8;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    CorrContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    int ret;

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;
    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    outlink->time_base = s->fs.time_base;

    if (av_cmp_q(mainlink->time_base, outlink->time_base) ||
        av_cmp_q(ctx->inputs[1]->time_base, outlink->time_base))
        av_log(ctx, AV_LOG_WARNING, "not matching timebases found between first input: %d/%d and second input %d/%d, results may be incorrect!\n",
               mainlink->time_base.num, mainlink->time_base.den,
               ctx->inputs[1]->time_base.num, ctx->inputs[1]->time_base.den);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    CorrContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CorrContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        char buf[256];

        buf[0] = 0;
        for (int j = 0; j < s->nb_components; j++) {
            int c = s->is_rgb ? s->rgba_map[j] : j;
            av_strlcatf(buf, sizeof(buf), " %c:%f", s->comps[j], s->score_comp[c] / s->nb_frames);
        }

        av_log(ctx, AV_LOG_INFO, "%s%s average:%f min:%f max:%f\n",
               ctx->filter->name,
               buf,
               s->score / s->nb_frames,
               s->min_score,
               s->max_score);
    }

    ff_framesync_uninit(&s->fs);
    av_freep(&s->qsums);
    av_freep(&s->sums);
}

static const AVFilterPad corr_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },{
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
};

static const AVFilterPad corr_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

static const AVOption options[] = {
    { NULL }
};

#define corr_options options
FRAMESYNC_DEFINE_CLASS(corr, CorrContext, fs);

const AVFilter ff_vf_corr = {
    .name          = "corr",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the correlation between two video streams."),
    .preinit       = corr_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(CorrContext),
    .priv_class    = &corr_class,
    FILTER_INPUTS(corr_inputs),
    FILTER_OUTPUTS(corr_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS             |
                     AVFILTER_FLAG_METADATA_ONLY,
};
