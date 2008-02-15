/*
 * Filter layer
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "avfilter.h"
#include "allfilters.h"

/** list of registered filters, sorted by name */
static int filter_count = 0;
static AVFilter **filters = NULL;

AVFilterPicRef *avfilter_ref_pic(AVFilterPicRef *ref, int pmask)
{
    AVFilterPicRef *ret = av_malloc(sizeof(AVFilterPicRef));
    memcpy(ret, ref, sizeof(AVFilterPicRef));
    ret->perms &= pmask;
    ret->pic->refcount ++;
    return ret;
}

void avfilter_unref_pic(AVFilterPicRef *ref)
{
    if(-- ref->pic->refcount == 0)
        ref->pic->free(ref->pic);
    av_free(ref);
}

void avfilter_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                         AVFilterPad **pads, AVFilterLink ***links,
                         AVFilterPad *newpad)
{
    unsigned i;

    idx = FFMIN(idx, *count);

    *pads  = av_realloc(*pads,  sizeof(AVFilterPad)   * (*count + 1));
    *links = av_realloc(*links, sizeof(AVFilterLink*) * (*count + 1));
    memmove(*pads +idx+1, *pads +idx, sizeof(AVFilterPad)   * (*count-idx));
    memmove(*links+idx+1, *links+idx, sizeof(AVFilterLink*) * (*count-idx));
    memcpy(*pads+idx, newpad, sizeof(AVFilterPad));
    (*links)[idx] = NULL;

    (*count) ++;
    for(i = idx+1; i < *count; i ++)
        if(*links[i])
            (*(unsigned *)((uint8_t *)(*links[i]) + padidx_off)) ++;
}

int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad)
{
    AVFilterLink *link;

    if(src->output_count <= srcpad || dst->input_count <= dstpad ||
       src->outputs[srcpad]        || dst->inputs[dstpad])
        return -1;

    src->outputs[srcpad] =
    dst->inputs[dstpad]  = link = av_malloc(sizeof(AVFilterLink));

    link->src     = src;
    link->dst     = dst;
    link->srcpad  = srcpad;
    link->dstpad  = dstpad;
    link->cur_pic = NULL;
    link->format  = -1;

    return 0;
}

int avfilter_config_link(AVFilterLink *link)
{
    int *fmts[2], i, j;
    int (*config_link)(AVFilterLink *);

    if(!link)
        return 0;

    /* find a format both filters support - TODO: auto-insert conversion filter */
    link->format = -1;
    if(link->src->output_pads[link->srcpad].query_formats)
        fmts[0] = link->src->output_pads[link->srcpad].query_formats(link);
    else
        fmts[0] = avfilter_default_query_output_formats(link);
    fmts[1] = link->dst->input_pads[link->dstpad].query_formats(link);
    for(i = 0; fmts[0][i] != -1; i ++)
        for(j = 0; fmts[1][j] != -1; j ++)
            if(fmts[0][i] == fmts[1][j]) {
                link->format = fmts[0][i];
                goto format_done;
            }

format_done:
    av_free(fmts[0]);
    av_free(fmts[1]);
    if(link->format == -1)
        return -1;

    if(!(config_link = link->src->output_pads[link->srcpad].config_props))
        config_link  = avfilter_default_config_output_link;
    if(config_link(link))
            return -1;

    if(!(config_link = link->dst->input_pads[link->dstpad].config_props))
        config_link  = avfilter_default_config_input_link;
    if(config_link(link))
            return -1;

    return 0;
}

AVFilterPicRef *avfilter_get_video_buffer(AVFilterLink *link, int perms)
{
    AVFilterPicRef *ret = NULL;

    if(link->dst->input_pads[link->dstpad].get_video_buffer)
        ret = link->dst->input_pads[link->dstpad].get_video_buffer(link, perms);

    if(!ret)
        ret = avfilter_default_get_video_buffer(link, perms);

    return ret;
}

int avfilter_request_frame(AVFilterLink *link)
{
    const AVFilterPad *pad = &link->src->output_pads[link->srcpad];

    if(pad->request_frame)
        return pad->request_frame(link);
    else if(link->src->inputs[0])
        return avfilter_request_frame(link->src->inputs[0]);
    else return -1;
}

/* XXX: should we do the duplicating of the picture ref here, instead of
 * forcing the source filter to do it? */
void avfilter_start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    void (*start_frame)(AVFilterLink *, AVFilterPicRef *);

    start_frame = link->dst->input_pads[link->dstpad].start_frame;
    if(!start_frame)
        start_frame = avfilter_default_start_frame;

    start_frame(link, picref);
}

void avfilter_end_frame(AVFilterLink *link)
{
    void (*end_frame)(AVFilterLink *);

    end_frame = link->dst->input_pads[link->dstpad].end_frame;
    if(!end_frame)
        end_frame = avfilter_default_end_frame;

    end_frame(link);
}

