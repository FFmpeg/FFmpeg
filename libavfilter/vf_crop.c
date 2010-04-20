/*
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

/**
 * @file
 * video crop filter
 */

#include "avfilter.h"
#include "libavutil/pixdesc.h"

typedef struct {
    int  x;             ///< x offset of the non-cropped area with respect to the input area
    int  y;             ///< y offset of the non-cropped area with respect to the input area
    int  w;             ///< width of the cropped area
    int  h;             ///< height of the cropped area

    int bpp;            ///< bits per pixel
    int hsub, vsub;     ///< chroma subsampling
} CropContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB48BE,      PIX_FMT_RGB48LE,
        PIX_FMT_ARGB,         PIX_FMT_RGBA,
        PIX_FMT_ABGR,         PIX_FMT_BGRA,
        PIX_FMT_RGB24,        PIX_FMT_BGR24,
        PIX_FMT_RGB565BE,     PIX_FMT_RGB565LE,
        PIX_FMT_RGB555BE,     PIX_FMT_RGB555LE,
        PIX_FMT_BGR565BE,     PIX_FMT_BGR565LE,
        PIX_FMT_BGR555BE,     PIX_FMT_BGR555LE,
        PIX_FMT_GRAY16BE,     PIX_FMT_GRAY16LE,
        PIX_FMT_YUV420P16LE,  PIX_FMT_YUV420P16BE,
        PIX_FMT_YUV422P16LE,  PIX_FMT_YUV422P16BE,
        PIX_FMT_YUV444P16LE,  PIX_FMT_YUV444P16BE,
        PIX_FMT_YUV444P,      PIX_FMT_YUV422P,
        PIX_FMT_YUV420P,      PIX_FMT_YUV411P,
        PIX_FMT_YUV410P,      PIX_FMT_YUV440P,
        PIX_FMT_YUVJ444P,     PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ420P,     PIX_FMT_YUVJ440P,
        PIX_FMT_YUVA420P,
        PIX_FMT_RGB8,         PIX_FMT_BGR8,
        PIX_FMT_RGB4_BYTE,    PIX_FMT_BGR4_BYTE,
        PIX_FMT_PAL8,         PIX_FMT_GRAY8,
        PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    CropContext *crop = ctx->priv;

    if (args)
        sscanf(args, "%d:%d:%d:%d", &crop->x, &crop->y, &crop->w, &crop->h);

    return 0;
}

static int config_input(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    CropContext *crop = ctx->priv;

    switch (link->format) {
    case PIX_FMT_RGB48BE:
    case PIX_FMT_RGB48LE:
        crop->bpp = 48;
        break;
    case PIX_FMT_ARGB:
    case PIX_FMT_RGBA:
    case PIX_FMT_ABGR:
    case PIX_FMT_BGRA:
        crop->bpp = 32;
        break;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        crop->bpp = 24;
        break;
    case PIX_FMT_RGB565BE:
    case PIX_FMT_RGB565LE:
    case PIX_FMT_RGB555BE:
    case PIX_FMT_RGB555LE:
    case PIX_FMT_BGR565BE:
    case PIX_FMT_BGR565LE:
    case PIX_FMT_BGR555BE:
    case PIX_FMT_BGR555LE:
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_YUV420P16LE:
    case PIX_FMT_YUV420P16BE:
    case PIX_FMT_YUV422P16LE:
    case PIX_FMT_YUV422P16BE:
    case PIX_FMT_YUV444P16LE:
    case PIX_FMT_YUV444P16BE:
        crop->bpp = 16;
        break;
    default:
        crop->bpp = 8;
    }

    avcodec_get_chroma_sub_sample(link->format, &crop->hsub, &crop->vsub);

    if (crop->w == 0)
        crop->w = link->w - crop->x;
    if (crop->h == 0)
        crop->h = link->h - crop->y;

    crop->x &= ~((1 << crop->hsub) - 1);
    crop->y &= ~((1 << crop->vsub) - 1);

    av_log(link->dst, AV_LOG_INFO, "x:%d y:%d w:%d h:%d\n",
           crop->x, crop->y, crop->w, crop->h);

    if (crop->x <  0 || crop->y <  0                    ||
        crop->w <= 0 || crop->h <= 0                    ||
        (unsigned)crop->x + (unsigned)crop->w > link->w ||
        (unsigned)crop->y + (unsigned)crop->h > link->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Output area %d:%d:%d:%d not within the input area 0:0:%d:%d or zero-sized\n",
               crop->x, crop->y, crop->w, crop->h, link->w, link->h);
        return -1;
    }

    return 0;
}

static int config_output(AVFilterLink *link)
{
    CropContext *crop = link->src->priv;

    link->w = crop->w;
    link->h = crop->h;

    return 0;
}

static void start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    CropContext *crop = link->dst->priv;
    AVFilterPicRef *ref2 = avfilter_ref_pic(picref, ~0);
    int i;

    ref2->w        = crop->w;
    ref2->h        = crop->h;

    ref2->data[0] += crop->y * ref2->linesize[0];
    ref2->data[0] += (crop->x * crop->bpp) >> 3;

    if (!(av_pix_fmt_descriptors[link->format].flags & PIX_FMT_PAL)) {
        for (i = 1; i < 3; i ++) {
            if (ref2->data[i]) {
                ref2->data[i] += (crop->y >> crop->vsub) * ref2->linesize[i];
                ref2->data[i] += ((crop->x * crop->bpp) >> 3) >> crop->hsub;
            }
        }
    }

    /* alpha plane */
    if (ref2->data[3]) {
        ref2->data[3] += crop->y * ref2->linesize[3];
        ref2->data[3] += (crop->x * crop->bpp) >> 3;
    }

    avfilter_start_frame(link->dst->outputs[0], ref2);
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = link->dst;
    CropContext *crop = ctx->priv;

    if (y >= crop->y + crop->h || y + h <= crop->y)
        return;

    if (y < crop->y) {
        h -= crop->y - y;
        y  = crop->y;
    }
    if (y + h > crop->y + crop->h)
        h = crop->y + crop->h - y;

    avfilter_draw_slice(ctx->outputs[0], y - crop->y, h, slice_dir);
}

AVFilter avfilter_vf_crop = {
    .name      = "crop",
    .description = NULL_IF_CONFIG_SMALL("Crop the input video to x:y:width:height."),

    .priv_size = sizeof(CropContext),

    .query_formats = query_formats,
    .init          = init,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .config_props     = config_input, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_output, },
                                  { .name = NULL}},
};
