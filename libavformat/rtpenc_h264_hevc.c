/*
 * RTP packetization for H.264 (RFC3984)
 * RTP packetizer for HEVC/H.265 payload format (draft version 6)
 * Copyright (c) 2008 Luca Abeni
 * Copyright (c) 2014 Thomas Volkert <thomas@homer-conferencing.com>
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

/**
 * @file
 * @brief H.264/HEVC packetization
 * @author Luca Abeni <lucabe72@email.it>
 */

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "avc.h"
#include "rtpenc.h"

static void flush_buffered(AVFormatContext *s1, int last)
{
    RTPMuxContext *s = s1->priv_data;
    if (s->buf_ptr != s->buf) {
        // If we're only sending one single NAL unit, send it as such, skip
        // the STAP-A/AP framing
        if (s->buffered_nals == 1) {
            enum AVCodecID codec = s1->streams[0]->codecpar->codec_id;
            if (codec == AV_CODEC_ID_H264)
                ff_rtp_send_data(s1, s->buf + 3, s->buf_ptr - s->buf - 3, last);
            else
                ff_rtp_send_data(s1, s->buf + 4, s->buf_ptr - s->buf - 4, last);
        } else
            ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, last);
    }
    s->buf_ptr = s->buf;
    s->buffered_nals = 0;
}

static void nal_send(AVFormatContext *s1, const uint8_t *buf, int size, int last)
{
    RTPMuxContext *s = s1->priv_data;
    enum AVCodecID codec = s1->streams[0]->codecpar->codec_id;

    av_log(s1, AV_LOG_DEBUG, "Sending NAL %x of len %d M=%d\n", buf[0] & 0x1F, size, last);
    if (size <= s->max_payload_size) {
        int buffered_size = s->buf_ptr - s->buf;
        int header_size;
        int skip_aggregate = 0;

        if (codec == AV_CODEC_ID_H264) {
            header_size = 1;
            skip_aggregate = s->flags & FF_RTP_FLAG_H264_MODE0;
        } else {
            header_size = 2;
        }

        // Flush buffered NAL units if the current unit doesn't fit
        if (buffered_size + 2 + size > s->max_payload_size) {
            flush_buffered(s1, 0);
            buffered_size = 0;
        }
        // If we aren't using mode 0, and the NAL unit fits including the
        // framing (2 bytes length, plus 1/2 bytes for the STAP-A/AP marker),
        // write the unit to the buffer as a STAP-A/AP packet, otherwise flush
        // and send as single NAL.
        if (buffered_size + 2 + header_size + size <= s->max_payload_size &&
            !skip_aggregate) {
            if (buffered_size == 0) {
                if (codec == AV_CODEC_ID_H264) {
                    *s->buf_ptr++ = 24;
                } else {
                    *s->buf_ptr++ = 48 << 1;
                    *s->buf_ptr++ = 1;
                }
            }
            AV_WB16(s->buf_ptr, size);
            s->buf_ptr += 2;
            memcpy(s->buf_ptr, buf, size);
            s->buf_ptr += size;
            s->buffered_nals++;
        } else {
            flush_buffered(s1, 0);
            ff_rtp_send_data(s1, buf, size, last);
        }
    } else {
        int flag_byte, header_size;
        flush_buffered(s1, 0);
        if (codec == AV_CODEC_ID_H264 && (s->flags & FF_RTP_FLAG_H264_MODE0)) {
            av_log(s1, AV_LOG_ERROR,
                   "NAL size %d > %d, try -slice-max-size %d\n", size,
                   s->max_payload_size, s->max_payload_size);
            return;
        }
        av_log(s1, AV_LOG_DEBUG, "NAL size %d > %d\n", size, s->max_payload_size);
        if (codec == AV_CODEC_ID_H264) {
            uint8_t type = buf[0] & 0x1F;
            uint8_t nri = buf[0] & 0x60;

            s->buf[0] = 28;        /* FU Indicator; Type = 28 ---> FU-A */
            s->buf[0] |= nri;
            s->buf[1] = type;
            s->buf[1] |= 1 << 7;
            buf  += 1;
            size -= 1;

            flag_byte   = 1;
            header_size = 2;
        } else {
            uint8_t nal_type = (buf[0] >> 1) & 0x3F;
            /*
             * create the HEVC payload header and transmit the buffer as fragmentation units (FU)
             *
             *    0                   1
             *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
             *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             *   |F|   Type    |  LayerId  | TID |
             *   +-------------+-----------------+
             *
             *      F       = 0
             *      Type    = 49 (fragmentation unit (FU))
             *      LayerId = 0
             *      TID     = 1
             */
            s->buf[0] = 49 << 1;
            s->buf[1] = 1;

            /*
             *     create the FU header
             *
             *     0 1 2 3 4 5 6 7
             *    +-+-+-+-+-+-+-+-+
             *    |S|E|  FuType   |
             *    +---------------+
             *
             *       S       = variable
             *       E       = variable
             *       FuType  = NAL unit type
             */
            s->buf[2]  = nal_type;
            /* set the S bit: mark as start fragment */
            s->buf[2] |= 1 << 7;

            /* pass the original NAL header */
            buf  += 2;
            size -= 2;

            flag_byte   = 2;
            header_size = 3;
        }

        while (size + header_size > s->max_payload_size) {
            memcpy(&s->buf[header_size], buf, s->max_payload_size - header_size);
            ff_rtp_send_data(s1, s->buf, s->max_payload_size, 0);
            buf  += s->max_payload_size - header_size;
            size -= s->max_payload_size - header_size;
            s->buf[flag_byte] &= ~(1 << 7);
        }
        s->buf[flag_byte] |= 1 << 6;
        memcpy(&s->buf[header_size], buf, size);
        ff_rtp_send_data(s1, s->buf, size + header_size, last);
    }
}

void ff_rtp_send_h264_hevc(AVFormatContext *s1, const uint8_t *buf1, int size)
{
    const uint8_t *r, *end = buf1 + size;
    RTPMuxContext *s = s1->priv_data;

    s->timestamp = s->cur_timestamp;
    s->buf_ptr   = s->buf;
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
    flush_buffered(s1, 1);
}
