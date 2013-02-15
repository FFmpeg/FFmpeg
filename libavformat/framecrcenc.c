/*
 * frame CRC encoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard
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

#include "libavutil/adler32.h"
#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"

static int framecrc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint32_t crc = av_adler32_update(0, pkt->data, pkt->size);
    char buf[256];

    snprintf(buf, sizeof(buf), "%d, %10"PRId64", %10"PRId64", %8d, %8d, 0x%08x",
             pkt->stream_index, pkt->dts, pkt->pts, pkt->duration, pkt->size, crc);
    if (pkt->flags != AV_PKT_FLAG_KEY)
        av_strlcatf(buf, sizeof(buf), ", F=0x%0X", pkt->flags);
    if (pkt->side_data_elems) {
        int i;
        av_strlcatf(buf, sizeof(buf), ", S=%d", pkt->side_data_elems);

        for (i=0; i<pkt->side_data_elems; i++) {
            uint32_t side_data_crc = av_adler32_update(0,
                                                    pkt->side_data[i].data,
                                                    pkt->side_data[i].size);
            av_strlcatf(buf, sizeof(buf), ", %8d, 0x%08x", pkt->side_data[i].size, side_data_crc);
        }
    }
    av_strlcatf(buf, sizeof(buf), "\n");
    avio_write(s->pb, buf, strlen(buf));
    avio_flush(s->pb);
    return 0;
}

AVOutputFormat ff_framecrc_muxer = {
    .name              = "framecrc",
    .long_name         = NULL_IF_CONFIG_SMALL("framecrc testing"),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = ff_framehash_write_header,
    .write_packet      = framecrc_write_packet,
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
};
