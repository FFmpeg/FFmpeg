/*
 * Copyright (c) 2002 A'rpi
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
 * border detection filter
 * Ported from MPlayer libmpcodecs/vf_cropdetect.c.
 */

#include "libavutil/imgutils.h"
#include "avfilter.h"

typedef struct {
    int x1, y1, x2, y2;
    int limit;
    int round;
    int reset_count;
    int frame_nb;
    int max_pixsteps[4];
} CropDetectContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P, PIX_FMT_YUVJ420P,
        PIX_FMT_YUV422P, PIX_FMT_YUVJ422P,
        PIX_FMT_YUV444P, PIX_FMT_YUVJ444P,
        PIX_FMT_YUV411P, PIX_FMT_GRAY8,
        PIX_FMT_NV12,    PIX_FMT_NV21,
        PIX_FMT_NONE
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int checkline(void *ctx, const unsigned char *src, int stride, int len, int bpp)
{
    int total = 0;
    int div = len;

    switch (bpp) {
    case 1:
        while (--len >= 0) {
            total += src[0];
            src += stride;
        }
        break;
    case 3:
    case 4:
        while (--len >= 0) {
            total += src[0] + src[1] + src[2];
            src += stride;
        }
        div *= 3;
        break;
    }
    total /= div;

    av_log(ctx, AV_LOG_DEBUG, "total:%d\n", total);
    return total;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    CropDetectContext *cd = ctx->priv;

    cd->limit = 24;
    cd->round = 0;
    cd->reset_count = 0;
    cd->frame_nb = -2;

    if (args)
        sscanf(args, "%d:%d:%d", &cd->limit, &cd->round, &cd->reset_count);

    av_log(ctx, AV_LOG_INFO, "limit:%d round:%d reset_count:%d\n",
           cd->limit, cd->round, cd->reset_count);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *cd = ctx->priv;

    av_image_fill_max_pixsteps(cd->max_pixsteps, NULL,
                               &av_pix_fmt_descriptors[inlink->format]);

    cd->x1 = inlink->w - 1;
    cd->y1 = inlink->h - 1;
    cd->x2 = 0;
    cd->y2 = 0;

    return 0;
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *cd = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    int bpp = cd->max_pixsteps[0];
    int w, h, x, y, shrink_by;

    // ignore first 2 frames - they may be empty
    if (++cd->frame_nb > 0) {
        // Reset the crop area every reset_count frames, if reset_count is > 0
        if (cd->reset_count > 0 && cd->frame_nb > cd->reset_count) {
            cd->x1 = picref->video->w-1;
            cd->y1 = picref->video->h-1;
            cd->x2 = 0;
            cd->y2 = 0;
            cd->frame_nb = 1;
        }

        for (y = 0; y < cd->y1; y++) {
            if (checkline(ctx, picref->data[0] + picref->linesize[0] * y, bpp, picref->video->w, bpp) > cd->limit) {
                cd->y1 = y;
                break;
            }
        }

        for (y = picref->video->h-1; y > cd->y2; y--) {
            if (checkline(ctx, picref->data[0] + picref->linesize[0] * y, bpp, picref->video->w, bpp) > cd->limit) {
                cd->y2 = y;
                break;
            }
        }

        for (y = 0; y < cd->x1; y++) {
            if (checkline(ctx, picref->data[0] + bpp*y, picref->linesize[0], picref->video->h, bpp) > cd->limit) {
                cd->x1 = y;
                break;
            }
        }

        for (y = picref->video->w-1; y > cd->x2; y--) {
            if (checkline(ctx, picref->data[0] + bpp*y, picref->linesize[0], picref->video->h, bpp) > cd->limit) {
                cd->x2 = y;
                break;
            }
        }

        // round x and y (up), important for yuv colorspaces
        // make sure they stay rounded!
        x = (cd->x1+1) & ~1;
        y = (cd->y1+1) & ~1;

        w = cd->x2 - x + 1;
        h = cd->y2 - y + 1;

        // w and h must be divisible by 2 as well because of yuv
        // colorspace problems.
        if (cd->round <= 1)
            cd->round = 16;
        if (cd->round % 2)
            cd->round *= 2;

        shrink_by = w % cd->round;
        w -= shrink_by;
        x += (shrink_by/2 + 1) & ~1;

        shrink_by = h % cd->round;
        h -= shrink_by;
        y += (shrink_by/2 + 1) & ~1;

        av_log(ctx, AV_LOG_INFO,
               "x1:%d x2:%d y1:%d y2:%d w:%d h:%d x:%d y:%d pos:%"PRId64" pts:%"PRId64" t:%f crop=%d:%d:%d:%d\n",
               cd->x1, cd->x2, cd->y1, cd->y2, w, h, x, y, picref->pos, picref->pts,
               picref->pts == AV_NOPTS_VALUE ? -1 : picref->pts * av_q2d(inlink->time_base),
               w, h, x, y);
    }

    avfilter_end_frame(inlink->dst->outputs[0]);
}

AVFilter avfilter_vf_cropdetect = {
    .name        = "cropdetect",
    .description = NULL_IF_CONFIG_SMALL("Auto-detect crop size."),

    .priv_size = sizeof(CropDetectContext),
    .init      = init,

    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_input,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .start_frame      = avfilter_null_start_frame,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO },
                                  { .name = NULL}},
};
