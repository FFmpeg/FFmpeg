/*
 * Copyright (c) 2008 Affine Systems, Inc (Michael Sullivan, Bobby Impollonia)
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

/**
 * @file
 * Box drawing filter. Also a nice template for a filter that needs to
 * write in the input frame.
 */

#include "libavutil/colorspace.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct {
    int x, y, w, h;
    unsigned char yuv_color[4];
    int vsub, hsub;   ///< chroma subsampling
} DrawBoxContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    DrawBoxContext *drawbox= ctx->priv;
    char color_str[1024] = "black";
    uint8_t rgba_color[4];

    drawbox->x = drawbox->y = drawbox->w = drawbox->h = 0;

    if (args)
        sscanf(args, "%d:%d:%d:%d:%s",
               &drawbox->x, &drawbox->y, &drawbox->w, &drawbox->h, color_str);

    if (av_parse_color(rgba_color, color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    drawbox->yuv_color[Y] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
    drawbox->yuv_color[U] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
    drawbox->yuv_color[V] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
    drawbox->yuv_color[A] = rgba_color[3];

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV410P,
        PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,
        PIX_FMT_YUV440P,  PIX_FMT_YUVJ440P,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    DrawBoxContext *drawbox = inlink->dst->priv;

    drawbox->hsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    drawbox->vsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

    if (drawbox->w == 0) drawbox->w = inlink->w;
    if (drawbox->h == 0) drawbox->h = inlink->h;

    av_log(inlink->dst, AV_LOG_INFO, "x:%d y:%d w:%d h:%d color:0x%02X%02X%02X%02X\n",
           drawbox->w, drawbox->y, drawbox->w, drawbox->h,
           drawbox->yuv_color[Y], drawbox->yuv_color[U], drawbox->yuv_color[V], drawbox->yuv_color[A]);

    return 0;
}

static void draw_slice(AVFilterLink *inlink, int y0, int h, int slice_dir)
{
    DrawBoxContext *drawbox = inlink->dst->priv;
    int plane, x, y, xb = drawbox->x, yb = drawbox->y;
    unsigned char *row[4];
    AVFilterBufferRef *picref = inlink->cur_buf;

    for (y = FFMAX(yb, y0); y < (y0 + h) && y < (yb + drawbox->h); y++) {
        row[0] = picref->data[0] + y * picref->linesize[0];

        for (plane = 1; plane < 3; plane++)
            row[plane] = picref->data[plane] +
                picref->linesize[plane] * (y >> drawbox->vsub);

        for (x = FFMAX(xb, 0); x < (xb + drawbox->w) && x < picref->video->w; x++) {
            double alpha = (double)drawbox->yuv_color[A] / 255;

            if ((y - yb < 3) || (yb + drawbox->h - y < 4) ||
                (x - xb < 3) || (xb + drawbox->w - x < 4)) {
                row[0][x                 ] = (1 - alpha) * row[0][x                 ] + alpha * drawbox->yuv_color[Y];
                row[1][x >> drawbox->hsub] = (1 - alpha) * row[1][x >> drawbox->hsub] + alpha * drawbox->yuv_color[U];
                row[2][x >> drawbox->hsub] = (1 - alpha) * row[2][x >> drawbox->hsub] + alpha * drawbox->yuv_color[V];
            }
        }
    }

    ff_draw_slice(inlink->dst->outputs[0], y0, h, 1);
}

AVFilter avfilter_vf_drawbox = {
    .name      = "drawbox",
    .description = NULL_IF_CONFIG_SMALL("Draw a colored box on the input video."),
    .priv_size = sizeof(DrawBoxContext),
    .init      = init,

    .query_formats   = query_formats,
    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_input,
                                    .get_video_buffer = ff_null_get_video_buffer,
                                    .start_frame      = ff_null_start_frame,
                                    .draw_slice       = draw_slice,
                                    .end_frame        = ff_null_end_frame,
                                    .min_perms        = AV_PERM_WRITE | AV_PERM_READ,
                                    .rej_perms        = AV_PERM_PRESERVE },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
