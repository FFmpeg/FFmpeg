/*
 * Common code for the RTP depacketization of MPEG-1/2 formats.
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

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "rtpdec_formats.h"

static int mpeg_parse_packet(AVFormatContext *ctx, PayloadContext *data,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags)
{
    unsigned int h;
    if (len <= 4)
        return AVERROR_INVALIDDATA;
    h    = AV_RB32(buf);
    buf += 4;
    len -= 4;
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && h & (1 << 26)) {
        /* MPEG-2 */
        if (len <= 4)
            return AVERROR_INVALIDDATA;
        buf += 4;
        len -= 4;
    }
    if (av_new_packet(pkt, len) < 0)
        return AVERROR(ENOMEM);
    memcpy(pkt->data, buf, len);
    pkt->stream_index = st->index;
    return 0;
}

const RTPDynamicProtocolHandler ff_mpeg_audio_dynamic_handler = {
    .codec_type        = AVMEDIA_TYPE_AUDIO,
    .codec_id          = AV_CODEC_ID_MP3,
    .need_parsing      = AVSTREAM_PARSE_FULL,
    .parse_packet      = mpeg_parse_packet,
    .static_payload_id = 14,
};

const RTPDynamicProtocolHandler ff_mpeg_video_dynamic_handler = {
    .codec_type        = AVMEDIA_TYPE_VIDEO,
    .codec_id          = AV_CODEC_ID_MPEG2VIDEO,
    .need_parsing      = AVSTREAM_PARSE_FULL,
    .parse_packet      = mpeg_parse_packet,
    .static_payload_id = 32,
};
