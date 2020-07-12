/*
 * Filter layer - format negotiation
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"

#define KNOWN(l) (!FF_LAYOUT2COUNT(l)) /* for readability */

/**
 * Add all refs from a to ret and destroy a.
 */
#define MERGE_REF(ret, a, fmts, type, fail)                                \
do {                                                                       \
    type ***tmp;                                                           \
    int i;                                                                 \
                                                                           \
    if (!(tmp = av_realloc_array(ret->refs, ret->refcount + a->refcount,   \
                                 sizeof(*tmp))))                           \
        goto fail;                                                         \
    ret->refs = tmp;                                                       \
                                                                           \
    for (i = 0; i < a->refcount; i ++) {                                   \
        ret->refs[ret->refcount] = a->refs[i];                             \
        *ret->refs[ret->refcount++] = ret;                                 \
    }                                                                      \
                                                                           \
    av_freep(&a->refs);                                                    \
    av_freep(&a->fmts);                                                    \
    av_freep(&a);                                                          \
} while (0)

/**
 * Add all formats common for a and b to ret, copy the refs and destroy
 * a and b.
 */
#define MERGE_FORMATS(ret, a, b, fmts, nb, type, fail)                          \
do {                                                                            \
    int i, j, k = 0, count = FFMIN(a->nb, b->nb);                               \
                                                                                \
    if (!(ret = av_mallocz(sizeof(*ret))))                                      \
        goto fail;                                                              \
                                                                                \
    if (count) {                                                                \
        if (!(ret->fmts = av_malloc_array(count, sizeof(*ret->fmts))))          \
            goto fail;                                                          \
        for (i = 0; i < a->nb; i++)                                             \
            for (j = 0; j < b->nb; j++)                                         \
                if (a->fmts[i] == b->fmts[j]) {                                 \
                    if(k >= FFMIN(a->nb, b->nb)){                               \
                        av_log(NULL, AV_LOG_ERROR, "Duplicate formats in %s detected\n", __FUNCTION__); \
                        av_free(ret->fmts);                                     \
                        av_free(ret);                                           \
                        return NULL;                                            \
                    }                                                           \
                    ret->fmts[k++] = a->fmts[i];                                \
                }                                                               \
    }                                                                           \
    ret->nb = k;                                                                \
    /* check that there was at least one common format */                       \
    if (!ret->nb)                                                               \
        goto fail;                                                              \
                                                                                \
    MERGE_REF(ret, a, fmts, type, fail);                                        \
    MERGE_REF(ret, b, fmts, type, fail);                                        \
} while (0)

AVFilterFormats *ff_merge_formats(AVFilterFormats *a, AVFilterFormats *b,
                                  enum AVMediaType type)
{
    AVFilterFormats *ret = NULL;
    int i, j;
    int alpha1=0, alpha2=0;
    int chroma1=0, chroma2=0;

    if (a == b)
        return a;

    /* Do not lose chroma or alpha in merging.
       It happens if both lists have formats with chroma (resp. alpha), but
       the only formats in common do not have it (e.g. YUV+gray vs.
       RGB+gray): in that case, the merging would select the gray format,
       possibly causing a lossy conversion elsewhere in the graph.
       To avoid that, pretend that there are no common formats to force the
       insertion of a conversion filter. */
    if (type == AVMEDIA_TYPE_VIDEO)
        for (i = 0; i < a->nb_formats; i++)
            for (j = 0; j < b->nb_formats; j++) {
                const AVPixFmtDescriptor *adesc = av_pix_fmt_desc_get(a->formats[i]);
                const AVPixFmtDescriptor *bdesc = av_pix_fmt_desc_get(b->formats[j]);
                alpha2 |= adesc->flags & bdesc->flags & AV_PIX_FMT_FLAG_ALPHA;
                chroma2|= adesc->nb_components > 1 && bdesc->nb_components > 1;
                if (a->formats[i] == b->formats[j]) {
                    alpha1 |= adesc->flags & AV_PIX_FMT_FLAG_ALPHA;
                    chroma1|= adesc->nb_components > 1;
                }
            }

