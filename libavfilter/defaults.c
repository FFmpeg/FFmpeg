/*
 * Filter layer - default implementations
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

#include "libavutil/audioconvert.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"

#include "avfilter.h"
#include "internal.h"
#include "formats.h"

/* TODO: buffer pool.  see comment for avfilter_default_get_video_buffer() */
void ff_avfilter_default_free_buffer(AVFilterBuffer *ptr)
{
    if (ptr->extended_data != ptr->data)
        av_freep(&ptr->extended_data);
    av_free(ptr->data[0]);
    av_free(ptr);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterBufferRef *avfilter_default_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    int linesize[4];
    uint8_t *data[4];
    AVFilterBufferRef *picref = NULL;

    // +2 is needed for swscaler, +16 to be SIMD-friendly
    if (av_image_alloc(data, linesize, w, h, link->format, 16) < 0)
        return NULL;

    picref = avfilter_get_video_buffer_ref_from_arrays(data, linesize,
                                                       perms, w, h, link->format);
    if (!picref) {
        av_free(data[0]);
        return NULL;
    }

    return picref;
}

void avfilter_default_start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        outlink->out_buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
        avfilter_start_frame(outlink, avfilter_ref_buffer(outlink->out_buf, ~0));
    }
}

void avfilter_default_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink)
        avfilter_draw_slice(outlink, y, h, slice_dir);
}

void avfilter_default_end_frame(AVFilterLink *inlink)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    avfilter_unref_buffer(inlink->cur_buf);
    inlink->cur_buf = NULL;

    if (outlink) {
        if (outlink->out_buf) {
            avfilter_unref_buffer(outlink->out_buf);
            outlink->out_buf = NULL;
        }
        avfilter_end_frame(outlink);
    }
}

/**
 * default config_link() implementation for output video links to simplify
 * the implementation of one input one output video filters */
int avfilter_default_config_output_link(AVFilterLink *link)
{
    if (link->src->input_count && link->src->inputs[0]) {
        if (link->type == AVMEDIA_TYPE_VIDEO) {
            link->w = link->src->inputs[0]->w;
            link->h = link->src->inputs[0]->h;
            link->time_base = link->src->inputs[0]->time_base;
        }
    } else {
        /* XXX: any non-simple filter which would cause this branch to be taken
         * really should implement its own config_props() for this link. */
        return -1;
    }

    return 0;
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

int avfilter_default_query_formats(AVFilterContext *ctx)
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
