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

#include <inttypes.h>
#include <stddef.h>

#include "libavutil/adler32.h"
#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/timestamp.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct AShowInfoContext {
    /**
     * Scratch space for individual plane checksums for planar audio
     */
    uint32_t *plane_checksums;
} AShowInfoContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    AShowInfoContext *s = ctx->priv;
    av_freep(&s->plane_checksums);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AShowInfoContext *s  = ctx->priv;
    char chlayout_str[128];
    uint32_t checksum = 0;
    int channels    = inlink->channels;
    int planar      = av_sample_fmt_is_planar(buf->format);
    int block_align = av_get_bytes_per_sample(buf->format) * (planar ? 1 : channels);
    int data_size   = buf->nb_samples * block_align;
    int planes      = planar ? channels : 1;
    int i;
    void *tmp_ptr = av_realloc(s->plane_checksums, channels * sizeof(*s->plane_checksums));

    if (!tmp_ptr)
        return AVERROR(ENOMEM);
    s->plane_checksums = tmp_ptr;

    for (i = 0; i < planes; i++) {
        uint8_t *data = buf->extended_data[i];

        s->plane_checksums[i] = av_adler32_update(0, data, data_size);
        checksum = i ? av_adler32_update(checksum, data, data_size) :
                       s->plane_checksums[0];
    }

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str), -1,
                                 buf->channel_layout);

    av_log(ctx, AV_LOG_INFO,
           "n:%"PRId64" pts:%s pts_time:%s pos:%"PRId64" "
           "fmt:%s channels:%d chlayout:%s rate:%d nb_samples:%d "
           "checksum:%08X ",
           inlink->frame_count,
           av_ts2str(buf->pts), av_ts2timestr(buf->pts, &inlink->time_base),
           av_frame_get_pkt_pos(buf),
           av_get_sample_fmt_name(buf->format), av_frame_get_channels(buf), chlayout_str,
           buf->sample_rate, buf->nb_samples,
           checksum);

    av_log(ctx, AV_LOG_INFO, "plane_checksums: [ ");
    for (i = 0; i < planes; i++)
        av_log(ctx, AV_LOG_INFO, "%08X ", s->plane_checksums[i]);
    av_log(ctx, AV_LOG_INFO, "]\n");

    return ff_filter_frame(inlink->dst->outputs[0], buf);
}

static const AVFilterPad inputs[] = {
    {
        .name       = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL },
};

AVFilter avfilter_af_ashowinfo = {
    .name        = "ashowinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each audio frame."),
    .priv_size   = sizeof(AShowInfoContext),
    .uninit      = uninit,
    .inputs      = inputs,
    .outputs     = outputs,
};