    // If chroma or alpha can be lost through merging then do not merge
    if (alpha2 > alpha1 || chroma2 > chroma1)
        return NULL;

    MERGE_FORMATS(ret, a, b, formats, nb_formats, AVFilterFormats, fail);

    return ret;
fail:
    if (ret) {
        av_freep(&ret->refs);
        av_freep(&ret->formats);
    }
    av_freep(&ret);
    return NULL;
}

AVFilterFormats *ff_merge_samplerates(AVFilterFormats *a,
                                      AVFilterFormats *b)
{
    AVFilterFormats *ret = NULL;

    if (a == b) return a;

    if (a->nb_formats && b->nb_formats) {
        MERGE_FORMATS(ret, a, b, formats, nb_formats, AVFilterFormats, fail);
    } else if (a->nb_formats) {
        MERGE_REF(a, b, formats, AVFilterFormats, fail);
        ret = a;
    } else {
        MERGE_REF(b, a, formats, AVFilterFormats, fail);
        ret = b;
    }

    return ret;
fail:
    if (ret) {
        av_freep(&ret->refs);
        av_freep(&ret->formats);
    }
    av_freep(&ret);
    return NULL;
}

AVFilterChannelLayouts *ff_merge_channel_layouts(AVFilterChannelLayouts *a,
                                                 AVFilterChannelLayouts *b)
{
    AVFilterChannelLayouts *ret = NULL;
    unsigned a_all = a->all_layouts + a->all_counts;
    unsigned b_all = b->all_layouts + b->all_counts;
    int ret_max, ret_nb = 0, i, j, round;

    if (a == b) return a;

    /* Put the most generic set in a, to avoid doing everything twice */
    if (a_all < b_all) {
        FFSWAP(AVFilterChannelLayouts *, a, b);
        FFSWAP(unsigned, a_all, b_all);
    }
    if (a_all) {
        if (a_all == 1 && !b_all) {
            /* keep only known layouts in b; works also for b_all = 1 */
            for (i = j = 0; i < b->nb_channel_layouts; i++)
                if (KNOWN(b->channel_layouts[i]))
                    b->channel_layouts[j++] = b->channel_layouts[i];
            /* Not optimal: the unknown layouts of b may become known after
               another merge. */
            if (!j)
                return NULL;
            b->nb_channel_layouts = j;
        }
        MERGE_REF(b, a, channel_layouts, AVFilterChannelLayouts, fail);
        return b;
    }

    ret_max = a->nb_channel_layouts + b->nb_channel_layouts;
    if (!(ret = av_mallocz(sizeof(*ret))) ||
        !(ret->channel_layouts = av_malloc_array(ret_max,
                                                 sizeof(*ret->channel_layouts))))
        goto fail;

    /* a[known] intersect b[known] */
    for (i = 0; i < a->nb_channel_layouts; i++) {
        if (!KNOWN(a->channel_layouts[i]))
            continue;
        for (j = 0; j < b->nb_channel_layouts; j++) {
            if (a->channel_layouts[i] == b->channel_layouts[j]) {
                ret->channel_layouts[ret_nb++] = a->channel_layouts[i];
                a->channel_layouts[i] = b->channel_layouts[j] = 0;
            }
        }
    }
    /* 1st round: a[known] intersect b[generic]
       2nd round: a[generic] intersect b[known] */
    for (round = 0; round < 2; round++) {
        for (i = 0; i < a->nb_channel_layouts; i++) {
            uint64_t fmt = a->channel_layouts[i], bfmt;
            if (!fmt || !KNOWN(fmt))
                continue;
            bfmt = FF_COUNT2LAYOUT(av_get_channel_layout_nb_channels(fmt));
            for (j = 0; j < b->nb_channel_layouts; j++)
                if (b->channel_layouts[j] == bfmt)
                    ret->channel_layouts[ret_nb++] = a->channel_layouts[i];
        }
        /* 1st round: swap to prepare 2nd round; 2nd round: put it back */
        FFSWAP(AVFilterChannelLayouts *, a, b);
    }
    /* a[generic] intersect b[generic] */
    for (i = 0; i < a->nb_channel_layouts; i++) {
        if (KNOWN(a->channel_layouts[i]))
            continue;
        for (j = 0; j < b->nb_channel_layouts; j++)
            if (a->channel_layouts[i] == b->channel_layouts[j])
                ret->channel_layouts[ret_nb++] = a->channel_layouts[i];
    }

    ret->nb_channel_layouts = ret_nb;
    if (!ret->nb_channel_layouts)
        goto fail;
    MERGE_REF(ret, a, channel_layouts, AVFilterChannelLayouts, fail);
    MERGE_REF(ret, b, channel_layouts, AVFilterChannelLayouts, fail);
    return ret;

fail:
    if (ret) {
        av_freep(&ret->refs);
        av_freep(&ret->channel_layouts);
    }
    av_freep(&ret);
    return NULL;
}

