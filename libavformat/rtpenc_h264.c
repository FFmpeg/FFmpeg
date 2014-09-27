/*
 * RTP packetization for H.264 (RFC3984)
 * Copyright (c) 2008 Luca Abeni
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
 * @brief H.264 packetization
 * @author Luca Abeni <lucabe72@email.it>
 */

#include "avformat.h"
#include "avc.h"
#include "rtpenc.h"

static void nal_send(AVFormatContext *s1, const uint8_t *buf, int size, int last)
{
    RTPMuxContext *s = s1->priv_data;

    av_log(s1, AV_LOG_DEBUG, "Sending NAL %x of len %d M=%d\n", buf[0] & 0x1F, size, last);
    if (size <= s->max_payload_size) {
        ff_rtp_send_data(s1, buf, size, last);
    } else {
        uint8_t type = buf[0] & 0x1F;
        uint8_t nri = buf[0] & 0x60;

        if (s->flags & FF_RTP_FLAG_H264_MODE0) {
            av_log(s1, AV_LOG_ERROR,
                   "NAL size %d > %d, try -slice-max-size %d\n", size,
                   s->max_payload_size, s->max_payload_size);
            return;
        }
        av_log(s1, AV_LOG_DEBUG, "NAL size %d > %d\n", size, s->max_payload_size);
        s->buf[0] = 28;        /* FU Indicator; Type = 28 ---> FU-A */
        s->buf[0] |= nri;
        s->buf[1] = type;
        s->buf[1] |= 1 << 7;
        buf += 1;
        size -= 1;
        while (size + 2 > s->max_payload_size) {
            memcpy(&s->buf[2], buf, s->max_payload_size - 2);
            ff_rtp_send_data(s1, s->buf, s->max_payload_size, 0);
            buf += s->max_payload_size - 2;
            size -= s->max_payload_size - 2;
            s->buf[1] &= ~(1 << 7);
        }
        s->buf[1] |= 1 << 6;
        memcpy(&s->buf[2], buf, size);
        ff_rtp_send_data(s1, s->buf, size + 2, last);
    }
}

void ff_rtp_send_h264(AVFormatContext *s1, const uint8_t *buf1, int size)
{
    const uint8_t *r, *end = buf1 + size;
    RTPMuxContext *s = s1->priv_data;

    s->timestamp = s->cur_timestamp;
    if (s->nal_length_size)
        r = ff_avc_mp4_find_startcode(buf1, end, s->nal_length_size) ? buf1 : end;
    else
        r = ff_avc_find_startcode(buf1, end);
    while (r < end) {
        const uint8_t *r1;

        if (s->nal_length_size) {
            r1 = ff_avc_mp4_find_startcode(r, end, s->nal_length_size);
            if (!r1)
                r1 = end;
            r += s->nal_length_size;
        } else {
            while (!*(r++));
            r1 = ff_avc_find_startcode(r, end);
        }
        nal_send(s1, r, r1 - r, r1 == end);
        r = r1;
    }
}
