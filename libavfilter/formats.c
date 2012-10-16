/*
 * Filter layer - format negotiation
 * copyright (c) 2007 Bobby Bingham
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

#include "libavutil/pixdesc.h"
#include "avfilter.h"

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
    unsigned i, j, k = 0;

    if (a == b)
        return a;

    ret = av_mallocz(sizeof(AVFilterFormats));

    /* merge list of formats */
    ret->formats = av_malloc(sizeof(*ret->formats) * FFMIN(a->format_count,
                                                           b->format_count));
    for(i = 0; i < a->format_count; i ++)
        for(j = 0; j < b->format_count; j ++)
            if(a->formats[i] == b->formats[j])
                ret->formats[k++] = a->formats[i];

    ret->format_count = k;
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

AVFilterFormats *avfilter_make_format_list(const enum PixelFormat *pix_fmts)
{
    AVFilterFormats *formats;
    int count;

    for (count = 0; pix_fmts[count] != PIX_FMT_NONE; count++)
        ;

    formats               = av_mallocz(sizeof(AVFilterFormats));
    formats->formats      = av_malloc(sizeof(*formats->formats) * count);
    formats->format_count = count;
    memcpy(formats->formats, pix_fmts, sizeof(*formats->formats) * count);

    return formats;
}

int avfilter_add_colorspace(AVFilterFormats **avff, enum PixelFormat pix_fmt)
{
    enum PixelFormat *pix_fmts;

    if (!(*avff) && !(*avff = av_mallocz(sizeof(AVFilterFormats))))
        return AVERROR(ENOMEM);

    pix_fmts = av_realloc((*avff)->formats,
                          sizeof((*avff)->formats) * ((*avff)->format_count+1));
    if (!pix_fmts)
        return AVERROR(ENOMEM);

    (*avff)->formats = pix_fmts;
    (*avff)->formats[(*avff)->format_count++] = pix_fmt;
    return 0;
}

AVFilterFormats *avfilter_all_colorspaces(void)
{
    AVFilterFormats *ret = NULL;
    enum PixelFormat pix_fmt;

    for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
        if (!(av_pix_fmt_descriptors[pix_fmt].flags & PIX_FMT_HWACCEL))
            avfilter_add_colorspace(&ret, pix_fmt);

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

