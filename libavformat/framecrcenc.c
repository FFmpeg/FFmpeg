/*
 * frame CRC encoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>

#include "libavutil/adler32.h"
#include "avformat.h"
#include "internal.h"

static int framecrc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint32_t crc = av_adler32_update(0, pkt->data, pkt->size);
    char buf[256];

    snprintf(buf, sizeof(buf), "%d, %10"PRId64", %10"PRId64", %8"PRId64", %8d, 0x%08"PRIx32"\n",
             pkt->stream_index, pkt->dts, pkt->pts, pkt->duration, pkt->size, crc);
    avio_write(s->pb, buf, strlen(buf));
    return 0;
}

AVOutputFormat ff_framecrc_muxer = {
    .name              = "framecrc",
    .long_name         = NULL_IF_CONFIG_SMALL("framecrc testing"),
    .extensions        = "",
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = ff_framehash_write_header,
    .write_packet      = framecrc_write_packet,
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT |
                         AVFMT_TS_NEGATIVE,
};
