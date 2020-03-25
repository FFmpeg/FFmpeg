/*
 * Retime Interleaving functions
 *
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

#include "libavutil/mathematics.h"
#include "avformat.h"
#include "retimeinterleave.h"
#include "internal.h"

void ff_retime_interleave_init(RetimeInterleaveContext *aic, AVRational time_base)
{
    aic->time_base = time_base;
}

int ff_retime_interleave(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush,
                        int (*get_packet)(AVFormatContext *, AVPacket *, AVPacket *, int),
                        int (*compare_ts)(AVFormatContext *, const AVPacket *, const AVPacket *))
{
    int ret;

    if (pkt) {
        AVStream *st = s->streams[pkt->stream_index];
        RetimeInterleaveContext *aic = st->priv_data;
        pkt->duration = av_rescale_q(pkt->duration, st->time_base, aic->time_base);
        // rewrite pts and dts to be decoded time line position
        pkt->pts = pkt->dts = aic->dts;
        aic->dts += pkt->duration;
        if ((ret = ff_interleave_add_packet(s, pkt, compare_ts)) < 0)
            return ret;
    }

    return get_packet(s, out, NULL, flush);
}
