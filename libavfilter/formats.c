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

#include "avfilter.h"

AVFilterFormats *avfilter_merge_formats(AVFilterFormats *a, AVFilterFormats *b)
{
    AVFilterFormats *ret;
    unsigned i, j, k = 0;

    ret = av_mallocz(sizeof(AVFilterFormats));

    /* merge list of formats */
    ret->formats = av_malloc(sizeof(*ret->formats) * FFMIN(a->format_count,
                                                           b->format_count));
    for(i = 0; i < a->format_count; i ++)
        for(j = 0; j < b->format_count; j ++)
            if(a->formats[i] == b->formats[j])
                ret->formats[k++] = a->formats[i];

    /* check that there was at least one common format */
    if(!(ret->format_count = k)) {
        av_free(ret->formats);
        av_free(ret);
        return NULL;
    }

    /* merge and update all the references */
    ret->refs = av_malloc(sizeof(AVFilterFormats**)*(a->refcount+b->refcount));
    for(i = 0; i < a->refcount; i ++) {
        ret->refs[ret->refcount] = a->refs[i];
        *ret->refs[ret->refcount++] = ret;
    }
    for(i = 0; i < b->refcount; i ++) {
        ret->refs[ret->refcount] = b->refs[i];
        *ret->refs[ret->refcount++] = ret;
    }

    av_free(a->refs);
    av_free(a->formats);
    av_free(a);

    av_free(b->refs);
    av_free(b->formats);
    av_free(b);

    return ret;
}

AVFilterFormats *avfilter_make_format_list(int len, ...)
{
    AVFilterFormats *ret;
    int i;
    va_list vl;

    ret = av_mallocz(sizeof(AVFilterFormats));
    ret->formats = av_malloc(sizeof(*ret->formats) * len);
    ret->format_count = len;

    va_start(vl, len);
    for(i = 0; i < len; i ++)
        ret->formats[i] = va_arg(vl, int);
    va_end(vl);

    return ret;
}

AVFilterFormats *avfilter_all_colorspaces(void)
{
    return avfilter_make_format_list(35,
                PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
                PIX_FMT_YUV411P,  PIX_FMT_YUV410P,
                PIX_FMT_YUYV422,  PIX_FMT_UYVY422,  PIX_FMT_UYYVYY411,
                PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,
                PIX_FMT_YUV440P,  PIX_FMT_YUVJ440P,
                PIX_FMT_RGB32,    PIX_FMT_BGR32,
                PIX_FMT_RGB32_1,  PIX_FMT_BGR32_1,
                PIX_FMT_RGB24,    PIX_FMT_BGR24,
                PIX_FMT_RGB565,   PIX_FMT_BGR565,
                PIX_FMT_RGB555,   PIX_FMT_BGR555,
                PIX_FMT_RGB8,     PIX_FMT_BGR8,
                PIX_FMT_RGB4_BYTE,PIX_FMT_BGR4_BYTE,
                PIX_FMT_GRAY16BE, PIX_FMT_GRAY16LE,
                PIX_FMT_GRAY8,    PIX_FMT_PAL8,
                PIX_FMT_MONOWHITE,PIX_FMT_MONOBLACK,
                PIX_FMT_NV12,     PIX_FMT_NV21);
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

    if((idx = find_ref_index(ref)) >= 0)
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
    int idx;

    if((idx = find_ref_index(oldref)) >= 0) {
        (*oldref)->refs[idx] = newref;
        *newref = *oldref;
        *oldref = NULL;
    }
}

