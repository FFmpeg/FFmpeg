/*
 * RTP packetization for Xiph audio and video
 * Copyright (c) 2010 Josh Allmann
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

#include "libavutil/avassert.h"
#include "avformat.h"
#include "rtpenc.h"

/**
 * Packetize Xiph frames into RTP according to
 * RFC 5215 (Vorbis) and the Theora RFC draft.
 * (http://svn.xiph.org/trunk/theora/doc/draft-ietf-avt-rtp-theora-00.txt)
 */
void ff_rtp_send_xiph(AVFormatContext *s1, const uint8_t *buff, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int max_pkt_size, xdt, frag;
    uint8_t *q;

    max_pkt_size = s->max_payload_size;

    // set xiph data type
    switch (*buff) {
    case 0x01:   // vorbis id
    case 0x05:   // vorbis setup
    case 0x80:   // theora header
    case 0x82:   // theora tables
        xdt = 1; // packed config payload
        break;
    case 0x03:   // vorbis comments
    case 0x81:   // theora comments
        xdt = 2; // comment payload
        break;
    default:
        xdt = 0; // raw data payload
        break;
    }

    // Set ident.
    // Probably need a non-fixed way of generating
    // this, but it has to be done in SDP and passed in from there.
    q = s->buf;
    *q++ = (RTP_XIPH_IDENT >> 16) & 0xff;
    *q++ = (RTP_XIPH_IDENT >>  8) & 0xff;
    *q++ = (RTP_XIPH_IDENT      ) & 0xff;

    // set fragment
    // 0 - whole frame (possibly multiple frames)
    // 1 - first fragment
    // 2 - fragment continuation
    // 3 - last fragmement
    frag = size <= max_pkt_size ? 0 : 1;

    if (!frag && !xdt) { // do we have a whole frame of raw data?
        uint8_t *end_ptr = s->buf + 6 + max_pkt_size; // what we're allowed to write
        uint8_t *ptr     = s->buf_ptr + 2 + size; // what we're going to write
        int remaining    = end_ptr - ptr;

        av_assert1(s->num_frames <= s->max_frames_per_packet);
        if ((s->num_frames > 0 && remaining < 0) ||
            s->num_frames == s->max_frames_per_packet) {
            // send previous packets now; no room for new data
            ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 0);
            s->num_frames = 0;
        }

        // buffer current frame to send later
        if (0 == s->num_frames) s->timestamp = s->cur_timestamp;
        s->num_frames++;

        // Set packet header. Normally, this is OR'd with frag and xdt,
        // but those are zero, so omitted here
        *q++ = s->num_frames;

        if (s->num_frames > 1) q = s->buf_ptr; // jump ahead if needed
        *q++ = (size >> 8) & 0xff;
        *q++ = size & 0xff;
        memcpy(q, buff, size);
        q += size;
        s->buf_ptr = q;

        return;
    } else if (s->num_frames) {
        // immediately send buffered frames if buffer is not raw data,
        // or if current frame is fragmented.
        ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 0);
    }

    s->timestamp = s->cur_timestamp;
    s->num_frames = 0;
    s->buf_ptr = q;
    while (size > 0) {
        int len = (!frag || frag == 3) ? size : max_pkt_size;
        q = s->buf_ptr;

        // set packet headers
        *q++ = (frag << 6) | (xdt << 4); // num_frames = 0
        *q++ = (len >> 8) & 0xff;
        *q++ = len & 0xff;
        // set packet body
        memcpy(q, buff, len);
        q += len;
        buff += len;
        size -= len;

        ff_rtp_send_data(s1, s->buf, q - s->buf, 0);

        frag = size <= max_pkt_size ? 3 : 2;
    }
}
