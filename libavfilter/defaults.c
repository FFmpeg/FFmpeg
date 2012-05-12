/*
 * Filter layer - default implementations
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavutil/avassert.h"
#include "libavutil/audioconvert.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"

#include "avfilter.h"
#include "internal.h"
#include "formats.h"

static void set_common_formats(AVFilterContext *ctx, AVFilterFormats *fmts,
                               enum AVMediaType type, int offin, int offout)
{
    int i;
    for (i = 0; i < ctx->input_count; i++)
        if (ctx->inputs[i] && ctx->inputs[i]->type == type)
            avfilter_formats_ref(fmts,
                                 (AVFilterFormats **)((uint8_t *)ctx->inputs[i]+offout));

    for (i = 0; i < ctx->output_count; i++)
        if (ctx->outputs[i] && ctx->outputs[i]->type == type)
            avfilter_formats_ref(fmts,
                                 (AVFilterFormats **)((uint8_t *)ctx->outputs[i]+offin));

    if (!fmts->refcount) {
        av_free(fmts->formats);
        av_free(fmts->refs);
        av_free(fmts);
    }
}

void avfilter_set_common_pixel_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    set_common_formats(ctx, formats, AVMEDIA_TYPE_VIDEO,
                       offsetof(AVFilterLink, in_formats),
                       offsetof(AVFilterLink, out_formats));
}

void avfilter_set_common_sample_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    set_common_formats(ctx, formats, AVMEDIA_TYPE_AUDIO,
                       offsetof(AVFilterLink, in_formats),
                       offsetof(AVFilterLink, out_formats));
}

void avfilter_set_common_channel_layouts(AVFilterContext *ctx, AVFilterFormats *formats)
{
    set_common_formats(ctx, formats, AVMEDIA_TYPE_AUDIO,
                       offsetof(AVFilterLink, in_channel_layouts),
                       offsetof(AVFilterLink, out_channel_layouts));
}

#if FF_API_PACKING
void avfilter_set_common_packing_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    set_common_formats(ctx, formats, AVMEDIA_TYPE_AUDIO,
                       offsetof(AVFilterLink, in_packing),
                       offsetof(AVFilterLink, out_packing));
}
#endif
