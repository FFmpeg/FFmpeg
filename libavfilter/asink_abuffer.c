/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * audio buffer sink
 */

#include "avfilter.h"
#include "asink_abuffer.h"

static void filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
}

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    if (!opaque) {
        av_log(ctx, AV_LOG_ERROR, "Opaque field required, please pass"
                                  " an initialized ABufferSinkContext");
        return AVERROR(EINVAL);
    }
    memcpy(ctx->priv, opaque, sizeof(ABufferSinkContext));

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ABufferSinkContext *abuffersink = ctx->priv;
    AVFilterFormats *formats = NULL;

    if (!(formats = avfilter_make_format_list(abuffersink->sample_fmts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_sample_formats(ctx, formats);

    if (!(formats = avfilter_make_format64_list(abuffersink->channel_layouts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_channel_layouts(ctx, formats);

    if (!(formats = avfilter_make_format_list(abuffersink->packing_fmts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

int av_asink_abuffer_get_audio_buffer_ref(AVFilterContext *abuffersink,
                                          AVFilterBufferRef **samplesref,
                                          int av_unused flags)
{
    int ret;
    AVFilterLink * const inlink = abuffersink->inputs[0];

    if ((ret = avfilter_request_frame(inlink)))
        return ret;
    if (!inlink->cur_buf)
        return AVERROR(EINVAL);
    *samplesref = inlink->cur_buf;
    inlink->cur_buf = NULL;

    return 0;
}

AVFilter avfilter_asink_abuffersink = {
    .name      = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .init      = init,
    .priv_size = sizeof(ABufferSinkContext),
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name           = "default",
                                    .type           = AVMEDIA_TYPE_AUDIO,
                                    .filter_samples = filter_samples,
                                    .min_perms      = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};

