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

#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/audioconvert.h"
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
                if (a->fmts[i] == b->fmts[j]) {                                 \
                    if(k >= FFMIN(a->nb, b->nb)){                               \
                        av_log(0, AV_LOG_ERROR, "Duplicate formats in avfilter_merge_formats() detected\n"); \
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

AVFilterFormats *avfilter_merge_formats(AVFilterFormats *a, AVFilterFormats *b)
{
    AVFilterFormats *ret = NULL;

    if (a == b)
        return a;

    MERGE_FORMATS(ret, a, b, formats, format_count, AVFilterFormats, fail);

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

    if (a->format_count && b->format_count) {
        MERGE_FORMATS(ret, a, b, formats, format_count, AVFilterFormats, fail);
    } else if (a->format_count) {
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

    for (p = fmts; *p != -1; p++) {
        if (fmt == *p)
            return 1;
    }
    return 0;
}

#define COPY_INT_LIST(list_copy, list, type) {                          \
    int count = 0;                                                      \
    if (list)                                                           \
        for (count = 0; list[count] != -1; count++)                     \
            ;                                                           \
    list_copy = av_calloc(count+1, sizeof(type));                       \
    if (list_copy) {                                                    \
        memcpy(list_copy, list, sizeof(type) * count);                  \
        list_copy[count] = -1;                                          \
    }                                                                   \
}

int *ff_copy_int_list(const int * const list)
{
    int *ret = NULL;
    COPY_INT_LIST(ret, list, int);
    return ret;
}

int64_t *ff_copy_int64_list(const int64_t * const list)
{
    int64_t *ret = NULL;
    COPY_INT_LIST(ret, list, int64_t);
    return ret;
}

#define MAKE_FORMAT_LIST(type, field, count_field)                      \
    type *formats;                                                      \
    int count = 0;                                                      \
    if (fmts)                                                           \
        for (count = 0; fmts[count] != -1; count++)                     \
            ;                                                           \
    formats = av_mallocz(sizeof(*formats));                             \
    if (!formats) return NULL;                                          \
    formats->count_field = count;                                       \
    if (count) {                                                        \
        formats->field = av_malloc(sizeof(*formats->field)*count);      \
        if (!formats->field) {                                          \
            av_free(formats);                                           \
            return NULL;                                                \
        }                                                               \
    }

AVFilterFormats *avfilter_make_format_list(const int *fmts)
{
    MAKE_FORMAT_LIST(AVFilterFormats, formats, format_count);
    while (count--)
        formats->formats[count] = fmts[count];

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

#define ADD_FORMAT(f, fmt, type, list, nb)                  \
do {                                                        \
    type *fmts;                                             \
                                                            \
    if (!(*f) && !(*f = av_mallocz(sizeof(**f))))           \
        return AVERROR(ENOMEM);                             \
                                                            \
    fmts = av_realloc((*f)->list,                           \
                      sizeof(*(*f)->list) * ((*f)->nb + 1));\
    if (!fmts)                                              \
        return AVERROR(ENOMEM);                             \
                                                            \
    (*f)->list = fmts;                                      \
    (*f)->list[(*f)->nb++] = fmt;                           \
    return 0;                                               \
} while (0)

int avfilter_add_format(AVFilterFormats **avff, int64_t fmt)
{
    ADD_FORMAT(avff, fmt, int, formats, format_count);
}

int ff_add_channel_layout(AVFilterChannelLayouts **l, uint64_t channel_layout)
{
    ADD_FORMAT(l, channel_layout, uint64_t, channel_layouts, nb_channel_layouts);
}

#if FF_API_OLD_ALL_FORMATS_API
AVFilterFormats *avfilter_all_formats(enum AVMediaType type)
{
    return avfilter_make_all_formats(type);
}
#endif

AVFilterFormats *avfilter_make_all_formats(enum AVMediaType type)
{
    AVFilterFormats *ret = NULL;
    int fmt;
    int num_formats = type == AVMEDIA_TYPE_VIDEO ? PIX_FMT_NB    :
                      type == AVMEDIA_TYPE_AUDIO ? AV_SAMPLE_FMT_NB : 0;

    for (fmt = 0; fmt < num_formats; fmt++)
        if ((type != AVMEDIA_TYPE_VIDEO) ||
            (type == AVMEDIA_TYPE_VIDEO && !(av_pix_fmt_descriptors[fmt].flags & PIX_FMT_HWACCEL)))
            avfilter_add_format(&ret, fmt);

    return ret;
}

const int64_t avfilter_all_channel_layouts[] = {
#include "all_channel_layouts.inc"
    -1
};

// AVFilterFormats *avfilter_make_all_channel_layouts(void)
// {
//     return avfilter_make_format64_list(avfilter_all_channel_layouts);
// }

#if FF_API_PACKING
AVFilterFormats *avfilter_make_all_packing_formats(void)
{
    static const int packing[] = {
        AVFILTER_PACKED,
        AVFILTER_PLANAR,
        -1,
    };

    return avfilter_make_format_list(packing);
}
#endif

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
    f->refs[f->refcount-1] = ref;                                    \
} while (0)

void ff_channel_layouts_ref(AVFilterChannelLayouts *f, AVFilterChannelLayouts **ref)
{
    FORMATS_REF(f, ref);
}

void avfilter_formats_ref(AVFilterFormats *f, AVFilterFormats **ref)
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

void avfilter_formats_unref(AVFilterFormats **ref)
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

void avfilter_formats_changeref(AVFilterFormats **oldref,
                                AVFilterFormats **newref)
{
    FORMATS_CHANGEREF(oldref, newref);
}

#define SET_COMMON_FORMATS(ctx, fmts, in_fmts, out_fmts, ref, list) \
{                                                                   \
    int count = 0, i;                                               \
                                                                    \
    for (i = 0; i < ctx->input_count; i++) {                        \
        if (ctx->inputs[i]) {                                       \
            ref(fmts, &ctx->inputs[i]->out_fmts);                   \
            count++;                                                \
        }                                                           \
    }                                                               \
    for (i = 0; i < ctx->output_count; i++) {                       \
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
                       avfilter_formats_ref, formats);
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
void avfilter_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    SET_COMMON_FORMATS(ctx, formats, in_formats, out_formats,
                       avfilter_formats_ref, formats);
}

int ff_default_query_formats(AVFilterContext *ctx)
{
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    avfilter_set_common_formats(ctx, avfilter_all_formats(type));
    if (type == AVMEDIA_TYPE_AUDIO) {
        ff_set_common_channel_layouts(ctx, ff_all_channel_layouts());
        ff_set_common_samplerates(ctx, ff_all_samplerates());
    }

    return 0;
}

/* internal functions for parsing audio format arguments */

int ff_parse_pixel_format(enum PixelFormat *ret, const char *arg, void *log_ctx)
{
    char *tail;
    int pix_fmt = av_get_pix_fmt(arg);
    if (pix_fmt == PIX_FMT_NONE) {
        pix_fmt = strtol(arg, &tail, 0);
        if (*tail || (unsigned)pix_fmt >= PIX_FMT_NB) {
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
        if (*tail || (unsigned)sfmt >= AV_SAMPLE_FMT_NB) {
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

int ff_parse_channel_layout(int64_t *ret, const char *arg, void *log_ctx)
{
    char *tail;
    int64_t chlayout = av_get_channel_layout(arg);
    if (chlayout == 0) {
        chlayout = strtol(arg, &tail, 10);
        if (*tail || chlayout == 0) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid channel layout '%s'\n", arg);
            return AVERROR(EINVAL);
        }
    }
    *ret = chlayout;
    return 0;
}

#if FF_API_FILTERS_PUBLIC
int avfilter_default_query_formats(AVFilterContext *ctx)
{
    return ff_default_query_formats(ctx);
}
#endif
#ifdef TEST

#undef printf

int main(void)
{
    const int64_t *cl;
    char buf[512];

    for (cl = avfilter_all_channel_layouts; *cl != -1; cl++) {
        av_get_channel_layout_string(buf, sizeof(buf), -1, *cl);
        printf("%s\n", buf);
    }

    return 0;
}

#endif
