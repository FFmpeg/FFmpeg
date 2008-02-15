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

/* TODO: buffer pool.  see comment for avfilter_default_get_video_buffer() */
void avfilter_default_free_video_buffer(AVFilterPic *pic)
{
    avpicture_free((AVPicture *) pic);
    av_free(pic);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterPicRef *avfilter_default_get_video_buffer(AVFilterLink *link, int perms)
{
    AVFilterPic *pic = av_mallocz(sizeof(AVFilterPic));
    AVFilterPicRef *ref = av_mallocz(sizeof(AVFilterPicRef));

    ref->pic   = pic;
    ref->w     = link->w;
    ref->h     = link->h;

    /* make sure the buffer gets read permission or it's useless for output */
    ref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    pic->format   = link->format;
    pic->free     = avfilter_default_free_video_buffer;
    avpicture_alloc((AVPicture *)pic, pic->format, ref->w, ref->h);

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
 * default config_link() implementation for input video links to simplify
 * the implementation of one input one output video filters */
int avfilter_default_config_input_link(AVFilterLink *link)
{
    if(!link->dst->output_count)
        return 0;
    return avfilter_config_link(link->dst->outputs[0]);
}

/**
 * default query_formats() implementation for output video links to simplify
 * the implementation of one input one output video filters */
int *avfilter_default_query_output_formats(AVFilterLink *link)
{
    if(link->src->input_count && link->src->inputs[0])
        return avfilter_make_format_list(1, link->src->inputs[0]->format);
    else
        /* XXX: any non-simple filter which would cause this branch to be taken
         * really should implement its own query_formats() for this link */
        return avfilter_make_format_list(0);
}

