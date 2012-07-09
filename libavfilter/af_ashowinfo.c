/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * filter for showing textual audio frame information
 */

#include "libavutil/adler32.h"
#include "libavutil/audioconvert.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "avfilter.h"

typedef struct {
    unsigned int frame;
} ShowInfoContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ShowInfoContext *showinfo = ctx->priv;
    showinfo->frame = 0;
    return 0;
}

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *samplesref)
{
    AVFilterContext *ctx = inlink->dst;
    ShowInfoContext *showinfo = ctx->priv;
    uint32_t plane_checksum[8] = {0}, checksum = 0;
    char chlayout_str[128];
    int plane;
    int linesize =
        samplesref->audio->nb_samples *
        av_get_bytes_per_sample(samplesref->format);
    if (!av_sample_fmt_is_planar(samplesref->format))
        linesize *= av_get_channel_layout_nb_channels(samplesref->audio->channel_layout);

    for (plane = 0; samplesref->data[plane] && plane < 8; plane++) {
        uint8_t *data = samplesref->data[plane];

        plane_checksum[plane] = av_adler32_update(plane_checksum[plane],
                                                  data, linesize);
        checksum = av_adler32_update(checksum, data, linesize);
    }

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str), -1,
                                 samplesref->audio->channel_layout);

    av_log(ctx, AV_LOG_INFO,
           "n:%d pts:%s pts_time:%s pos:%"PRId64" "
           "fmt:%s chlayout:%s nb_samples:%d rate:%d "
           "checksum:%08X plane_checksum[%08X",
           showinfo->frame,
           av_ts2str(samplesref->pts), av_ts2timestr(samplesref->pts, &inlink->time_base),
           samplesref->pos,
           av_get_sample_fmt_name(samplesref->format),
           chlayout_str,
           samplesref->audio->nb_samples,
           samplesref->audio->sample_rate,
           checksum,
           plane_checksum[0]);

    for (plane = 1; samplesref->data[plane] && plane < 8; plane++)
        av_log(ctx, AV_LOG_INFO, " %08X", plane_checksum[plane]);
    av_log(ctx, AV_LOG_INFO, "]\n");

    showinfo->frame++;
    return ff_filter_samples(inlink->dst->outputs[0], samplesref);
}

AVFilter avfilter_af_ashowinfo = {
    .name        = "ashowinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each audio frame."),

    .priv_size = sizeof(ShowInfoContext),
    .init      = init,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_AUDIO,
                                    .get_audio_buffer = ff_null_get_audio_buffer,
                                    .filter_samples   = filter_samples,
                                    .min_perms        = AV_PERM_READ, },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_AUDIO },
                                  { .name = NULL}},
};
