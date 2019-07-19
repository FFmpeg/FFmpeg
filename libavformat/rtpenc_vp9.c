/*
 * RTP packetizer for VP9 payload format (draft version 06) - experimental
 * Copyright (c) 2016 Thomas Volkert <thomas@netzeal.de>
 * Copyright (c) 2019 Sohonet <dev@sohonet.com>
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

#include <stdbool.h>

#include "libavcodec/get_bits.h"
#include "rtpenc.h"

#define RTP_VP9_DESC_REQUIRED_SIZE 1
#define RTP_VP9_SS_SIZE 8
#define RTP_VP9_MAX_HEADER_SIZE 9

#define P_BIT 0x40
#define B_BIT 0x08
#define E_BIT 0x04
#define V_BIT 0x02

/**
 * Parse the uncompressed header until we can determine whether the current
 * frame is a keyframe.
 * Based on version 0.6 of the VP9 Bitstream & Process Specification.
 */
static bool is_keyframe(AVFormatContext *ctx, const uint8_t *buf, int size)
{
    GetBitContext gb;
    int marker, profile, show_existing_frame;

    if (init_get_bits8(&gb, buf, size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing VP9 frame\n");
        return false;
    }

    // Frame marker.
    marker = get_bits(&gb, 2);
    if (marker != 0x2) {
        av_log(ctx, AV_LOG_ERROR, "VP9 frame marker is invalid: 0x%x != 0x2\n", marker);
        return false;
    }

    // Profile.
    profile  = get_bits1(&gb);
    profile |= get_bits1(&gb) << 1;
    if (profile > 2 && get_bits1(&gb)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported VP9 profile\n");
        return false;
    }

    // Show existing frame.
    show_existing_frame = get_bits1(&gb);
    if (show_existing_frame) {
        return false;
    }

    // Frame type: key frame(0) or inter-frame(1).
    return !get_bits1(&gb);
}

/**
 * Scalability structure:
 *
 *      +-+-+-+-+-+-+-+-+
 * V:   | N_S |Y|G|-|-|-|
 *      +-+-+-+-+-+-+-+-+              -\
 * Y:   |     WIDTH     | (OPTIONAL)    |
 *      +               +               |
 *      |               | (OPTIONAL)    |
 *      +-+-+-+-+-+-+-+-+               | - N_S + 1 times
 *      |     HEIGHT    | (OPTIONAL)    |
 *      +               +               |
 *      |               | (OPTIONAL)    |
 *      +-+-+-+-+-+-+-+-+              -/            -\
 * G:   |      N_G      | (OPTIONAL)
 *      +-+-+-+-+-+-+-+-+                            -\
 * N_G: | TID |U| R |-|-| (OPTIONAL)                  |
 *      +-+-+-+-+-+-+-+-+              -\             | - N_G times
 *      |    P_DIFF     | (OPTIONAL)    | - R times   |
 *      +-+-+-+-+-+-+-+-+              -/            -/
 */
static size_t write_rtp_vp9_ss(AVFormatContext *ctx, uint8_t *buf)
{
    int width  = ctx->streams[0]->codecpar->width;
    int height = ctx->streams[0]->codecpar->height;

    // 1 Layer, resolution present, PG description present.
    // N_S=0, Y=1, G=1.
    *buf++ = 0x18;

    *buf++ = width >> 8;
    *buf++ = width & 0x0FF;
    *buf++ = height >> 8;
    *buf++ = height & 0x0FF;

    // 1 picture group.
    // TID=0, U=0, R=1, P_DIFF=1.
    *buf++ = 0x01;
    *buf++ = 0x04;
    *buf++ = 0x01;

    return RTP_VP9_SS_SIZE;
}

/**
 * Payload descriptor, non-flexible mode.
 *
 *       0 1 2 3 4 5 6 7
 *      +-+-+-+-+-+-+-+-+
 *      |I|P|L|F|B|E|V|-| (REQUIRED)
 *      +-+-+-+-+-+-+-+-+
 * I:   |M| PICTURE ID  | (RECOMMENDED)
 *      +-+-+-+-+-+-+-+-+
 * M:   | EXTENDED PID  | (RECOMMENDED)
 *      +-+-+-+-+-+-+-+-+
 * L:   | TID |U| SID |D| (CONDITIONALLY RECOMMENDED)
 *      +-+-+-+-+-+-+-+-+
 *      |   TL0PICIDX   | (CONDITIONALLY REQUIRED)
 *      +-+-+-+-+-+-+-+-+
 * V:   | SS            |
 *      | ..            |
 *      +-+-+-+-+-+-+-+-+
 */
static size_t write_rtp_vp9_headers(AVFormatContext *ctx, uint8_t *buf,
                                    bool first, bool last, bool keyframe)
{
    uint8_t descriptor = 0x0;
    size_t length = RTP_VP9_DESC_REQUIRED_SIZE;
    bool include_ss = first && keyframe;

    /**
     *  0 1 2 3 4 5 6 7
     * +-+-+-+-+-+-+-+-+
     * |I|P|L|F|B|E|V|-| (REQUIRED)
     *  ^ ^ ^ ^ ^ ^ ^ ^
     *  | | | | | | | |
     *  | | | | | | | -: Reserved. Must be zero.
     *  | | | | | | V: Scalability structure (SS) present.
     *  | | | | | E: End of a frame.
     *  | | | | B: Start of a frame.
     *  | | | F: Flexible mode (hardwired to 0).
     *  | | L: Layer indices present (hardwired to 0).
     *  | P: Inter-picture predicted frame (!keyframe).
     *  I: Picture ID present (hardwired to 0).
     */
    if (!keyframe)  descriptor |= P_BIT;
    if (first)      descriptor |= B_BIT;
    if (last)       descriptor |= E_BIT;
    if (include_ss) descriptor |= V_BIT;

    *buf++ = descriptor;

    if (include_ss) {
        length += write_rtp_vp9_ss(ctx, buf);
    }

    return length;
}

void ff_rtp_send_vp9(AVFormatContext *ctx, const uint8_t *buf, int size)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    int len = 0,
        hdr_len = 0;
    bool first = true,
         keyframe = is_keyframe(ctx, buf, size),
         include_ss = false;

    rtp_ctx->timestamp = rtp_ctx->cur_timestamp;

    while (size > 0) {
        rtp_ctx->buf_ptr = rtp_ctx->buf;

        include_ss = first && keyframe;

        hdr_len = include_ss ?
            (RTP_VP9_DESC_REQUIRED_SIZE + RTP_VP9_SS_SIZE) :
            RTP_VP9_DESC_REQUIRED_SIZE;
        len = FFMIN(size, rtp_ctx->max_payload_size - RTP_VP9_MAX_HEADER_SIZE);

        rtp_ctx->buf_ptr += write_rtp_vp9_headers(ctx, rtp_ctx->buf, first, size == len, keyframe);

        memcpy(rtp_ctx->buf_ptr, buf, len);
        ff_rtp_send_data(ctx, rtp_ctx->buf, len + hdr_len, size == len);

        size -= len;
        buf  += len;

        first = false;
    }
}
