/*
 * Copyright (c) 2009 Stefano Sabatini
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
 * pixdesc test filter
 */

#include "libavutil/pixdesc.h"
#include "avfilter.h"

typedef struct {
    const AVPixFmtDescriptor *pix_desc;
    uint16_t *line;
} PixdescTestContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    PixdescTestContext *priv = ctx->priv;
    av_freep(&priv->line);
}

static int config_props(AVFilterLink *inlink)
{
    PixdescTestContext *priv = inlink->dst->priv;

    priv->pix_desc = &av_pix_fmt_descriptors[inlink->format];

    if (!(priv->line = av_malloc(sizeof(*priv->line) * inlink->w)))
        return AVERROR(ENOMEM);

    return 0;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    PixdescTestContext *priv = inlink->dst->priv;
    AVFilterLink *outlink    = inlink->dst->outputs[0];
    AVFilterBufferRef *outpicref;
    int i;

    outlink->out_buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                                outlink->w, outlink->h);
    outpicref = outlink->out_buf;
    avfilter_copy_buffer_ref_props(outpicref, picref);

    for (i = 0; i < 4; i++) {
        int h = outlink->h;
        h = i == 1 || i == 2 ? h>>priv->pix_desc->log2_chroma_h : h;
        if (outpicref->data[i]) {
            uint8_t *data = outpicref->data[i] +
                (outpicref->linesize[i] > 0 ? 0 : outpicref->linesize[i] * (h-1));
            memset(data, 0, FFABS(outpicref->linesize[i]) * h);
        }
    }

    /* copy palette */
    if (priv->pix_desc->flags & PIX_FMT_PAL)
        memcpy(outpicref->data[1], outpicref->data[1], 256*4);

    avfilter_start_frame(outlink, avfilter_ref_buffer(outpicref, ~0));
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    PixdescTestContext *priv = inlink->dst->priv;
    AVFilterBufferRef *inpic    = inlink->cur_buf;
    AVFilterBufferRef *outpic   = inlink->dst->outputs[0]->out_buf;
    int i, c, w = inlink->w;

    for (c = 0; c < priv->pix_desc->nb_components; c++) {
        int w1 = c == 1 || c == 2 ? w>>priv->pix_desc->log2_chroma_w : w;
        int h1 = c == 1 || c == 2 ? h>>priv->pix_desc->log2_chroma_h : h;
        int y1 = c == 1 || c == 2 ? y>>priv->pix_desc->log2_chroma_h : y;

        for (i = y1; i < y1 + h1; i++) {
            av_read_image_line(priv->line,
                               inpic->data,
                               inpic->linesize,
                               priv->pix_desc,
                               0, i, c, w1, 0);

            av_write_image_line(priv->line,
                                outpic->data,
                                outpic->linesize,
                                priv->pix_desc,
                                0, i, c, w1);
        }
    }

    avfilter_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
}

AVFilter avfilter_vf_pixdesctest = {
    .name        = "pixdesctest",
    .description = NULL_IF_CONFIG_SMALL("Test pixel format definitions."),

    .priv_size = sizeof(PixdescTestContext),
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .start_frame     = start_frame,
                                    .draw_slice      = draw_slice,
                                    .config_props    = config_props,
                                    .min_perms       = AV_PERM_READ, },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
