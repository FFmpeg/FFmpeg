/*
 * Copyright (c) 2007 Benoit Fouet
 * Copyright (c) 2010 Stefano Sabatini
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
 * horizontal flip filter
 */

#include "avfilter.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

typedef struct {
    int max_step[4];    ///< max pixel step for each plane, expressed as a number of bytes
    int hsub, vsub;     ///< chroma subsampling
} FlipContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB48BE,      PIX_FMT_RGB48LE,
        PIX_FMT_BGR48BE,      PIX_FMT_BGR48LE,
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

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    FlipContext *flip = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];

    av_image_fill_max_pixsteps(flip->max_step, NULL, pix_desc);
    flip->hsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    flip->vsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

    return 0;
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    FlipContext *flip = inlink->dst->priv;
    AVFilterBufferRef *inpic  = inlink->cur_buf;
    AVFilterBufferRef *outpic = inlink->dst->outputs[0]->out_buf;
    uint8_t *inrow, *outrow;
    int i, j, plane, step, hsub, vsub;

    for (plane = 0; plane < 4 && inpic->data[plane]; plane++) {
        step = flip->max_step[plane];
        hsub = (plane == 1 || plane == 2) ? flip->hsub : 0;
        vsub = (plane == 1 || plane == 2) ? flip->vsub : 0;

        outrow = outpic->data[plane] + (y>>vsub) * outpic->linesize[plane];
        inrow  = inpic ->data[plane] + (y>>vsub) * inpic ->linesize[plane] + ((inlink->w >> hsub) - 1) * step;
        for (i = 0; i < h>>vsub; i++) {
            switch (step) {
            case 1:
                for (j = 0; j < (inlink->w >> hsub); j++)
                    outrow[j] = inrow[-j];
            break;

            case 2:
            {
                uint16_t *outrow16 = (uint16_t *)outrow;
                uint16_t * inrow16 = (uint16_t *) inrow;
                for (j = 0; j < (inlink->w >> hsub); j++)
                    outrow16[j] = inrow16[-j];
            }
            break;

            case 3:
            {
                uint8_t *in  =  inrow;
                uint8_t *out = outrow;
                for (j = 0; j < (inlink->w >> hsub); j++, out += 3, in -= 3) {
                    int32_t v = AV_RB24(in);
                    AV_WB24(out, v);
                }
            }
            break;

            case 4:
            {
                uint32_t *outrow32 = (uint32_t *)outrow;
                uint32_t * inrow32 = (uint32_t *) inrow;
                for (j = 0; j < (inlink->w >> hsub); j++)
                    outrow32[j] = inrow32[-j];
            }
            break;

            default:
                for (j = 0; j < (inlink->w >> hsub); j++)
                    memcpy(outrow + j*step, inrow - j*step, step);
            }

            inrow  += inpic ->linesize[plane];
            outrow += outpic->linesize[plane];
        }
    }

    avfilter_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
}

AVFilter avfilter_vf_hflip = {
    .name      = "hflip",
    .description = NULL_IF_CONFIG_SMALL("Horizontally flip the input video."),
    .priv_size = sizeof(FlipContext),
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice      = draw_slice,
                                    .config_props    = config_props,
                                    .min_perms       = AV_PERM_READ, },
                                  { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
