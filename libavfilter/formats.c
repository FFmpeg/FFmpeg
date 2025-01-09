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
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

/**
 * Add all refs from a to ret and destroy a.
 */
#define MERGE_REF(ret, a, fmts, type, fail_statement)                      \
do {                                                                       \
    type ***tmp;                                                           \
    int i;                                                                 \
                                                                           \
    if (!(tmp = av_realloc_array(ret->refs, ret->refcount + a->refcount,   \
                                 sizeof(*tmp))))                           \
        { fail_statement }                                                 \
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
 * Add all formats common to a and b to a, add b's refs to a and destroy b.
 * If check is set, nothing is modified and it is only checked whether
 * the formats are compatible.
 * If empty_allowed is set and one of a,b->nb is zero, the lists are
 * merged; otherwise, 0 (for nonmergeability) is returned.
 */
#define MERGE_FORMATS(a, b, fmts, nb, type, check, empty_allowed)          \
do {                                                                       \
    int i, j, k = 0, skip = 0;                                             \
                                                                           \
    if (empty_allowed) {                                                   \
        if (!a->nb || !b->nb) {                                            \
            if (check)                                                     \
                return 1;                                                  \
            if (!a->nb)                                                    \
                FFSWAP(type *, a, b);                                      \
            skip = 1;                                                      \
        }                                                                  \
    }                                                                      \
    if (!skip) {                                                           \
        for (i = 0; i < a->nb; i++)                                        \
            for (j = 0; j < b->nb; j++)                                    \
                if (a->fmts[i] == b->fmts[j]) {                            \
                    if (check)                                             \
                        return 1;                                          \
                    a->fmts[k++] = a->fmts[i];                             \
                    break;                                                 \
                }                                                          \
        /* Check that there was at least one common format.                \
         * Notice that both a and b are unchanged if not. */               \
        if (!k)                                                            \
            return 0;                                                      \
        av_assert2(!check);                                                \
        a->nb = k;                                                         \
    }                                                                      \
                                                                           \
    MERGE_REF(a, b, fmts, type, return AVERROR(ENOMEM););                  \
} while (0)

static int merge_formats_internal(AVFilterFormats *a, AVFilterFormats *b,
                                  enum AVMediaType type, int check)
{
    int i, j;
    int alpha1=0, alpha2=0;
    int chroma1=0, chroma2=0;

    av_assert2(check || (a->refcount && b->refcount));

    if (a == b)
        return 1;

    /* Do not lose chroma or alpha in merging.
       It happens if both lists have formats with chroma (resp. alpha), but
       the only formats in common do not have it (e.g. YUV+gray vs.
       RGB+gray): in that case, the merging would select the gray format,
       possibly causing a lossy conversion elsewhere in the graph.
       To avoid that, pretend that there are no common formats to force the
       insertion of a conversion filter. */
    if (type == AVMEDIA_TYPE_VIDEO)
        for (i = 0; i < a->nb_formats; i++) {
            const AVPixFmtDescriptor *const adesc = av_pix_fmt_desc_get(a->formats[i]);
            for (j = 0; j < b->nb_formats; j++) {
                const AVPixFmtDescriptor *bdesc = av_pix_fmt_desc_get(b->formats[j]);
                alpha2 |= adesc->flags & bdesc->flags & AV_PIX_FMT_FLAG_ALPHA;
                chroma2|= adesc->nb_components > 1 && bdesc->nb_components > 1;
                if (a->formats[i] == b->formats[j]) {
                    alpha1 |= adesc->flags & AV_PIX_FMT_FLAG_ALPHA;
                    chroma1|= adesc->nb_components > 1;
                }
            }
        }

    // If chroma or alpha can be lost through merging then do not merge
    if (alpha2 > alpha1 || chroma2 > chroma1)
        return 0;

    MERGE_FORMATS(a, b, formats, nb_formats, AVFilterFormats, check, 0);

    return 1;
}


/**
 * Check the formats lists for compatibility for merging without actually
 * merging.
 *
 * @return 1 if they are compatible, 0 if not.
 */
static int can_merge_pix_fmts(const void *a, const void *b)
{
    return merge_formats_internal((AVFilterFormats *)a,
                                  (AVFilterFormats *)b, AVMEDIA_TYPE_VIDEO, 1);
}

/**
 * Merge the formats lists if they are compatible and update all the
 * references of a and b to point to the combined list and free the old
 * lists as needed. The combined list usually contains the intersection of
 * the lists of a and b.
 *
 * Both a and b must have owners (i.e. refcount > 0) for these functions.
 *
 * @return 1 if merging succeeded, 0 if a and b are incompatible
 *         and negative AVERROR code on failure.
 *         a and b are unmodified if 0 is returned.
 */
static int merge_pix_fmts(void *a, void *b)
{
    return merge_formats_internal(a, b, AVMEDIA_TYPE_VIDEO, 0);
}

/**
 * See can_merge_pix_fmts().
 */
static int can_merge_sample_fmts(const void *a, const void *b)
{
    return merge_formats_internal((AVFilterFormats *)a,
                                  (AVFilterFormats *)b, AVMEDIA_TYPE_AUDIO, 1);
}

/**
 * See merge_pix_fmts().
 */
static int merge_sample_fmts(void *a, void *b)
{
    return merge_formats_internal(a, b, AVMEDIA_TYPE_AUDIO, 0);
}

static int merge_samplerates_internal(AVFilterFormats *a,
                                      AVFilterFormats *b, int check)
{
    av_assert2(check || (a->refcount && b->refcount));
    if (a == b) return 1;

    MERGE_FORMATS(a, b, formats, nb_formats, AVFilterFormats, check, 1);
    return 1;
}

/**
 * See can_merge_pix_fmts().
 */
static int can_merge_samplerates(const void *a, const void *b)
{
    return merge_samplerates_internal((AVFilterFormats *)a, (AVFilterFormats *)b, 1);
}

/**
 * See merge_pix_fmts().
 */
static int merge_samplerates(void *a, void *b)
{
    return merge_samplerates_internal(a, b, 0);
}

/**
 * See merge_pix_fmts().
 */
static int merge_channel_layouts_internal(AVFilterChannelLayouts *a,
                                          AVFilterChannelLayouts *b, int check)
{
    AVChannelLayout *channel_layouts = NULL;
    unsigned a_all = a->all_layouts + a->all_counts;
    unsigned b_all = b->all_layouts + b->all_counts;
    int ret_max, ret_nb = 0, i, j, round;

    av_assert2(a->refcount && b->refcount);

    if (a == b) return 1;

    /* Put the most generic set in a, to avoid doing everything twice */
    if (a_all < b_all) {
        FFSWAP(AVFilterChannelLayouts *, a, b);
        FFSWAP(unsigned, a_all, b_all);
    }
    if (a_all) {
        if (a_all == 1 && !b_all) {
            /* keep only known layouts in b; works also for b_all = 1 */
            for (i = j = 0; i < b->nb_channel_layouts; i++)
                if (KNOWN(&b->channel_layouts[i]) && i != j++) {
                    if (check)
                        return 1;
                    av_channel_layout_copy(&b->channel_layouts[j], &b->channel_layouts[i]);
                }
            /* Not optimal: the unknown layouts of b may become known after
               another merge. */
            if (!j)
                return 0;
            b->nb_channel_layouts = j;
        }
        MERGE_REF(b, a, channel_layouts, AVFilterChannelLayouts, return AVERROR(ENOMEM););
        return 1;
    }

    ret_max = a->nb_channel_layouts + b->nb_channel_layouts;
    if (!check && !(channel_layouts = av_calloc(ret_max, sizeof(*channel_layouts))))
        return AVERROR(ENOMEM);

    /* a[known] intersect b[known] */
    for (i = 0; i < a->nb_channel_layouts; i++) {
        if (!KNOWN(&a->channel_layouts[i]))
            continue;
        for (j = 0; j < b->nb_channel_layouts; j++) {
            if (!av_channel_layout_compare(&a->channel_layouts[i], &b->channel_layouts[j])) {
                if (check)
                    return 1;
                av_channel_layout_copy(&channel_layouts[ret_nb++], &a->channel_layouts[i]);
                av_channel_layout_uninit(&a->channel_layouts[i]);
                av_channel_layout_uninit(&b->channel_layouts[j]);
                break;
            }
        }
    }
    /* 1st round: a[known] intersect b[generic]
       2nd round: a[generic] intersect b[known] */
    for (round = 0; round < 2; round++) {
        for (i = 0; i < a->nb_channel_layouts; i++) {
            AVChannelLayout *fmt = &a->channel_layouts[i], bfmt = { 0 };
            if (!av_channel_layout_check(fmt) || !KNOWN(fmt))
                continue;
            bfmt = FF_COUNT2LAYOUT(fmt->nb_channels);
            for (j = 0; j < b->nb_channel_layouts; j++)
                if (!av_channel_layout_compare(&b->channel_layouts[j], &bfmt)) {
                    if (check)
                        return 1;
                    av_channel_layout_copy(&channel_layouts[ret_nb++], fmt);
                }
        }
        /* 1st round: swap to prepare 2nd round; 2nd round: put it back */
        FFSWAP(AVFilterChannelLayouts *, a, b);
    }
    /* a[generic] intersect b[generic] */
    for (i = 0; i < a->nb_channel_layouts; i++) {
        if (KNOWN(&a->channel_layouts[i]))
            continue;
        for (j = 0; j < b->nb_channel_layouts; j++)
            if (!av_channel_layout_compare(&a->channel_layouts[i], &b->channel_layouts[j])) {
                if (check)
                    return 1;
                av_channel_layout_copy(&channel_layouts[ret_nb++], &a->channel_layouts[i]);
            }
    }

    if (!ret_nb) {
        av_free(channel_layouts);
        return 0;
    }

    if (a->refcount > b->refcount)
        FFSWAP(AVFilterChannelLayouts *, a, b);

    MERGE_REF(b, a, channel_layouts, AVFilterChannelLayouts,
              { av_free(channel_layouts); return AVERROR(ENOMEM); });
    av_freep(&b->channel_layouts);
    b->channel_layouts    = channel_layouts;
    b->nb_channel_layouts = ret_nb;
    return 1;
}

static int can_merge_channel_layouts(const void *a, const void *b)
{
    return merge_channel_layouts_internal((AVFilterChannelLayouts *)a,
                                          (AVFilterChannelLayouts *)b, 1);
}

static int merge_channel_layouts(void *a, void *b)
{
    return merge_channel_layouts_internal(a, b, 0);
}

static int merge_generic_internal(AVFilterFormats *a,
                                  AVFilterFormats *b, int check)
{
    av_assert2(check || (a->refcount && b->refcount));

    if (a == b)
        return 1;

    MERGE_FORMATS(a, b, formats, nb_formats, AVFilterFormats, check, 0);

    return 1;
}

static int can_merge_generic(const void *a, const void *b)
{
    return merge_generic_internal((AVFilterFormats *)a,
                                  (AVFilterFormats *)b, 1);
}

static int merge_generic(void *a, void *b)
{
    return merge_generic_internal(a, b, 0);
}

static const AVFilterFormatsMerger mergers_video[] = {
    {
        .offset     = offsetof(AVFilterFormatsConfig, formats),
        .merge      = merge_pix_fmts,
        .can_merge  = can_merge_pix_fmts,
    },
    {
        .offset     = offsetof(AVFilterFormatsConfig, color_spaces),
        .merge      = merge_generic,
        .can_merge  = can_merge_generic,
    },
    {
        .offset     = offsetof(AVFilterFormatsConfig, color_ranges),
        .merge      = merge_generic,
        .can_merge  = can_merge_generic,
    },
};

static const AVFilterFormatsMerger mergers_audio[] = {
    {
        .offset     = offsetof(AVFilterFormatsConfig, channel_layouts),
        .merge      = merge_channel_layouts,
        .can_merge  = can_merge_channel_layouts,
    },
    {
        .offset     = offsetof(AVFilterFormatsConfig, samplerates),
        .merge      = merge_samplerates,
        .can_merge  = can_merge_samplerates,
    },
    {
        .offset     = offsetof(AVFilterFormatsConfig, formats),
        .merge      = merge_sample_fmts,
        .can_merge  = can_merge_sample_fmts,
    },
};

static const AVFilterNegotiation negotiate_video = {
    .nb_mergers = FF_ARRAY_ELEMS(mergers_video),
    .mergers = mergers_video,
    .conversion_filter = "scale",
    .conversion_opts_offset = offsetof(AVFilterGraph, scale_sws_opts),
};

static const AVFilterNegotiation negotiate_audio = {
    .nb_mergers = FF_ARRAY_ELEMS(mergers_audio),
    .mergers = mergers_audio,
    .conversion_filter = "aresample",
    .conversion_opts_offset = offsetof(AVFilterGraph, aresample_swr_opts),
};

const AVFilterNegotiation *ff_filter_get_negotiation(AVFilterLink *link)
{
    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO: return &negotiate_video;
    case AVMEDIA_TYPE_AUDIO: return &negotiate_audio;
    default: return NULL;
    }
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

AVFilterChannelLayouts *ff_make_channel_layout_list(const AVChannelLayout *fmts)
{
    AVFilterChannelLayouts *ch_layouts;
    int count = 0;
    if (fmts)
        for (count = 0; fmts[count].nb_channels; count++)
            ;
    ch_layouts = av_mallocz(sizeof(*ch_layouts));
    if (!ch_layouts)
        return NULL;
    ch_layouts->nb_channel_layouts = count;
    if (count) {
        ch_layouts->channel_layouts =
            av_calloc(count, sizeof(*ch_layouts->channel_layouts));
        if (!ch_layouts->channel_layouts) {
            av_freep(&ch_layouts);
            return NULL;
        }
        for (int i = 0; i < count; i++) {
            int ret = av_channel_layout_copy(&ch_layouts->channel_layouts[i], &fmts[i]);
            if (ret < 0)
                goto fail;
        }
    }

    return ch_layouts;

fail:
    for (int i = 0; i < count; i++)
        av_channel_layout_uninit(&ch_layouts->channel_layouts[i]);
    av_free(ch_layouts->channel_layouts);
    av_freep(&ch_layouts);

    return NULL;
}

#define ADD_FORMAT(f, fmt, unref_fn, type, list, nb)        \
do {                                                        \
    type *fmts;                                             \
                                                            \
    if (!(*f) && !(*f = av_mallocz(sizeof(**f)))) {         \
        return AVERROR(ENOMEM);                             \
    }                                                       \
                                                            \
    fmts = av_realloc_array((*f)->list, (*f)->nb + 1,       \
                            sizeof(*(*f)->list));           \
    if (!fmts) {                                            \
        unref_fn(f);                                        \
        return AVERROR(ENOMEM);                             \
    }                                                       \
                                                            \
    (*f)->list = fmts;                                      \
    ASSIGN_FMT(f, fmt, list, nb);                           \
} while (0)

#define ASSIGN_FMT(f, fmt, list, nb)                        \
do {                                                        \
    (*f)->list[(*f)->nb++] = fmt;                           \
} while (0)

int ff_add_format(AVFilterFormats **avff, int64_t fmt)
{
    ADD_FORMAT(avff, fmt, ff_formats_unref, int, formats, nb_formats);
    return 0;
}

#undef ASSIGN_FMT
#define ASSIGN_FMT(f, fmt, list, nb)                              \
do {                                                              \
    int ret;                                                      \
    memset((*f)->list + (*f)->nb, 0, sizeof(*(*f)->list));        \
    ret = av_channel_layout_copy(&(*f)->list[(*f)->nb], fmt);     \
    if (ret < 0)                                                  \
        return ret;                                               \
    (*f)->nb++;                                                   \
} while (0)

int ff_add_channel_layout(AVFilterChannelLayouts **l,
                          const AVChannelLayout *channel_layout)
{
    av_assert1(!(*l && (*l)->all_layouts));
    ADD_FORMAT(l, channel_layout, ff_channel_layouts_unref, AVChannelLayout, channel_layouts, nb_channel_layouts);
    return 0;
}

AVFilterFormats *ff_make_formats_list_singleton(int fmt)
{
    int fmts[2] = { fmt, -1 };
    return ff_make_format_list(fmts);
}

AVFilterFormats *ff_all_formats(enum AVMediaType type)
{
    AVFilterFormats *ret = NULL;

    if (type == AVMEDIA_TYPE_VIDEO) {
        return ff_formats_pixdesc_filter(0, 0);
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

AVFilterFormats *ff_formats_pixdesc_filter(unsigned want, unsigned rej)
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
            return formats;
        }
        formats = av_mallocz(sizeof(*formats));
        if (!formats)
            return NULL;
        formats->nb_formats = nb_formats;
        if (nb_formats) {
            formats->formats = av_malloc_array(nb_formats, sizeof(*formats->formats));
            if (!formats->formats) {
                av_freep(&formats);
                return NULL;
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

AVFilterFormats *ff_all_color_spaces(void)
{
    AVFilterFormats *ret = NULL;
    if (ff_add_format(&ret, AVCOL_SPC_UNSPECIFIED) < 0)
        return NULL;
    for (int csp = 0; csp < AVCOL_SPC_NB; csp++) {
        if (csp == AVCOL_SPC_RESERVED ||
            csp == AVCOL_SPC_UNSPECIFIED)
            continue;
        if (ff_add_format(&ret, csp) < 0)
            return NULL;
    }

    return ret;
}

AVFilterFormats *ff_all_color_ranges(void)
{
    AVFilterFormats *ret = NULL;
    for (int range = 0; range < AVCOL_RANGE_NB; range++) {
        if (ff_add_format(&ret, range) < 0)
            return NULL;
    }

    return ret;
}

#define FORMATS_REF(f, ref, unref_fn)                                           \
    void *tmp;                                                                  \
                                                                                \
    if (!f)                                                                     \
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
    if (!*ref)                                                     \
        return;                                                    \
                                                                   \
    FIND_REF_INDEX(ref, idx);                                      \
                                                                   \
    if (idx >= 0) {                                                \
        memmove((*ref)->refs + idx, (*ref)->refs + idx + 1,        \
            sizeof(*(*ref)->refs) * ((*ref)->refcount - idx - 1)); \
        --(*ref)->refcount;                                        \
    }                                                              \
    if (!(*ref)->refcount) {                                       \
        FREE_LIST(ref, list);                                      \
        av_free((*ref)->list);                                     \
        av_free((*ref)->refs);                                     \
        av_free(*ref);                                             \
    }                                                              \
    *ref = NULL;                                                   \
} while (0)

#define FREE_LIST(ref, list) do { } while(0)
void ff_formats_unref(AVFilterFormats **ref)
{
    FORMATS_UNREF(ref, formats);
}

#undef FREE_LIST
#define FREE_LIST(ref, list)                                       \
    do {                                                           \
        for (int i = 0; i < (*ref)->nb_channel_layouts; i++)       \
            av_channel_layout_uninit(&(*ref)->list[i]);            \
    } while(0)

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

#define SET_COMMON_FORMATS(ctx, fmts, media_type, ref_fn, unref_fn) \
    int i;                                                          \
                                                                    \
    if (!fmts)                                                      \
        return AVERROR(ENOMEM);                                     \
                                                                    \
    for (i = 0; i < ctx->nb_inputs; i++) {                          \
        AVFilterLink *const link = ctx->inputs[i];                  \
        if (link && !link->outcfg.fmts &&                           \
            (media_type == AVMEDIA_TYPE_UNKNOWN || link->type == media_type)) { \
            int ret = ref_fn(fmts, &ctx->inputs[i]->outcfg.fmts);   \
            if (ret < 0) {                                          \
                return ret;                                         \
            }                                                       \
        }                                                           \
    }                                                               \
    for (i = 0; i < ctx->nb_outputs; i++) {                         \
        AVFilterLink *const link = ctx->outputs[i];                 \
        if (link && !link->incfg.fmts &&                            \
            (media_type == AVMEDIA_TYPE_UNKNOWN || link->type == media_type)) { \
            int ret = ref_fn(fmts, &ctx->outputs[i]->incfg.fmts);   \
            if (ret < 0) {                                          \
                return ret;                                         \
            }                                                       \
        }                                                           \
    }                                                               \
                                                                    \
    if (!fmts->refcount)                                            \
        unref_fn(&fmts);                                            \
                                                                    \
    return 0;

int ff_set_common_channel_layouts(AVFilterContext *ctx,
                                  AVFilterChannelLayouts *channel_layouts)
{
    SET_COMMON_FORMATS(ctx, channel_layouts, AVMEDIA_TYPE_AUDIO,
                       ff_channel_layouts_ref, ff_channel_layouts_unref);
}

int ff_set_common_channel_layouts_from_list(AVFilterContext *ctx,
                                            const AVChannelLayout *fmts)
{
    return ff_set_common_channel_layouts(ctx, ff_make_channel_layout_list(fmts));
}

int ff_set_common_all_channel_counts(AVFilterContext *ctx)
{
    return ff_set_common_channel_layouts(ctx, ff_all_channel_counts());
}

int ff_set_common_samplerates(AVFilterContext *ctx,
                              AVFilterFormats *samplerates)
{
    SET_COMMON_FORMATS(ctx, samplerates, AVMEDIA_TYPE_AUDIO,
                       ff_formats_ref, ff_formats_unref);
}

int ff_set_common_samplerates_from_list(AVFilterContext *ctx,
                                        const int *samplerates)
{
    return ff_set_common_samplerates(ctx, ff_make_format_list(samplerates));
}

int ff_set_common_all_samplerates(AVFilterContext *ctx)
{
    return ff_set_common_samplerates(ctx, ff_all_samplerates());
}

int ff_set_common_color_spaces(AVFilterContext *ctx,
                               AVFilterFormats *color_spaces)
{
    SET_COMMON_FORMATS(ctx, color_spaces, AVMEDIA_TYPE_VIDEO,
                       ff_formats_ref, ff_formats_unref);
}

int ff_set_common_color_spaces_from_list(AVFilterContext *ctx,
                                         const int *color_ranges)
{
    return ff_set_common_color_spaces(ctx, ff_make_format_list(color_ranges));
}

int ff_set_common_all_color_spaces(AVFilterContext *ctx)
{
    return ff_set_common_color_spaces(ctx, ff_all_color_spaces());
}

int ff_set_common_color_ranges(AVFilterContext *ctx,
                               AVFilterFormats *color_ranges)
{
    SET_COMMON_FORMATS(ctx, color_ranges, AVMEDIA_TYPE_VIDEO,
                       ff_formats_ref, ff_formats_unref);
}

int ff_set_common_color_ranges_from_list(AVFilterContext *ctx,
                                         const int *color_ranges)
{
    return ff_set_common_color_ranges(ctx, ff_make_format_list(color_ranges));
}

int ff_set_common_all_color_ranges(AVFilterContext *ctx)
{
    return ff_set_common_color_ranges(ctx, ff_all_color_ranges());
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
int ff_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    SET_COMMON_FORMATS(ctx, formats, AVMEDIA_TYPE_UNKNOWN,
                       ff_formats_ref, ff_formats_unref);
}

int ff_set_common_formats_from_list(AVFilterContext *ctx, const int *fmts)
{
    return ff_set_common_formats(ctx, ff_make_format_list(fmts));
}

#define SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, fmts, media_type, \
                            ref_fn, unref_fn)                       \
    if (!fmts)                                                      \
        return AVERROR(ENOMEM);                                     \
                                                                    \
    for (unsigned i = 0; i < ctx->nb_inputs; i++) {                 \
        const AVFilterLink *const link = ctx->inputs[i];            \
        if (!cfg_in[i]->fmts &&                                     \
            (media_type == AVMEDIA_TYPE_UNKNOWN ||                  \
             link->type == media_type)) {                           \
            int ret = ref_fn(fmts, &cfg_in[i]->fmts);               \
            if (ret < 0) {                                          \
                return ret;                                         \
            }                                                       \
        }                                                           \
    }                                                               \
    for (unsigned i = 0; i < ctx->nb_outputs; i++) {                \
        const AVFilterLink *const link = ctx->outputs[i];           \
        if (!cfg_out[i]->fmts &&                                    \
            (media_type == AVMEDIA_TYPE_UNKNOWN ||                  \
             link->type == media_type)) {                           \
            int ret = ref_fn(fmts, &cfg_out[i]->fmts);              \
            if (ret < 0) {                                          \
                return ret;                                         \
            }                                                       \
        }                                                           \
    }                                                               \
                                                                    \
    if (!fmts->refcount)                                            \
        unref_fn(&fmts);                                            \
                                                                    \
    return 0;

int ff_set_common_channel_layouts2(const AVFilterContext *ctx,
                                   AVFilterFormatsConfig **cfg_in,
                                   AVFilterFormatsConfig **cfg_out,
                                   AVFilterChannelLayouts *channel_layouts)
{
    SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, channel_layouts, AVMEDIA_TYPE_AUDIO,
                        ff_channel_layouts_ref, ff_channel_layouts_unref);
}

int ff_set_common_channel_layouts_from_list2(const AVFilterContext *ctx,
                                             AVFilterFormatsConfig **cfg_in,
                                             AVFilterFormatsConfig **cfg_out,
                                             const AVChannelLayout *fmts)
{
    return ff_set_common_channel_layouts2(ctx, cfg_in, cfg_out, ff_make_channel_layout_list(fmts));
}

int ff_set_common_all_channel_counts2(const AVFilterContext *ctx,
                                      AVFilterFormatsConfig **cfg_in,
                                      AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_channel_layouts2(ctx, cfg_in, cfg_out, ff_all_channel_counts());
}

int ff_set_common_samplerates2(const AVFilterContext *ctx,
                               AVFilterFormatsConfig **cfg_in,
                               AVFilterFormatsConfig **cfg_out,
                               AVFilterFormats *samplerates)
{
    SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, samplerates, AVMEDIA_TYPE_AUDIO,
                        ff_formats_ref, ff_formats_unref);
}