int ff_fmt_is_in(int fmt, const int *fmts)
{
    const int *p;

    for (p = fmts; *p != -1; p++) {
        if (fmt == *p)
            return 1;
    }
    return 0;
}

#define MAKE_FORMAT_LIST(type, field, count_field)                      \
    type *formats;                                                      \
    int count = 0;                                                      \
    if (fmts)                                                           \
        for (count = 0; fmts[count] != -1; count++)                     \
            ;                                                           \
    formats = av_mallocz(sizeof(*formats));                             \
    if (!formats)                                                       \
        return NULL;                                                    \
    formats->count_field = count;                                       \
    if (count) {                                                        \
        formats->field = av_malloc_array(count, sizeof(*formats->field));      \
        if (!formats->field) {                                          \
            av_freep(&formats);                                         \
            return NULL;                                                \
        }                                                               \
    }

AVFilterFormats *ff_make_format_list(const int *fmts)
{
    MAKE_FORMAT_LIST(AVFilterFormats, formats, nb_formats);
    while (count--)
        formats->formats[count] = fmts[count];

    return formats;
}

AVFilterChannelLayouts *ff_make_formatu64_list(const uint64_t *fmts)
{
    MAKE_FORMAT_LIST(AVFilterChannelLayouts,
                     channel_layouts, nb_channel_layouts);
    if (count)
        memcpy(formats->channel_layouts, fmts,
               sizeof(*formats->channel_layouts) * count);

    return formats;
}

AVFilterChannelLayouts *avfilter_make_format64_list(const int64_t *fmts)
{
    MAKE_FORMAT_LIST(AVFilterChannelLayouts,
                     channel_layouts, nb_channel_layouts);
    if (count)
        memcpy(formats->channel_layouts, fmts,
               sizeof(*formats->channel_layouts) * count);

    return formats;
}

#define ADD_FORMAT(f, fmt, unref_fn, type, list, nb)        \
do {                                                        \
    type *fmts;                                             \
    void *oldf = *f;                                        \
                                                            \
    if (!(*f) && !(*f = av_mallocz(sizeof(**f)))) {         \
        return AVERROR(ENOMEM);                             \
    }                                                       \
                                                            \
    fmts = av_realloc_array((*f)->list, (*f)->nb + 1,       \
                            sizeof(*(*f)->list));           \
    if (!fmts) {                                            \
        unref_fn(f);                                        \
        if (!oldf)                                          \
            av_freep(f);                                    \
        return AVERROR(ENOMEM);                             \
    }                                                       \
                                                            \
    (*f)->list = fmts;                                      \
    (*f)->list[(*f)->nb++] = fmt;                           \
} while (0)

int ff_add_format(AVFilterFormats **avff, int64_t fmt)
{
    ADD_FORMAT(avff, fmt, ff_formats_unref, int, formats, nb_formats);
    return 0;
}

