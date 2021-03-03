/*
 * Copyright (c) 2021 Paul B Mahol
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

/**
 * @file
 * Caculate the Identity between two input videos.
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

typedef struct IdentityContext {
    const AVClass *class;
    FFFrameSync fs;
    double score, min_score, max_score, score_comp[4];
    uint64_t nb_frames;
    int is_rgb;
    uint8_t rgba_map[4];
    char comps[4];
    int nb_components;
    int nb_threads;
    int planewidth[4];
    int planeheight[4];
    uint64_t **scores;
    unsigned (*filter_line)(const uint8_t *buf, const uint8_t *ref, int w);
} IdentityContext;

#define OFFSET(x) offsetof(IdentityContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption identity_options[] = {
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(identity, IdentityContext, fs);

static unsigned identity_line_8bit(const uint8_t *main_line,  const uint8_t *ref_line, int outw)
{
    unsigned score = 0;

    for (int j = 0; j < outw; j++)
        score += main_line[j] == ref_line[j];

    return score;
}

static unsigned identity_line_16bit(const uint8_t *mmain_line, const uint8_t *rref_line, int outw)
{
    const uint16_t *main_line = (const uint16_t *)mmain_line;
    const uint16_t *ref_line = (const uint16_t *)rref_line;
    unsigned score = 0;

    for (int j = 0; j < outw; j++)
        score += main_line[j] == ref_line[j];

    return score;
}

typedef struct ThreadData {
    const uint8_t *main_data[4];
    const uint8_t *ref_data[4];
    int main_linesize[4];
    int ref_linesize[4];
    int planewidth[4];
    int planeheight[4];
    uint64_t **score;
    int nb_components;
} ThreadData;

static
int compute_images_identity(AVFilterContext *ctx, void *arg,
                            int jobnr, int nb_jobs)
{
    IdentityContext *s = ctx->priv;
    ThreadData *td = arg;
    uint64_t *score = td->score[jobnr];

    for (int c = 0; c < td->nb_components; c++) {
        const int outw = td->planewidth[c];
        const int outh = td->planeheight[c];
        const int slice_start = (outh * jobnr) / nb_jobs;
        const int slice_end = (outh * (jobnr+1)) / nb_jobs;
        const int ref_linesize = td->ref_linesize[c];
        const int main_linesize = td->main_linesize[c];
        const uint8_t *main_line = td->main_data[c] + main_linesize * slice_start;
        const uint8_t *ref_line = td->ref_data[c] + ref_linesize * slice_start;
        uint64_t m = 0;

        for (int i = slice_start; i < slice_end; i++) {
            m += s->filter_line(main_line, ref_line, outw);
            ref_line += ref_linesize;
            main_line += main_linesize;
        }
        score[c] = m;
    }

    return 0;
}

static void set_meta(AVDictionary **metadata, const char *key, char comp, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    if (comp) {
        char key2[128];
        snprintf(key2, sizeof(key2), "%s%c", key, comp);
        av_dict_set(metadata, key2, value, 0);
    } else {
        av_dict_set(metadata, key, value, 0);
    }
}

static int do_identity(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    IdentityContext *s = ctx->priv;
    AVFrame *master, *ref;
    double comp_identity[4], score = 0.;
    uint64_t comp_sum[4] = { 0 };
    AVDictionary **metadata;
    ThreadData td;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;
    if (ctx->is_disabled || !ref)
        return ff_filter_frame(ctx->outputs[0], master);
    metadata = &master->metadata;

    td.nb_components = s->nb_components;
    td.score = s->scores;
    for (int c = 0; c < s->nb_components; c++) {
        td.main_data[c] = master->data[c];
        td.ref_data[c] = ref->data[c];
        td.main_linesize[c] = master->linesize[c];
        td.ref_linesize[c] = ref->linesize[c];
        td.planewidth[c] = s->planewidth[c];
        td.planeheight[c] = s->planeheight[c];
    }

    ctx->internal->execute(ctx, compute_images_identity, &td, NULL, FFMIN(s->planeheight[1], s->nb_threads));

    for (int j = 0; j < s->nb_threads; j++) {
        for (int c = 0; c < s->nb_components; c++)
            comp_sum[c] += s->scores[j][c];
    }

    for (int c = 0; c < s->nb_components; c++)
        comp_identity[c] = comp_sum[c] / ((double)s->planewidth[c] * s->planeheight[c]);

    for (int c = 0; c < s->nb_components; c++)
        score += comp_identity[c];
    score /= s->nb_components;

    s->min_score = FFMIN(s->min_score, score);
    s->max_score = FFMAX(s->max_score, score);

    s->score += score;

    for (int j = 0; j < s->nb_components; j++)
        s->score_comp[j] += comp_identity[j];
    s->nb_frames++;

    for (int j = 0; j < s->nb_components; j++) {
        int c = s->is_rgb ? s->rgba_map[j] : j;
        set_meta(metadata, "lavfi.identity.identity.", s->comps[j], comp_identity[c]);
    }
    set_meta(metadata, "lavfi.identity.identity_avg", 0, score);

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    IdentityContext *s = ctx->priv;

    s->fs.on_event = do_identity;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
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

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input_ref(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx  = inlink->dst;
    IdentityContext *s = ctx->priv;

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->nb_components = desc->nb_components;
    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "Inputs must be of same pixel format.\n");
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

    s->scores = av_calloc(s->nb_threads, sizeof(*s->scores));
    if (!s->scores)
        return AVERROR(ENOMEM);

    for (int t = 0; t < s->nb_threads && s->scores; t++) {
        s->scores[t] = av_calloc(s->nb_components, sizeof(*s->scores[0]));
        if (!s->scores[t])
            return AVERROR(ENOMEM);
    }

    s->min_score = +INFINITY;
    s->max_score = -INFINITY;

    s->filter_line = desc->comp[0].depth > 8 ? identity_line_16bit : identity_line_8bit;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    IdentityContext *s = ctx->priv;
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
    IdentityContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    IdentityContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        char buf[256];

        buf[0] = 0;
        for (int j = 0; j < s->nb_components; j++) {
            int c = s->is_rgb ? s->rgba_map[j] : j;
            av_strlcatf(buf, sizeof(buf), " %c:%f", s->comps[j], s->score_comp[c] / s->nb_frames);
        }

        av_log(ctx, AV_LOG_INFO, "Identity%s average:%f min:%f max:%f\n",
               buf,
               s->score / s->nb_frames,
               s->min_score,
               s->max_score);
    }

    ff_framesync_uninit(&s->fs);
    for (int t = 0; t < s->nb_threads && s->scores; t++)
        av_freep(&s->scores[t]);
    av_freep(&s->scores);
}

static const AVFilterPad identity_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },{
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
    { NULL }
};

static const AVFilterPad identity_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_identity = {
    .name          = "identity",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the Identity between two video streams."),
    .preinit       = identity_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(IdentityContext),
    .priv_class    = &identity_class,
    .inputs        = identity_inputs,
    .outputs       = identity_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
