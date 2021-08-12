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

#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct PixdescTestContext {
    const AVPixFmtDescriptor *pix_desc;
    uint32_t *line;
} PixdescTestContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    PixdescTestContext *priv = ctx->priv;
    av_freep(&priv->line);
}

static int config_props(AVFilterLink *inlink)
{
    PixdescTestContext *priv = inlink->dst->priv;

    priv->pix_desc = av_pix_fmt_desc_get(inlink->format);

    av_freep(&priv->line);
    if (!(priv->line = av_malloc_array(sizeof(*priv->line), inlink->w)))
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    PixdescTestContext *priv = inlink->dst->priv;
    AVFilterLink *outlink    = inlink->dst->outputs[0];
    AVFrame *out;
    int i, c, w = inlink->w, h = inlink->h;
    const int cw = AV_CEIL_RSHIFT(w, priv->pix_desc->log2_chroma_w);
    const int ch = AV_CEIL_RSHIFT(h, priv->pix_desc->log2_chroma_h);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    for (i = 0; i < 4; i++) {
        const int h1 = i == 1 || i == 2 ? ch : h;
        if (out->data[i]) {
            uint8_t *data = out->data[i] +
                (out->linesize[i] > 0 ? 0 : out->linesize[i] * (h1-1));
            memset(data, 0, FFABS(out->linesize[i]) * h1);
        }
    }

    /* copy palette */
    if (priv->pix_desc->flags & AV_PIX_FMT_FLAG_PAL)
        memcpy(out->data[1], in->data[1], AVPALETTE_SIZE);

    for (c = 0; c < priv->pix_desc->nb_components; c++) {
        const int w1 = c == 1 || c == 2 ? cw : w;
        const int h1 = c == 1 || c == 2 ? ch : h;

        for (i = 0; i < h1; i++) {
            av_read_image_line2(priv->line,
                               (void*)in->data,
                               in->linesize,
                               priv->pix_desc,
                               0, i, c, w1, 0, 4);

            av_write_image_line2(priv->line,
                                out->data,
                                out->linesize,
                                priv->pix_desc,
                                0, i, c, w1, 4);
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_pixdesctest_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
};

static const AVFilterPad avfilter_vf_pixdesctest_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_pixdesctest = {
    .name        = "pixdesctest",
    .description = NULL_IF_CONFIG_SMALL("Test pixel format definitions."),
    .priv_size   = sizeof(PixdescTestContext),
    .uninit      = uninit,
    FILTER_INPUTS(avfilter_vf_pixdesctest_inputs),
    FILTER_OUTPUTS(avfilter_vf_pixdesctest_outputs),
};
