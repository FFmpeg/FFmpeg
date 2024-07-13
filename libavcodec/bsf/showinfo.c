/*
 * Copyright (c) 2024 Anton Khirnov <anton@khirnov.net>
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

#include <inttypes.h>

#include "bsf.h"
#include "bsf_internal.h"

#include "libavutil/adler32.h"
#include "libavutil/log.h"
#include "libavutil/timestamp.h"

typedef struct ShowinfoContext {
    uint64_t nb_packets;
} ShowinfoContext;

static int showinfo_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    ShowinfoContext *priv = ctx->priv_data;
    uint32_t crc;
    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    crc = av_adler32_update(0, pkt->data, pkt->size);
    av_log(ctx, AV_LOG_INFO,
           "n:%7"PRIu64" "
           "size:%7d "
           "pts:%s pt:%s "
           "dts:%s dt:%s "
           "ds:%"PRId64" d:%s "
           "adler32:0x%08"PRIx32
           "\n",
           priv->nb_packets, pkt->size,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ctx->time_base_in),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ctx->time_base_in),
           pkt->duration, av_ts2timestr(pkt->duration, &ctx->time_base_in), crc);

    priv->nb_packets++;

    return 0;
}

const FFBitStreamFilter ff_showinfo_bsf = {
    .p.name         = "showinfo",
    .filter         = showinfo_filter,
    .priv_data_size = sizeof(ShowinfoContext),
};
