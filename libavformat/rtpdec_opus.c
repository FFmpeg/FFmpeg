/*
 * RTP Depacketization of Opus, RFC 7587
 * Copyright (c) 2025 Jonathan Baudanza <jon@jonb.org>
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

#include "libavcodec/bytestream.h"
#include "libavutil/mem.h"
#include "rtpdec_formats.h"
#include "internal.h"

static int opus_duration(const uint8_t *src, int size)
{
    unsigned nb_frames  = 1;
    unsigned toc        = src[0];
    unsigned toc_config = toc >> 3;
    unsigned toc_count  = toc & 3;
    unsigned frame_size = toc_config < 12 ? FFMAX(480, 960 * (toc_config & 3)) :
                          toc_config < 16 ? 480 << (toc_config & 1) :
                                            120 << (toc_config & 3);
    if (toc_count == 3) {
        if (size<2)
            return AVERROR_INVALIDDATA;
        nb_frames = src[1] & 0x3F;
    } else if (toc_count) {
        nb_frames = 2;
    }

    return frame_size * nb_frames;
}

static int opus_write_extradata(AVCodecParameters *codecpar)
{
    uint8_t *bs;
    int ret;

    /* This function writes an extradata with a channel mapping family of 0.
     * This mapping family only supports mono and stereo layouts. And RFC7587
     * specifies that the number of channels in the SDP must be 2.
     */
    if (codecpar->ch_layout.nb_channels > 2) {
        return AVERROR_INVALIDDATA;
    }

    ret = ff_alloc_extradata(codecpar, 19);
    if (ret < 0)
        return ret;

    bs = (uint8_t *)codecpar->extradata;

    /* Opus magic */
    bytestream_put_buffer(&bs, "OpusHead", 8);
    /* Version */
    bytestream_put_byte  (&bs, 0x1);
    /* Channel count */
    bytestream_put_byte  (&bs, codecpar->ch_layout.nb_channels);
    /* Pre skip */
    bytestream_put_le16  (&bs, 0);
    /* Input sample rate */
    bytestream_put_le32  (&bs, 48000);
    /* Output gain */
    bytestream_put_le16  (&bs, 0x0);
    /* Mapping family */
    bytestream_put_byte  (&bs, 0x0);

    return 0;
}

static int opus_init(AVFormatContext *s, int st_index, PayloadContext *priv_data)
{
    return opus_write_extradata(s->streams[st_index]->codecpar);
}

static int opus_parse_packet(AVFormatContext *ctx, PayloadContext *data,
                            AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                            const uint8_t *buf, int len, uint16_t seq,
                            int flags)
{
    int rv;
    int duration;

    if ((rv = av_new_packet(pkt, len)) < 0)
        return rv;

    memcpy(pkt->data, buf, len);
    pkt->stream_index = st->index;

    duration = opus_duration(buf, len);
    if (duration != AVERROR_INVALIDDATA) {
        pkt->duration = duration;
    }

    return 0;
}

const RTPDynamicProtocolHandler ff_opus_dynamic_handler = {
    .enc_name     = "opus",
    .codec_type   = AVMEDIA_TYPE_AUDIO,
    .codec_id     = AV_CODEC_ID_OPUS,
    .parse_packet = opus_parse_packet,
    .init         = opus_init,
};
