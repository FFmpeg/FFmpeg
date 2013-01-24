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
#include "internal.h"
#include "formats.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct {
    int is_packed_rgb;
    uint8_t rgba_map[4];
} AlphaExtractContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat out_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
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

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *cur_buf)
{
    AlphaExtractContext *extract = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *out_buf =
        ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    int ret;

    if (!out_buf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avfilter_copy_buffer_ref_props(out_buf, cur_buf);

    if (extract->is_packed_rgb) {
        int x, y;
        uint8_t *pcur, *pout;
        for (y = 0; y < outlink->h; y++) {
            pcur = cur_buf->data[0] + y * cur_buf->linesize[0] + extract->rgba_map[A];
            pout = out_buf->data[0] + y * out_buf->linesize[0];
            for (x = 0; x < outlink->w; x++) {
                *pout = *pcur;
                pout += 1;
                pcur += 4;
            }
        }
    } else {
        const int linesize = abs(FFMIN(out_buf->linesize[Y], cur_buf->linesize[A]));
        int y;
        for (y = 0; y < outlink->h; y++) {
            memcpy(out_buf->data[Y] + y * out_buf->linesize[Y],
                   cur_buf->data[A] + y * cur_buf->linesize[A],
                   linesize);
        }
    }

    ret = ff_filter_frame(outlink, out_buf);

end:
    avfilter_unref_buffer(cur_buf);
    return ret;
}

static const AVFilterPad alphaextract_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
        .min_perms    = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad alphaextract_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_alphaextract = {
    .name           = "alphaextract",
    .description    = NULL_IF_CONFIG_SMALL("Extract an alpha channel as a "
                      "grayscale image component."),
    .priv_size      = sizeof(AlphaExtractContext),
    .query_formats  = query_formats,
    .inputs         = alphaextract_inputs,
    .outputs        = alphaextract_outputs,
};