int ff_add_channel_layout(AVFilterChannelLayouts **l, uint64_t channel_layout)
{
    av_assert1(!(*l && (*l)->all_layouts));
    ADD_FORMAT(l, channel_layout, ff_channel_layouts_unref, uint64_t, channel_layouts, nb_channel_layouts);
    return 0;
}

AVFilterFormats *ff_all_formats(enum AVMediaType type)
{
    AVFilterFormats *ret = NULL;

    if (type == AVMEDIA_TYPE_VIDEO) {
        const AVPixFmtDescriptor *desc = NULL;
        while ((desc = av_pix_fmt_desc_next(desc))) {
            if (ff_add_format(&ret, av_pix_fmt_desc_get_id(desc)) < 0)
                return NULL;
        }
    } else if (type == AVMEDIA_TYPE_AUDIO) {
        enum AVSampleFormat fmt = 0;
        while (av_get_sample_fmt_name(fmt)) {
            if (ff_add_format(&ret, fmt) < 0)
                return NULL;
            fmt++;
        }
    }

    return ret;
}

int ff_formats_pixdesc_filter(AVFilterFormats **rfmts, unsigned want, unsigned rej)
{
    unsigned nb_formats, fmt, flags;
    AVFilterFormats *formats = NULL;

    while (1) {
        nb_formats = 0;
        for (fmt = 0;; fmt++) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
            if (!desc)
                break;
            flags = desc->flags;
            if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL) &&
                !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
                (desc->log2_chroma_w || desc->log2_chroma_h))
                flags |= FF_PIX_FMT_FLAG_SW_FLAT_SUB;
            if ((flags & (want | rej)) != want)
                continue;
            if (formats)
                formats->formats[nb_formats] = fmt;
            nb_formats++;
        }
        if (formats) {
            av_assert0(formats->nb_formats == nb_formats);
            *rfmts = formats;
            return 0;
        }
        formats = av_mallocz(sizeof(*formats));
        if (!formats)
            return AVERROR(ENOMEM);
        formats->nb_formats = nb_formats;
        if (nb_formats) {
            formats->formats = av_malloc_array(nb_formats, sizeof(*formats->formats));
            if (!formats->formats) {
                av_freep(&formats);
                return AVERROR(ENOMEM);
            }
        }
    }
}

AVFilterFormats *ff_planar_sample_fmts(void)
{
    AVFilterFormats *ret = NULL;
    int fmt;

    for (fmt = 0; av_get_bytes_per_sample(fmt)>0; fmt++)
        if (av_sample_fmt_is_planar(fmt))
            if (ff_add_format(&ret, fmt) < 0)
                return NULL;

    return ret;
}

AVFilterFormats *ff_all_samplerates(void)
{
    AVFilterFormats *ret = av_mallocz(sizeof(*ret));
    return ret;
}

AVFilterChannelLayouts *ff_all_channel_layouts(void)
{
    AVFilterChannelLayouts *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;
    ret->all_layouts = 1;
    return ret;
}

AVFilterChannelLayouts *ff_all_channel_counts(void)
{
    AVFilterChannelLayouts *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;
    ret->all_layouts = ret->all_counts = 1;
    return ret;
}

#define FORMATS_REF(f, ref, unref_fn)                                           \
    void *tmp;                                                                  \
                                                                                \
    if (!f || !ref)                                                             \
        return AVERROR(ENOMEM);                                                 \
                                                                                \
    tmp = av_realloc_array(f->refs, sizeof(*f->refs), f->refcount + 1);         \
    if (!tmp) {                                                                 \
        unref_fn(&f);                                                           \
        return AVERROR(ENOMEM);                                                 \
    }                                                                           \
    f->refs = tmp;                                                              \
    f->refs[f->refcount++] = ref;                                               \
    *ref = f;                                                                   \
    return 0

int ff_channel_layouts_ref(AVFilterChannelLayouts *f, AVFilterChannelLayouts **ref)
{
    FORMATS_REF(f, ref, ff_channel_layouts_unref);
}