void avfilter_draw_slice(AVFilterLink *link, uint8_t *data[4], int y, int h)
{
    if(!link->dst->input_pads[link->dstpad].draw_slice)
        return;

    link->dst->input_pads[link->dstpad].draw_slice(link, data, y, h);
}

static int filter_cmp(const void *aa, const void *bb)
{
    const AVFilter *a = *(const AVFilter **)aa, *b = *(const AVFilter **)bb;
    return strcmp(a->name, b->name);
}

AVFilter *avfilter_get_by_name(char *name)
{
    AVFilter key = { .name = name, };
    AVFilter *key2 = &key;
    AVFilter **ret;

    ret = bsearch(&key2, filters, filter_count, sizeof(AVFilter **), filter_cmp);
    if(ret)
        return *ret;
    return NULL;
}

/* FIXME: insert in order, rather than insert at end + resort */
void avfilter_register(AVFilter *filter)
{
    filters = av_realloc(filters, sizeof(AVFilter*) * (filter_count+1));
    filters[filter_count] = filter;
    qsort(filters, ++filter_count, sizeof(AVFilter **), filter_cmp);
}

void avfilter_init(void)
{
    avfilter_register(&vsrc_dummy);
    avfilter_register(&vsrc_ppm);
    avfilter_register(&vf_crop);
    avfilter_register(&vf_fps);
    avfilter_register(&vf_graph);
    avfilter_register(&vf_graphdesc);
    avfilter_register(&vf_graphfile);
    avfilter_register(&vf_overlay);
    avfilter_register(&vf_passthrough);
    avfilter_register(&vf_rgb2bgr);
    avfilter_register(&vf_slicify);
    avfilter_register(&vf_vflip);
}

void avfilter_uninit(void)
{
    av_freep(&filters);
    filter_count = 0;
}

static int pad_count(const AVFilterPad *pads)
{
    AVFilterPad *p = (AVFilterPad *) pads;
    int count;

    for(count = 0; p->name; count ++) p ++;
    return count;
}

static const char *filter_name(void *p)
{
    AVFilterContext *filter = p;
    return filter->filter->name;
}

AVFilterContext *avfilter_create(AVFilter *filter, char *inst_name)
{
    AVFilterContext *ret = av_malloc(sizeof(AVFilterContext));

    ret->av_class = av_mallocz(sizeof(AVClass));
    ret->av_class->item_name = filter_name;
    ret->filter   = filter;
    ret->name     = inst_name ? av_strdup(inst_name) : NULL;
    ret->priv     = av_mallocz(filter->priv_size);

    ret->input_count  = pad_count(filter->inputs);
    ret->input_pads   = av_malloc(sizeof(AVFilterPad) * ret->input_count);
    memcpy(ret->input_pads, filter->inputs, sizeof(AVFilterPad)*ret->input_count);
    ret->inputs       = av_mallocz(sizeof(AVFilterLink*) * ret->input_count);

    ret->output_count = pad_count(filter->outputs);
    ret->output_pads  = av_malloc(sizeof(AVFilterPad) * ret->output_count);
    memcpy(ret->output_pads, filter->outputs, sizeof(AVFilterPad)*ret->output_count);
    ret->outputs      = av_mallocz(sizeof(AVFilterLink*) * ret->output_count);

    return ret;
}

void avfilter_destroy(AVFilterContext *filter)
{
    int i;

    if(filter->filter->uninit)
        filter->filter->uninit(filter);

    for(i = 0; i < filter->input_count; i ++) {
        if(filter->inputs[i])
            filter->inputs[i]->src->outputs[filter->inputs[i]->srcpad] = NULL;
        av_free(filter->inputs[i]);
    }
    for(i = 0; i < filter->output_count; i ++) {
        if(filter->outputs[i])
            filter->outputs[i]->dst->inputs[filter->outputs[i]->dstpad] = NULL;
        av_free(filter->outputs[i]);
    }

    av_free(filter->name);
    av_free(filter->input_pads);
    av_free(filter->output_pads);
    av_free(filter->inputs);
    av_free(filter->outputs);
    av_free(filter->priv);
    av_free(filter->av_class);
    av_free(filter);
}

AVFilterContext *avfilter_create_by_name(char *name, char *inst_name)
{
    AVFilter *filt;

    if(!(filt = avfilter_get_by_name(name))) return NULL;
    return avfilter_create(filt, inst_name);
}

int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque)
{
    int ret;

    if(filter->filter->init)
        if((ret = filter->filter->init(filter, args, opaque))) return ret;
    return 0;
}

int *avfilter_make_format_list(int len, ...)
{
    int *ret, i;
    va_list vl;

    ret = av_malloc(sizeof(int) * (len + 1));
    va_start(vl, len);
    for(i = 0; i < len; i ++)
        ret[i] = va_arg(vl, int);
    va_end(vl);
    ret[len] = -1;

    return ret;
}

