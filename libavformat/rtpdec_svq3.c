/*
 * Sorenson-3 (SVQ3/SV3V) payload for RTP
 * Copyright (c) 2010 Ronald S. Bultje
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
 * @brief RTP support for the SV3V (SVQ3) payload
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * @see http://wiki.multimedia.cx/index.php?title=Sorenson_Video_3#Packetization
 */

#include <string.h>
#include "libavutil/intreadwrite.h"
#include "avio_internal.h"
#include "internal.h"
#include "rtp.h"
#include "rtpdec.h"
#include "rtpdec_formats.h"

struct PayloadContext {
    AVIOContext *pktbuf;
    int64_t        timestamp;
};

/** return 0 on packet, <0 on partial packet or error... */
static int svq3_parse_packet (AVFormatContext *s, PayloadContext *sv,
                              AVStream *st, AVPacket *pkt,
                              uint32_t *timestamp,
                              const uint8_t *buf, int len, uint16_t seq,
                              int flags)
{
    int config_packet, start_packet, end_packet;

    if (len < 2)
        return AVERROR_INVALIDDATA;

    config_packet = buf[0] & 0x40;
    start_packet  = buf[0] & 0x20;
    end_packet    = buf[0] & 0x10;
    buf += 2;     // ignore buf[1]
    len -= 2;

    if (config_packet) {

        av_freep(&st->codecpar->extradata);
        st->codecpar->extradata_size = 0;

        if (len < 2 || ff_alloc_extradata(st->codecpar, len + 8))
            return AVERROR_INVALIDDATA;

        memcpy(st->codecpar->extradata, "SEQH", 4);
        AV_WB32(st->codecpar->extradata + 4, len);
        memcpy(st->codecpar->extradata + 8, buf, len);

        /* We set codec_id to AV_CODEC_ID_NONE initially to
         * delay decoder initialization since extradata is
         * carried within the RTP stream, not SDP. Here,
         * by setting codec_id to AV_CODEC_ID_SVQ3, we are signalling
         * to the decoder that it is OK to initialize. */
        st->codecpar->codec_id = AV_CODEC_ID_SVQ3;

        return AVERROR(EAGAIN);
    }

    if (start_packet) {
        int res;

        ffio_free_dyn_buf(&sv->pktbuf);
        if ((res = avio_open_dyn_buf(&sv->pktbuf)) < 0)
            return res;
        sv->timestamp   = *timestamp;
    }

    if (!sv->pktbuf)
        return AVERROR_INVALIDDATA;

    avio_write(sv->pktbuf, buf, len);

    if (end_packet) {
        int ret = ff_rtp_finalize_packet(pkt, &sv->pktbuf, st->index);
        if (ret < 0)
            return ret;

        *timestamp        = sv->timestamp;
        return 0;
    }

    return AVERROR(EAGAIN);
}

static void svq3_close_context(PayloadContext *sv)
{
    ffio_free_dyn_buf(&sv->pktbuf);
}

const RTPDynamicProtocolHandler ff_svq3_dynamic_handler = {
    .enc_name         = "X-SV3V-ES",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_NONE,      // see if (config_packet) above
    .priv_data_size   = sizeof(PayloadContext),
    .close            = svq3_close_context,
    .parse_packet     = svq3_parse_packet,
};