int ff_formats_ref(AVFilterFormats *f, AVFilterFormats **ref)
{
    FORMATS_REF(f, ref, ff_formats_unref);
}

#define FIND_REF_INDEX(ref, idx)            \
do {                                        \
    int i;                                  \
    for (i = 0; i < (*ref)->refcount; i ++) \
        if((*ref)->refs[i] == ref) {        \
            idx = i;                        \
            break;                          \
        }                                   \
} while (0)

#define FORMATS_UNREF(ref, list)                                   \
do {                                                               \
    int idx = -1;                                                  \
                                                                   \
    if (!ref || !*ref || !(*ref)->refs)                            \
        return;                                                    \
                                                                   \
    FIND_REF_INDEX(ref, idx);                                      \
                                                                   \
    if (idx >= 0)                                                  \
        memmove((*ref)->refs + idx, (*ref)->refs + idx + 1,        \
            sizeof(*(*ref)->refs) * ((*ref)->refcount - idx - 1)); \
                                                                   \
    if(!--(*ref)->refcount) {                                      \
        av_free((*ref)->list);                                     \
        av_free((*ref)->refs);                                     \
        av_free(*ref);                                             \
    }                                                              \
    *ref = NULL;                                                   \
} while (0)

void ff_formats_unref(AVFilterFormats **ref)
{
    FORMATS_UNREF(ref, formats);
}

void ff_channel_layouts_unref(AVFilterChannelLayouts **ref)
{
    FORMATS_UNREF(ref, channel_layouts);
}

#define FORMATS_CHANGEREF(oldref, newref)       \
do {                                            \
    int idx = -1;                               \
                                                \
    FIND_REF_INDEX(oldref, idx);                \
                                                \
    if (idx >= 0) {                             \
        (*oldref)->refs[idx] = newref;          \
        *newref = *oldref;                      \
        *oldref = NULL;                         \
    }                                           \
} while (0)

void ff_channel_layouts_changeref(AVFilterChannelLayouts **oldref,
                                  AVFilterChannelLayouts **newref)
{
    FORMATS_CHANGEREF(oldref, newref);
}

void ff_formats_changeref(AVFilterFormats **oldref, AVFilterFormats **newref)
{
    FORMATS_CHANGEREF(oldref, newref);
}