int ff_set_common_samplerates_from_list2(const AVFilterContext *ctx,
                                         AVFilterFormatsConfig **cfg_in,
                                         AVFilterFormatsConfig **cfg_out,
                                         const int *samplerates)
{
    return ff_set_common_samplerates2(ctx, cfg_in, cfg_out, ff_make_format_list(samplerates));
}

int ff_set_common_all_samplerates2(const AVFilterContext *ctx,
                                   AVFilterFormatsConfig **cfg_in,
                                   AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_samplerates2(ctx, cfg_in, cfg_out, ff_all_samplerates());
}

int ff_set_common_color_spaces2(const AVFilterContext *ctx,
                                AVFilterFormatsConfig **cfg_in,
                                AVFilterFormatsConfig **cfg_out,
                                AVFilterFormats *color_spaces)
{
    SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, color_spaces, AVMEDIA_TYPE_VIDEO,
                        ff_formats_ref, ff_formats_unref);
}

int ff_set_common_color_spaces_from_list2(const AVFilterContext *ctx,
                                          AVFilterFormatsConfig **cfg_in,
                                          AVFilterFormatsConfig **cfg_out,
                                          const int *color_ranges)
{
    return ff_set_common_color_spaces2(ctx, cfg_in, cfg_out, ff_make_format_list(color_ranges));
}

