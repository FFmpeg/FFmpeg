/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2006 Ivo van Poorten
 * Copyright (c) 2006 Julian Hall
 * Copyright (c) 2002-2003 Brian J. Murrell
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Search for black frames to detect scene transitions.
 * Ported from MPlayer libmpcodecs/vf_blackframe.c.
 */

#include "avfilter.h"
#include "internal.h"

typedef struct {
    unsigned int bamount; ///< black amount
    unsigned int bthresh; ///< black threshold
    unsigned int frame;   ///< frame number
    unsigned int nblack;  ///< number of black pixels counted so far
    unsigned int last_keyframe; ///< frame number of the last received key-frame
} BlackFrameContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV410P, PIX_FMT_YUV420P, PIX_FMT_GRAY8, PIX_FMT_NV12,
        PIX_FMT_NV21, PIX_FMT_YUV444P, PIX_FMT_YUV422P, PIX_FMT_YUV411P,
        PIX_FMT_NONE
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BlackFrameContext *blackframe = ctx->priv;

    blackframe->bamount = 98;
    blackframe->bthresh = 32;
    blackframe->nblack = 0;
    blackframe->frame = 0;
    blackframe->last_keyframe = 0;

    if (args)
        sscanf(args, "%u:%u", &blackframe->bamount, &blackframe->bthresh);

    av_log(ctx, AV_LOG_INFO, "bamount:%u bthresh:%u\n",
           blackframe->bamount, blackframe->bthresh);

    if (blackframe->bamount > 100 || blackframe->bthresh > 255) {
        av_log(ctx, AV_LOG_ERROR, "Too big value for bamount (max is 100) or bthresh (max is 255)\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    BlackFrameContext *blackframe = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    int x, i;
    uint8_t *p = picref->data[0] + y * picref->linesize[0];

    for (i = 0; i < h; i++) {
        for (x = 0; x < inlink->w; x++)
            blackframe->nblack += p[x] < blackframe->bthresh;
        p += picref->linesize[0];
    }

    avfilter_draw_slice(ctx->outputs[0], y, h, slice_dir);
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BlackFrameContext *blackframe = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    int pblack = 0;

    if (picref->video->key_frame)
        blackframe->last_keyframe = blackframe->frame;

    pblack = blackframe->nblack * 100 / (inlink->w * inlink->h);
    if (pblack >= blackframe->bamount)
        av_log(ctx, AV_LOG_INFO, "frame:%u pblack:%u pos:%"PRId64" pts:%"PRId64" t:%f "
               "type:%c last_keyframe:%d\n",
               blackframe->frame, pblack, picref->pos, picref->pts,
               picref->pts == AV_NOPTS_VALUE ? -1 : picref->pts * av_q2d(inlink->time_base),
               av_get_picture_type_char(picref->video->pict_type), blackframe->last_keyframe);

    blackframe->frame++;
    blackframe->nblack = 0;
    avfilter_unref_buffer(picref);
    avfilter_end_frame(inlink->dst->outputs[0]);
}

AVFilter avfilter_vf_blackframe = {
    .name        = "blackframe",
    .description = NULL_IF_CONFIG_SMALL("Detect frames that are (almost) black."),

    .priv_size = sizeof(BlackFrameContext),
    .init      = init,

    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice       = draw_slice,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .start_frame      = ff_null_start_frame_keep_ref,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO },
                                  { .name = NULL}},
};
