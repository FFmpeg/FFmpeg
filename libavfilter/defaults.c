/*
 * Filter layer - default implementations
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
#include "imgconvert.h"

/* TODO: buffer pool.  see comment for avfilter_default_get_video_buffer() */
void avfilter_default_free_video_buffer(AVFilterPic *pic)
{
    av_free(pic->data[0]);
    av_free(pic);
}

#define ALIGN(a) do{ \
                     (a) = ((a) + 15) & (~15); \
                 } while(0);

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterPicRef *avfilter_default_get_video_buffer(AVFilterLink *link, int perms)
{
    AVFilterPic *pic = av_mallocz(sizeof(AVFilterPic));
    AVFilterPicRef *ref = av_mallocz(sizeof(AVFilterPicRef));
    int i, tempsize;
    char *buf;

    ref->pic   = pic;
    ref->w     = link->w;
    ref->h     = link->h;

    /* make sure the buffer gets read permission or it's useless for output */
    ref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    pic->format   = link->format;
    pic->free     = avfilter_default_free_video_buffer;
    ff_fill_linesize((AVPicture *)pic, pic->format, ref->w);

    for (i=0; i<4;i++)
        ALIGN(pic->linesize[i]);

    tempsize = ff_fill_pointer((AVPicture *)pic, NULL, pic->format, ref->h);
    buf = av_malloc(tempsize);
    ff_fill_pointer((AVPicture *)pic, buf, pic->format, ref->h);

    memcpy(ref->data,     pic->data,     sizeof(pic->data));
    memcpy(ref->linesize, pic->linesize, sizeof(pic->linesize));

    return ref;
}

void avfilter_default_start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    AVFilterLink *out = NULL;

    if(link->dst->output_count)
        out = link->dst->outputs[0];

    if(out) {
        out->outpic      = avfilter_get_video_buffer(out, AV_PERM_WRITE);
        out->outpic->pts = picref->pts;
        avfilter_start_frame(out, avfilter_ref_pic(out->outpic, ~0));
    }
}

void avfilter_default_end_frame(AVFilterLink *link)
{
    AVFilterLink *out = NULL;

    if(link->dst->output_count)
        out = link->dst->outputs[0];

    avfilter_unref_pic(link->cur_pic);
    link->cur_pic = NULL;

    if(out) {
        if(out->outpic) {
            avfilter_unref_pic(out->outpic);
            out->outpic = NULL;
        }
        avfilter_end_frame(out);
    }
}

/**
 * default config_link() implementation for output video links to simplify
 * the implementation of one input one output video filters */
int avfilter_default_config_output_link(AVFilterLink *link)
{
    if(link->src->input_count && link->src->inputs[0]) {
        link->w = link->src->inputs[0]->w;
        link->h = link->src->inputs[0]->h;
    } else {
        /* XXX: any non-simple filter which would cause this branch to be taken
         * really should implement its own config_props() for this link. */
        return -1;
    }

    return 0;
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 *
 * FIXME: this will need changed for filters with a mix of pad types
 * (video + audio, etc)
 */
void avfilter_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    int count = 0, i;

    for(i = 0; i < ctx->input_count; i ++) {
        if(ctx->inputs[i]) {
            avfilter_formats_ref(formats, &ctx->inputs[i]->out_formats);
            count ++;
        }
    }
    for(i = 0; i < ctx->output_count; i ++) {
        if(ctx->outputs[i]) {
            avfilter_formats_ref(formats, &ctx->outputs[i]->in_formats);
            count ++;
        }
    }

    if(!count) {
        av_free(formats->formats);
        av_free(formats->refs);
        av_free(formats);
    }
}

int avfilter_default_query_formats(AVFilterContext *ctx)
{
    avfilter_set_common_formats(ctx, avfilter_all_colorspaces());
    return 0;
}