int ff_set_common_all_color_spaces2(const AVFilterContext *ctx,
                                    AVFilterFormatsConfig **cfg_in,
                                    AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_color_spaces2(ctx, cfg_in, cfg_out, ff_all_color_spaces());
}

int ff_set_common_color_ranges2(const AVFilterContext *ctx,
                                AVFilterFormatsConfig **cfg_in,
                                AVFilterFormatsConfig **cfg_out,
                                AVFilterFormats *color_ranges)
{
    SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, color_ranges, AVMEDIA_TYPE_VIDEO,
                        ff_formats_ref, ff_formats_unref);
}

int ff_set_common_color_ranges_from_list2(const AVFilterContext *ctx,
                                          AVFilterFormatsConfig **cfg_in,
                                          AVFilterFormatsConfig **cfg_out,
                                          const int *color_ranges)
{
    return ff_set_common_color_ranges2(ctx, cfg_in, cfg_out, ff_make_format_list(color_ranges));
}

int ff_set_common_all_color_ranges2(const AVFilterContext *ctx,
                                    AVFilterFormatsConfig **cfg_in,
                                    AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_color_ranges2(ctx, cfg_in, cfg_out, ff_all_color_ranges());
}

int ff_set_common_formats2(const AVFilterContext *ctx,
                           AVFilterFormatsConfig **cfg_in,
                           AVFilterFormatsConfig **cfg_out,
                           AVFilterFormats *formats)
{
    SET_COMMON_FORMATS2(ctx, cfg_in, cfg_out, formats, AVMEDIA_TYPE_UNKNOWN,
                        ff_formats_ref, ff_formats_unref);
}

