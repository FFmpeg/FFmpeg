/*
 * Copyright (C) 2008  Ramiro Polla
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

#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"

#define HEADER_SIZE         24

/*
 * Header structure:
 *  uint16_t    ss;     // struct size
 *  uint16_t    width;  // frame width
 *  uint16_t    height; // frame height
 *  uint16_t    ff;     // keyframe + some other info(???)
 *  uint32_t    size;   // size of data
 *  uint32_t    fourcc; // ML20
 *  uint32_t    u3;     // ?
 *  uint32_t    ts;     // time
 */

static int msnwc_tcp_probe(AVProbeData *p)
{
    int i;

    for(i = 0 ; i + HEADER_SIZE <= p->buf_size ; i++) {
        uint16_t width, height;
        uint32_t fourcc;
        const uint8_t *bytestream = p->buf+i;

        if(bytestream_get_le16(&bytestream) != HEADER_SIZE)
            continue;
        width  = bytestream_get_le16(&bytestream);
        height = bytestream_get_le16(&bytestream);
        if(!(width==320 && height==240) && !(width==160 && height==120))
            continue;
        bytestream += 2; // keyframe
        bytestream += 4; // size
        fourcc = bytestream_get_le32(&bytestream);
        if(fourcc != MKTAG('M', 'L', '2', '0'))
            continue;

        if(i) {
            if(i < 14)  /* starts with SwitchBoard connection info */
                return AVPROBE_SCORE_MAX / 2;
            else        /* starts in the middle of stream */
                return AVPROBE_SCORE_MAX / 3;
        } else {
            return AVPROBE_SCORE_MAX;
        }
    }

    return -1;
}

static int msnwc_tcp_read_header(AVFormatContext *ctx)
{
    AVIOContext *pb = ctx->pb;
    AVCodecContext *codec;
    AVStream *st;

    st = avformat_new_stream(ctx, NULL);
    if(!st)
        return AVERROR(ENOMEM);

    codec = st->codec;
    codec->codec_type = AVMEDIA_TYPE_VIDEO;
    codec->codec_id = AV_CODEC_ID_MIMIC;
    codec->codec_tag = MKTAG('M', 'L', '2', '0');

    avpriv_set_pts_info(st, 32, 1, 1000);

    /* Some files start with "connected\r\n\r\n".
     * So skip until we find the first byte of struct size */
    while(avio_r8(pb) != HEADER_SIZE && !pb->eof_reached);

    if(pb->eof_reached) {
        av_log(ctx, AV_LOG_ERROR, "Could not find valid start.");
        return -1;
    }

    return 0;
}

static int msnwc_tcp_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    AVIOContext *pb = ctx->pb;
    uint16_t keyframe;
    uint32_t size, timestamp;

    avio_skip(pb, 1); /* one byte has been read ahead */
    avio_skip(pb, 2);
    avio_skip(pb, 2);
    keyframe = avio_rl16(pb);
    size = avio_rl32(pb);
    avio_skip(pb, 4);
    avio_skip(pb, 4);
    timestamp = avio_rl32(pb);

    if(!size || av_get_packet(pb, pkt, size) != size)
        return -1;

    avio_skip(pb, 1); /* Read ahead one byte of struct size like read_header */

    pkt->pts = timestamp;
    pkt->dts = timestamp;
    pkt->stream_index = 0;

    /* Some aMsn generated videos (or was it Mercury Messenger?) don't set
     * this bit and rely on the codec to get keyframe information */
    if(keyframe&1)
        pkt->flags |= AV_PKT_FLAG_KEY;

    return HEADER_SIZE + size;
}

AVInputFormat ff_msnwc_tcp_demuxer = {
    .name           = "msnwctcp",
    .long_name      = NULL_IF_CONFIG_SMALL("MSN TCP Webcam stream"),
    .read_probe     = msnwc_tcp_probe,
    .read_header    = msnwc_tcp_read_header,
    .read_packet    = msnwc_tcp_read_packet,
};
