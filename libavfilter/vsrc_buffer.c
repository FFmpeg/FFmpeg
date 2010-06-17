/*
 * Memory buffer source filter
 * Copyright (c) 2008 Vitor Sessak
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
#include "vsrc_buffer.h"

typedef struct {
    int64_t           pts;
    AVFrame           frame;
    int               has_frame;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        pixel_aspect;
} BufferSourceContext;


int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame,
                             int64_t pts, AVRational pixel_aspect)
{
    BufferSourceContext *c = buffer_filter->priv;

    if (c->has_frame) {
        av_log(buffer_filter, AV_LOG_ERROR,
               "Buffering several frames is not supported. "
               "Please consume all available frames before adding a new one.\n"
            );
        //return -1;
    }

    memcpy(c->frame.data    , frame->data    , sizeof(frame->data));
    memcpy(c->frame.linesize, frame->linesize, sizeof(frame->linesize));
    c->frame.interlaced_frame= frame->interlaced_frame;
    c->frame.top_field_first = frame->top_field_first;
    c->pts = pts;
    c->pixel_aspect = pixel_aspect;
    c->has_frame = 1;

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSourceContext *c = ctx->priv;

    if(args && sscanf(args, "%d:%d:%d", &c->w, &c->h, &c->pix_fmt) == 3)
        return 0;

    av_log(ctx, AV_LOG_ERROR, "init() expected 3 arguments:'%s'\n", args);
    return -1;
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    enum PixelFormat pix_fmts[] = { c->pix_fmt, PIX_FMT_NONE };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    link->w = c->w;
    link->h = c->h;

    return 0;
}


static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFilterPicRef *picref;

    if (!c->has_frame) {
        av_log(link->src, AV_LOG_ERROR,
               "request_frame() called with no available frame!\n");
        //return -1;
    }

    /* This picture will be needed unmodified later for decoding the next
     * frame */
    picref = avfilter_get_video_buffer(link, AV_PERM_WRITE | AV_PERM_PRESERVE |
                                       AV_PERM_REUSE2,
                                       link->w, link->h);

    av_picture_copy((AVPicture *)&picref->data, (AVPicture *)&c->frame,
                    picref->pic->format, link->w, link->h);

    picref->pts = c->pts;
    picref->pixel_aspect = c->pixel_aspect;
    picref->interlaced      = c->frame.interlaced_frame;
    picref->top_field_first = c->frame.top_field_first;
    avfilter_start_frame(link, avfilter_ref_pic(picref, ~0));
    avfilter_draw_slice(link, 0, link->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_pic(picref);

    c->has_frame = 0;

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    return !!(c->has_frame);
}

AVFilter avfilter_vsrc_buffer =
{
    .name      = "buffer",
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};