int ff_set_common_formats_from_list2(const AVFilterContext *ctx,
                                     AVFilterFormatsConfig **cfg_in,
                                     AVFilterFormatsConfig **cfg_out,
                                     const int *fmts)
{
    return ff_set_common_formats2(ctx, cfg_in, cfg_out, ff_make_format_list(fmts));
}


int ff_default_query_formats(AVFilterContext *ctx)
{
    const FFFilter *const f = fffilter(ctx->filter);
    AVFilterFormats *formats;
    enum AVMediaType type;
    int ret;

    switch (f->formats_state) {
    case FF_FILTER_FORMATS_PIXFMT_LIST:
        type    = AVMEDIA_TYPE_VIDEO;
        formats = ff_make_format_list(f->formats.pixels_list);
        break;
    case FF_FILTER_FORMATS_SAMPLEFMTS_LIST:
        type    = AVMEDIA_TYPE_AUDIO;
        formats = ff_make_format_list(f->formats.samples_list);
        break;
    case FF_FILTER_FORMATS_SINGLE_PIXFMT:
        type    = AVMEDIA_TYPE_VIDEO;
        formats = ff_make_formats_list_singleton(f->formats.pix_fmt);
        break;
    case FF_FILTER_FORMATS_SINGLE_SAMPLEFMT:
        type    = AVMEDIA_TYPE_AUDIO;
        formats = ff_make_formats_list_singleton(f->formats.sample_fmt);
        break;
    default:
        av_assert2(!"Unreachable");
    /* Intended fallthrough */
    case FF_FILTER_FORMATS_PASSTHROUGH:
    case FF_FILTER_FORMATS_QUERY_FUNC:
    case FF_FILTER_FORMATS_QUERY_FUNC2:
        type = AVMEDIA_TYPE_UNKNOWN;
        formats = ff_all_formats(ctx->nb_inputs  ? ctx->inputs [0]->type :
                                 ctx->nb_outputs ? ctx->outputs[0]->type :
                                 AVMEDIA_TYPE_VIDEO);
        break;
    }

    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;
    if (type != AVMEDIA_TYPE_AUDIO) {
        ret = ff_set_common_all_color_spaces(ctx);
        if (ret < 0)
            return ret;
        ret = ff_set_common_all_color_ranges(ctx);
        if (ret < 0)
            return ret;
    }
    if (type != AVMEDIA_TYPE_VIDEO) {
        ret = ff_set_common_all_channel_counts(ctx);
        if (ret < 0)
            return ret;
        ret = ff_set_common_all_samplerates(ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int check_list(void *log, const char *name, const AVFilterFormats *fmts)
{
    unsigned i, j;

    if (!fmts)
        return 0;
    if (!fmts->nb_formats) {
        av_log(log, AV_LOG_ERROR, "Empty %s list\n", name);
        return AVERROR(EINVAL);
    }
    for (i = 0; i < fmts->nb_formats; i++) {
        for (j = i + 1; j < fmts->nb_formats; j++) {
            if (fmts->formats[i] == fmts->formats[j]) {
                av_log(log, AV_LOG_ERROR, "Duplicated %s\n", name);
                return AVERROR(EINVAL);
            }
        }
    }
    return 0;
}

int ff_formats_check_pixel_formats(void *log, const AVFilterFormats *fmts)
{
    return check_list(log, "pixel format", fmts);
}

int ff_formats_check_sample_formats(void *log, const AVFilterFormats *fmts)
{
    return check_list(log, "sample format", fmts);
}

int ff_formats_check_sample_rates(void *log, const AVFilterFormats *fmts)
{
    if (!fmts || !fmts->nb_formats)
        return 0;
    return check_list(log, "sample rate", fmts);
}

int ff_formats_check_color_spaces(void *log, const AVFilterFormats *fmts)
{
    for (int i = 0; fmts && i < fmts->nb_formats; i++) {
        if (fmts->formats[i] == AVCOL_SPC_RESERVED) {
            av_log(log, AV_LOG_ERROR, "Invalid color space\n");
            return AVERROR(EINVAL);
        }
    }
    return check_list(log, "color space", fmts);
}

int ff_formats_check_color_ranges(void *log, const AVFilterFormats *fmts)
{
    return check_list(log, "color range", fmts);
}

static int layouts_compatible(const AVChannelLayout *a, const AVChannelLayout *b)
{
    return !av_channel_layout_compare(a, b) ||
           (KNOWN(a) && !KNOWN(b) && a->nb_channels == b->nb_channels) ||
           (KNOWN(b) && !KNOWN(a) && b->nb_channels == a->nb_channels);
}

int ff_formats_check_channel_layouts(void *log, const AVFilterChannelLayouts *fmts)
{
    unsigned i, j;

    if (!fmts)
        return 0;
    if (fmts->all_layouts < fmts->all_counts) {
        av_log(log, AV_LOG_ERROR, "Inconsistent generic list\n");
        return AVERROR(EINVAL);
    }
    if (!fmts->all_layouts && !fmts->nb_channel_layouts) {
        av_log(log, AV_LOG_ERROR, "Empty channel layout list\n");
        return AVERROR(EINVAL);
    }
    for (i = 0; i < fmts->nb_channel_layouts; i++) {
        for (j = i + 1; j < fmts->nb_channel_layouts; j++) {
            if (layouts_compatible(&fmts->channel_layouts[i], &fmts->channel_layouts[j])) {
                av_log(log, AV_LOG_ERROR, "Duplicated or redundant channel layout\n");
                return AVERROR(EINVAL);
            }
        }
    }
    return 0;
}
