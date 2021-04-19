/*
 * MicroDVD subtitle muxer
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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
#include "avformat.h"
#include "internal.h"

static int microdvd_write_header(struct AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    AVRational framerate = s->streams[0]->avg_frame_rate;

    if (s->nb_streams != 1 || par->codec_id != AV_CODEC_ID_MICRODVD) {
        av_log(s, AV_LOG_ERROR, "Exactly one MicroDVD stream is needed.\n");
        return -1;
    }

    if (par->extradata && par->extradata_size > 0) {
        avio_write(s->pb, "{DEFAULT}{}", 11);
        avio_write(s->pb, par->extradata, par->extradata_size);
        avio_w8(s->pb, '\n');
    }

    avpriv_set_pts_info(s->streams[0], 64, framerate.num, framerate.den);
    return 0;
}

static int microdvd_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    avio_printf(avf->pb, "{%"PRId64"}", pkt->pts);
    if (pkt->duration < 0)
        avio_write(avf->pb, "{}", 2);
    else
        avio_printf(avf->pb, "{%"PRId64"}", pkt->pts + pkt->duration);
    avio_write(avf->pb, pkt->data, pkt->size);
    avio_w8(avf->pb, '\n');
    return 0;
}

const AVOutputFormat ff_microdvd_muxer = {
    .name           = "microdvd",
    .long_name      = NULL_IF_CONFIG_SMALL("MicroDVD subtitle format"),
    .mime_type      = "text/x-microdvd",
    .extensions     = "sub",
    .write_header   = microdvd_write_header,
    .write_packet   = microdvd_write_packet,
    .flags          = AVFMT_NOTIMESTAMPS,
    .subtitle_codec = AV_CODEC_ID_MICRODVD,
};
