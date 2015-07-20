/*
 * Filter layer - format negotiation
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"

/**
 * Add all refs from a to ret and destroy a.
 */
#define MERGE_REF(ret, a, fmts, type, fail)                                \
do {                                                                       \
    type ***tmp;                                                           \
    int i;                                                                 \
                                                                           \
    if (!(tmp = av_realloc(ret->refs,                                      \
                           sizeof(*tmp) * (ret->refcount + a->refcount)))) \
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
        if (!(ret->fmts = av_malloc(sizeof(*ret->fmts) * count)))               \
            goto fail;                                                          \
        for (i = 0; i < a->nb; i++)                                             \
            for (j = 0; j < b->nb; j++)                                         \
                if (a->fmts[i] == b->fmts[j])                                   \
                    ret->fmts[k++] = a->fmts[i];                                \
                                                                                \
        ret->nb = k;                                                            \
    }                                                                           \
    /* check that there was at least one common format */                       \
    if (!ret->nb)                                                               \
        goto fail;                                                              \
                                                                                \
    MERGE_REF(ret, a, fmts, type, fail);                                        \
    MERGE_REF(ret, b, fmts, type, fail);                                        \
} while (0)

AVFilterFormats *ff_merge_formats(AVFilterFormats *a, AVFilterFormats *b)
{
    AVFilterFormats *ret = NULL;

    if (a == b)
        return a;

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

    if (a == b) return a;

    if (a->nb_channel_layouts && b->nb_channel_layouts) {
        MERGE_FORMATS(ret, a, b, channel_layouts, nb_channel_layouts,
                      AVFilterChannelLayouts, fail);
    } else if (a->nb_channel_layouts) {
        MERGE_REF(a, b, channel_layouts, AVFilterChannelLayouts, fail);
        ret = a;
    } else {
        MERGE_REF(b, a, channel_layouts, AVFilterChannelLayouts, fail);
        ret = b;
    }

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

    for (p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (fmt == *p)
            return 1;
    }
    return 0;
}

AVFilterFormats *ff_make_format_list(const int *fmts)
{
    AVFilterFormats *formats;
    int count;

    for (count = 0; fmts[count] != -1; count++)
        ;

    formats               = av_mallocz(sizeof(*formats));
    if (!formats)
        return NULL;
    if (count) {
        formats->formats  = av_malloc(sizeof(*formats->formats) * count);
        if (!formats->formats) {
            av_freep(&formats);
            return NULL;
        }
    }
    formats->nb_formats = count;
    memcpy(formats->formats, fmts, sizeof(*formats->formats) * count);

    return formats;
}

#define ADD_FORMAT(f, fmt, type, list, nb)                  \
do {                                                        \
    type *fmts;                                             \
                                                            \
    if (!(*f) && !(*f = av_mallocz(sizeof(**f))))           \
        return AVERROR(ENOMEM);                             \
                                                            \
    fmts = av_realloc((*f)->list,                           \
                      sizeof(*(*f)->list) * ((*f)->nb + 1));\
    if (!fmts) {                                            \
        av_freep(&f);                                       \
        return AVERROR(ENOMEM);                             \
    }                                                       \
                                                            \
    (*f)->list = fmts;                                      \
    (*f)->list[(*f)->nb++] = fmt;                           \
    return 0;                                               \
} while (0)

int ff_add_format(AVFilterFormats **avff, int fmt)
{
    ADD_FORMAT(avff, fmt, int, formats, nb_formats);
}

int ff_add_channel_layout(AVFilterChannelLayouts **l, uint64_t channel_layout)
{
    ADD_FORMAT(l, channel_layout, uint64_t, channel_layouts, nb_channel_layouts);
}

AVFilterFormats *ff_all_formats(enum AVMediaType type)
{
    AVFilterFormats *ret = NULL;

    if (type == AVMEDIA_TYPE_VIDEO) {
        const AVPixFmtDescriptor *desc = NULL;
        while ((desc = av_pix_fmt_desc_next(desc))) {
            ff_add_format(&ret, av_pix_fmt_desc_get_id(desc));
        }
    } else if (type == AVMEDIA_TYPE_AUDIO) {
        enum AVSampleFormat fmt = 0;
        while (av_get_sample_fmt_name(fmt)) {
            ff_add_format(&ret, fmt);
            fmt++;
        }
    }

    return ret;
}

AVFilterFormats *ff_planar_sample_fmts(void)
{
    AVFilterFormats *ret = NULL;
    int fmt;

    for (fmt = 0; fmt < AV_SAMPLE_FMT_NB; fmt++)
        if (av_sample_fmt_is_planar(fmt))
            ff_add_format(&ret, fmt);

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
    return ret;
}

#define FORMATS_REF(f, ref)                                          \
do {                                                                 \
    *ref = f;                                                        \
    f->refs = av_realloc(f->refs, sizeof(*f->refs) * ++f->refcount); \
    if (!f->refs)                                                    \
        return;                                                      \
    f->refs[f->refcount-1] = ref;                                    \
} while (0)

void ff_channel_layouts_ref(AVFilterChannelLayouts *f, AVFilterChannelLayouts **ref)
{
    FORMATS_REF(f, ref);
}

void ff_formats_ref(AVFilterFormats *f, AVFilterFormats **ref)
{
    FORMATS_REF(f, ref);
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

#define SET_COMMON_FORMATS(ctx, fmts, in_fmts, out_fmts, ref, list) \
{                                                                   \
    int count = 0, i;                                               \
                                                                    \
    for (i = 0; i < ctx->nb_inputs; i++) {                          \
        if (ctx->inputs[i]) {                                       \
            ref(fmts, &ctx->inputs[i]->out_fmts);                   \
            count++;                                                \
        }                                                           \
    }                                                               \
    for (i = 0; i < ctx->nb_outputs; i++) {                         \
        if (ctx->outputs[i]) {                                      \
            ref(fmts, &ctx->outputs[i]->in_fmts);                   \
            count++;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    if (!count) {                                                   \
        av_freep(&fmts->list);                                      \
        av_freep(&fmts->refs);                                      \
        av_freep(&fmts);                                            \
    }                                                               \
}

void ff_set_common_channel_layouts(AVFilterContext *ctx,
                                   AVFilterChannelLayouts *layouts)
{
    SET_COMMON_FORMATS(ctx, layouts, in_channel_layouts, out_channel_layouts,
                       ff_channel_layouts_ref, channel_layouts);
}

void ff_set_common_samplerates(AVFilterContext *ctx,
                               AVFilterFormats *samplerates)
{
    SET_COMMON_FORMATS(ctx, samplerates, in_samplerates, out_samplerates,
                       ff_formats_ref, formats);
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
void ff_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    SET_COMMON_FORMATS(ctx, formats, in_formats, out_formats,
                       ff_formats_ref, formats);
}

int ff_default_query_formats(AVFilterContext *ctx)
{
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    ff_set_common_formats(ctx, ff_all_formats(type));
    if (type == AVMEDIA_TYPE_AUDIO) {
        ff_set_common_channel_layouts(ctx, ff_all_channel_layouts());
        ff_set_common_samplerates(ctx, ff_all_samplerates());
    }

    return 0;
}
