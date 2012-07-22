/*
 * Copyright (c) 2012 Steven Robertson
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
 * simple channel-swapping filter to get at the alpha component
 */

#include <string.h>

#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct {
    int is_packed_rgb;
    uint8_t rgba_map[4];
} AlphaExtractContext;

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat in_fmts[] = {
        PIX_FMT_YUVA444P, PIX_FMT_YUVA422P, PIX_FMT_YUVA420P,
        PIX_FMT_RGBA, PIX_FMT_BGRA, PIX_FMT_ARGB, PIX_FMT_ABGR,
        PIX_FMT_NONE
    };
    enum PixelFormat out_fmts[] = { PIX_FMT_GRAY8, PIX_FMT_NONE };
    ff_formats_ref(ff_make_format_list(in_fmts), &ctx->inputs[0]->out_formats);
    ff_formats_ref(ff_make_format_list(out_fmts), &ctx->outputs[0]->in_formats);
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AlphaExtractContext *extract = inlink->dst->priv;
    extract->is_packed_rgb =
        ff_fill_rgba_map(extract->rgba_map, inlink->format) >= 0;
    return 0;
}

static int draw_slice(AVFilterLink *inlink, int y0, int h, int slice_dir)
{
    AlphaExtractContext *extract = inlink->dst->priv;
    AVFilterBufferRef *cur_buf = inlink->cur_buf;
    AVFilterBufferRef *out_buf = inlink->dst->outputs[0]->out_buf;

    if (extract->is_packed_rgb) {
        int x, y;
        uint8_t *pin, *pout;
        for (y = y0; y < (y0 + h); y++) {
            pin = cur_buf->data[0] + y * cur_buf->linesize[0] + extract->rgba_map[A];
            pout = out_buf->data[0] + y * out_buf->linesize[0];
            for (x = 0; x < out_buf->video->w; x++) {
                *pout = *pin;
                pout += 1;
                pin += 4;
            }
        }
    } else if (cur_buf->linesize[A] == out_buf->linesize[Y]) {
        const int linesize = cur_buf->linesize[A];
        memcpy(out_buf->data[Y] + y0 * linesize,
               cur_buf->data[A] + y0 * linesize,
               linesize * h);
    } else {
        const int linesize = FFMIN(out_buf->linesize[Y], cur_buf->linesize[A]);
        int y;
        for (y = y0; y < (y0 + h); y++) {
            memcpy(out_buf->data[Y] + y * out_buf->linesize[Y],
                   cur_buf->data[A] + y * cur_buf->linesize[A],
                   linesize);
        }
    }
    return ff_draw_slice(inlink->dst->outputs[0], y0, h, slice_dir);
}

AVFilter avfilter_vf_alphaextract = {
    .name           = "alphaextract",
    .description    = NULL_IF_CONFIG_SMALL("Extract an alpha channel as a "
                      "grayscale image component."),
    .priv_size      = sizeof(AlphaExtractContext),
    .query_formats  = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .config_props     = config_input,
          .draw_slice       = draw_slice,
          .min_perms        = AV_PERM_READ },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
      { .name               = "default",
        .type               = AVMEDIA_TYPE_VIDEO, },
      { .name = NULL }
    },
};
