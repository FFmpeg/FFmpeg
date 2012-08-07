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
                              const uint8_t *buf, int len, int flags)
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

        av_freep(&st->codec->extradata);
        st->codec->extradata_size = 0;

        if (len < 2 || !(st->codec->extradata =
                         av_malloc(len + 8 + FF_INPUT_BUFFER_PADDING_SIZE)))
            return AVERROR_INVALIDDATA;

        st->codec->extradata_size = len + 8;
        memcpy(st->codec->extradata, "SEQH", 4);
        AV_WB32(st->codec->extradata + 4, len);
        memcpy(st->codec->extradata + 8, buf, len);

        /* We set codec_id to AV_CODEC_ID_NONE initially to
         * delay decoder initialization since extradata is
         * carried within the RTP stream, not SDP. Here,
         * by setting codec_id to AV_CODEC_ID_SVQ3, we are signalling
         * to the decoder that it is OK to initialize. */
        st->codec->codec_id = AV_CODEC_ID_SVQ3;

        return AVERROR(EAGAIN);
    }

    if (start_packet) {
        int res;

        if (sv->pktbuf) {
            uint8_t *tmp;
            avio_close_dyn_buf(sv->pktbuf, &tmp);
            av_free(tmp);
        }
        if ((res = avio_open_dyn_buf(&sv->pktbuf)) < 0)
            return res;
        sv->timestamp   = *timestamp;
    }

    if (!sv->pktbuf)
        return AVERROR_INVALIDDATA;

    avio_write(sv->pktbuf, buf, len);

    if (end_packet) {
        av_init_packet(pkt);
        pkt->stream_index = st->index;
        *timestamp        = sv->timestamp;
        pkt->size         = avio_close_dyn_buf(sv->pktbuf, &pkt->data);
        pkt->destruct     = av_destruct_packet;
        sv->pktbuf        = NULL;
        return 0;
    }

    return AVERROR(EAGAIN);
}

static PayloadContext *svq3_extradata_new(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void svq3_extradata_free(PayloadContext *sv)
{
    if (sv->pktbuf) {
        uint8_t *buf;
        avio_close_dyn_buf(sv->pktbuf, &buf);
        av_free(buf);
    }
    av_free(sv);
}

RTPDynamicProtocolHandler ff_svq3_dynamic_handler = {
    .enc_name         = "X-SV3V-ES",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_NONE,      // see if (config_packet) above
    .alloc            = svq3_extradata_new,
    .free             = svq3_extradata_free,
    .parse_packet     = svq3_parse_packet,
};
