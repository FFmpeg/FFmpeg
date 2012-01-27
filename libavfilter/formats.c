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

#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

/**
 * Add all refs from a to ret and destroy a.
 */
static void merge_ref(AVFilterFormats *ret, AVFilterFormats *a)
{
    int i;

    for(i = 0; i < a->refcount; i ++) {
        ret->refs[ret->refcount] = a->refs[i];
        *ret->refs[ret->refcount++] = ret;
    }

    av_free(a->refs);
    av_free(a->formats);
    av_free(a);
}

AVFilterFormats *avfilter_merge_formats(AVFilterFormats *a, AVFilterFormats *b)
{
    AVFilterFormats *ret;
    unsigned i, j, k = 0, m_count;

    ret = av_mallocz(sizeof(AVFilterFormats));

    /* merge list of formats */
    m_count = FFMIN(a->format_count, b->format_count);
    if (m_count) {
        ret->formats = av_malloc(sizeof(*ret->formats) * m_count);
        for(i = 0; i < a->format_count; i ++)
            for(j = 0; j < b->format_count; j ++)
                if(a->formats[i] == b->formats[j])
                    ret->formats[k++] = a->formats[i];

        ret->format_count = k;
    }
    /* check that there was at least one common format */
    if(!ret->format_count) {
        av_free(ret->formats);
        av_free(ret);
        return NULL;
    }

    ret->refs = av_malloc(sizeof(AVFilterFormats**)*(a->refcount+b->refcount));

    merge_ref(ret, a);
    merge_ref(ret, b);

    return ret;
}

int ff_fmt_is_in(int fmt, const int *fmts)
{
    const int *p;

    for (p = fmts; *p != PIX_FMT_NONE; p++) {
        if (fmt == *p)
            return 1;
    }
    return 0;
}

AVFilterFormats *avfilter_make_format_list(const int *fmts)
{
    AVFilterFormats *formats;
    int count;

    for (count = 0; fmts[count] != -1; count++)
        ;

    formats               = av_mallocz(sizeof(AVFilterFormats));
    if (count)
        formats->formats  = av_malloc(sizeof(*formats->formats) * count);
    formats->format_count = count;
    memcpy(formats->formats, fmts, sizeof(*formats->formats) * count);

    return formats;
}

int avfilter_add_format(AVFilterFormats **avff, int fmt)
{
    int *fmts;

    if (!(*avff) && !(*avff = av_mallocz(sizeof(AVFilterFormats))))
        return AVERROR(ENOMEM);

    fmts = av_realloc((*avff)->formats,
                      sizeof(*(*avff)->formats) * ((*avff)->format_count+1));
    if (!fmts)
        return AVERROR(ENOMEM);

    (*avff)->formats = fmts;
    (*avff)->formats[(*avff)->format_count++] = fmt;
    return 0;
}

AVFilterFormats *avfilter_all_formats(enum AVMediaType type)
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

void avfilter_formats_ref(AVFilterFormats *f, AVFilterFormats **ref)
{
    *ref = f;
    f->refs = av_realloc(f->refs, sizeof(AVFilterFormats**) * ++f->refcount);
    f->refs[f->refcount-1] = ref;
}

static int find_ref_index(AVFilterFormats **ref)
{
    int i;
    for(i = 0; i < (*ref)->refcount; i ++)
        if((*ref)->refs[i] == ref)
            return i;
    return -1;
}

void avfilter_formats_unref(AVFilterFormats **ref)
{
    int idx;

    if (!*ref)
        return;

    idx = find_ref_index(ref);

    if(idx >= 0)
        memmove((*ref)->refs + idx, (*ref)->refs + idx+1,
            sizeof(AVFilterFormats**) * ((*ref)->refcount-idx-1));

    if(!--(*ref)->refcount) {
        av_free((*ref)->formats);
        av_free((*ref)->refs);
        av_free(*ref);
    }
    *ref = NULL;
}

void avfilter_formats_changeref(AVFilterFormats **oldref,
                                AVFilterFormats **newref)
{
    int idx = find_ref_index(oldref);

    if(idx >= 0) {
        (*oldref)->refs[idx] = newref;
        *newref = *oldref;
        *oldref = NULL;
    }
}
