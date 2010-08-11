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

#include "libavcore/imgutils.h"
#include "avfilter.h"

/* TODO: buffer pool.  see comment for avfilter_default_get_video_buffer() */
static void avfilter_default_free_buffer(AVFilterBuffer *ptr)
{
    av_free(ptr->data[0]);
    av_free(ptr);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterBufferRef *avfilter_default_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    AVFilterBuffer *pic = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *ref = av_mallocz(sizeof(AVFilterBufferRef));
    int i, tempsize;
    char *buf;

    ref->buf         = pic;
    ref->video       = av_mallocz(sizeof(AVFilterBufferRefVideoProps));
    ref->video->w    = w;
    ref->video->h    = h;

    /* make sure the buffer gets read permission or it's useless for output */
    ref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    ref->format   = link->format;
    pic->free     = avfilter_default_free_buffer;
    av_fill_image_linesizes(pic->linesize, ref->format, ref->video->w);

    for (i=0; i<4;i++)
        pic->linesize[i] = FFALIGN(pic->linesize[i], 16);

    tempsize = av_fill_image_pointers(pic->data, ref->format, ref->video->h, NULL, pic->linesize);
    buf = av_malloc(tempsize + 16); // +2 is needed for swscaler, +16 to be
                                    // SIMD-friendly
    av_fill_image_pointers(pic->data, ref->format, ref->video->h, buf, pic->linesize);

    memcpy(ref->data,     pic->data,     4*sizeof(pic->data[0]));
    memcpy(ref->linesize, pic->linesize, 4*sizeof(pic->linesize[0]));

    return ref;
}

void avfilter_default_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterLink *out = NULL;

    if(link->dst->output_count)
        out = link->dst->outputs[0];

    if(out) {
        out->out_buf      = avfilter_get_video_buffer(out, AV_PERM_WRITE, out->w, out->h);
        avfilter_copy_buffer_ref_props(out->out_buf, picref);
        avfilter_start_frame(out, avfilter_ref_buffer(out->out_buf, ~0));
    }
}

void avfilter_default_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    AVFilterLink *out = NULL;

    if(link->dst->output_count)
        out = link->dst->outputs[0];

    if(out)
        avfilter_draw_slice(out, y, h, slice_dir);
}

void avfilter_default_end_frame(AVFilterLink *link)
{
    AVFilterLink *out = NULL;

    if(link->dst->output_count)
        out = link->dst->outputs[0];

    avfilter_unref_buffer(link->cur_buf);
    link->cur_buf = NULL;

    if(out) {
        if(out->out_buf) {
            avfilter_unref_buffer(out->out_buf);
            out->out_buf = NULL;
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
    enum AVMediaType type = ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    avfilter_set_common_formats(ctx, avfilter_all_formats(type));
    return 0;
}

void avfilter_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    avfilter_start_frame(link->dst->outputs[0], picref);
}

void avfilter_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    avfilter_draw_slice(link->dst->outputs[0], y, h, slice_dir);
}

void avfilter_null_end_frame(AVFilterLink *link)
{
    avfilter_end_frame(link->dst->outputs[0]);
}

AVFilterBufferRef *avfilter_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    return avfilter_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