#define SET_COMMON_FORMATS(ctx, fmts, in_fmts, out_fmts, ref_fn, unref_fn, list) \
    int count = 0, i;                                               \
                                                                    \
    if (!fmts)                                                      \
        return AVERROR(ENOMEM);                                     \
                                                                    \
    for (i = 0; i < ctx->nb_inputs; i++) {                          \
        if (ctx->inputs[i] && !ctx->inputs[i]->out_fmts) {          \
            int ret = ref_fn(fmts, &ctx->inputs[i]->out_fmts);      \
            if (ret < 0) {                                          \
                unref_fn(&fmts);                                    \
                if (fmts)                                           \
                    av_freep(&fmts->list);                          \
                av_freep(&fmts);                                    \
                return ret;                                         \
            }                                                       \
            count++;                                                \
        }                                                           \
    }                                                               \
    for (i = 0; i < ctx->nb_outputs; i++) {                         \
        if (ctx->outputs[i] && !ctx->outputs[i]->in_fmts) {         \
            int ret = ref_fn(fmts, &ctx->outputs[i]->in_fmts);      \
            if (ret < 0) {                                          \
                unref_fn(&fmts);                                    \
                if (fmts)                                           \
                    av_freep(&fmts->list);                          \
                av_freep(&fmts);                                    \
                return ret;                                         \
            }                                                       \
            count++;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    if (!count) {                                                   \
        av_freep(&fmts->list);                                      \
        av_freep(&fmts->refs);                                      \
        av_freep(&fmts);                                            \
    }                                                               \
                                                                    \
    return 0;

int ff_set_common_channel_layouts(AVFilterContext *ctx,
                                  AVFilterChannelLayouts *layouts)
{
    SET_COMMON_FORMATS(ctx, layouts, in_channel_layouts, out_channel_layouts,
                       ff_channel_layouts_ref, ff_channel_layouts_unref, channel_layouts);
}

int ff_set_common_samplerates(AVFilterContext *ctx,
                              AVFilterFormats *samplerates)
{
    SET_COMMON_FORMATS(ctx, samplerates, in_samplerates, out_samplerates,
                       ff_formats_ref, ff_formats_unref, formats);
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
int ff_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    SET_COMMON_FORMATS(ctx, formats, in_formats, out_formats,
                       ff_formats_ref, ff_formats_unref, formats);
}

static int default_query_formats_common(AVFilterContext *ctx,
                                        AVFilterChannelLayouts *(layouts)(void))
{
    int ret;
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    ret = ff_set_common_formats(ctx, ff_all_formats(type));
    if (ret < 0)
        return ret;
    if (type == AVMEDIA_TYPE_AUDIO) {
        ret = ff_set_common_channel_layouts(ctx, layouts());
        if (ret < 0)
            return ret;
        ret = ff_set_common_samplerates(ctx, ff_all_samplerates());
        if (ret < 0)
            return ret;
    }

    return 0;
}

int ff_default_query_formats(AVFilterContext *ctx)
{
    return default_query_formats_common(ctx, ff_all_channel_counts);
}

int ff_query_formats_all_layouts(AVFilterContext *ctx)
{
    return default_query_formats_common(ctx, ff_all_channel_layouts);
}

/* internal functions for parsing audio format arguments */

int ff_parse_pixel_format(enum AVPixelFormat *ret, const char *arg, void *log_ctx)
{
    char *tail;
    int pix_fmt = av_get_pix_fmt(arg);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        pix_fmt = strtol(arg, &tail, 0);
        if (*tail || !av_pix_fmt_desc_get(pix_fmt)) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid pixel format '%s'\n", arg);
            return AVERROR(EINVAL);
        }
    }
    *ret = pix_fmt;
    return 0;
}

int ff_parse_sample_format(int *ret, const char *arg, void *log_ctx)
{
    char *tail;
    int sfmt = av_get_sample_fmt(arg);
    if (sfmt == AV_SAMPLE_FMT_NONE) {
        sfmt = strtol(arg, &tail, 0);
        if (*tail || av_get_bytes_per_sample(sfmt)<=0) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid sample format '%s'\n", arg);
            return AVERROR(EINVAL);
        }
    }
    *ret = sfmt;
    return 0;
}

int ff_parse_time_base(AVRational *ret, const char *arg, void *log_ctx)
{
    AVRational r;
    if(av_parse_ratio(&r, arg, INT_MAX, 0, log_ctx) < 0 ||r.num<=0  ||r.den<=0) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid time base '%s'\n", arg);
        return AVERROR(EINVAL);
    }
    *ret = r;
    return 0;
}

int ff_parse_sample_rate(int *ret, const char *arg, void *log_ctx)
{
    char *tail;
    double srate = av_strtod(arg, &tail);
    if (*tail || srate < 1 || (int)srate != srate || srate > INT_MAX) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid sample rate '%s'\n", arg);
        return AVERROR(EINVAL);
    }
    *ret = srate;
    return 0;
}

int ff_parse_channel_layout(int64_t *ret, int *nret, const char *arg,
                            void *log_ctx)
{
    int64_t chlayout;
    int nb_channels;

    if (av_get_extended_channel_layout(arg, &chlayout, &nb_channels) < 0) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid channel layout '%s'\n", arg);
        return AVERROR(EINVAL);
    }
    if (!chlayout && !nret) {
        av_log(log_ctx, AV_LOG_ERROR, "Unknown channel layout '%s' is not supported.\n", arg);
        return AVERROR(EINVAL);
    }
    *ret = chlayout;
    if (nret)
        *nret = nb_channels;

    return 0;
}
