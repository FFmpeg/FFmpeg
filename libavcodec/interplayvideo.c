/*
 * Interplay MVE Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 * Interplay MVE Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/interplay-mve.txt
 * This code is written in such a way that the identifiers match up
 * with the encoding descriptions in the document.
 *
 * This decoder presently only supports a PAL8 output colorspace.
 *
 * An Interplay video frame consists of 2 parts: The decoding map and
 * the video data. A demuxer must load these 2 parts together in a single
 * buffer before sending it through the stream to this decoder.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "bytestream.h"
#include "dsputil.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"

#define PALETTE_COUNT 256

typedef struct IpvideoContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame second_last_frame;
    AVFrame last_frame;
    AVFrame current_frame;
    const unsigned char *decoding_map;
    int decoding_map_size;

    int is_16bpp;
    GetByteContext stream_ptr, mv_ptr;
    unsigned char *pixel_ptr;
    int line_inc;
    int stride;
    int upper_motion_limit_offset;

    uint32_t pal[256];
} IpvideoContext;

static int copy_from(IpvideoContext *s, AVFrame *src, int delta_x, int delta_y)
{
    int current_offset = s->pixel_ptr - s->current_frame.data[0];
    int motion_offset = current_offset + delta_y * s->current_frame.linesize[0]
                       + delta_x * (1 + s->is_16bpp);
    if (motion_offset < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "motion offset < 0 (%d)\n", motion_offset);
        return AVERROR_INVALIDDATA;
    } else if (motion_offset > s->upper_motion_limit_offset) {
        av_log(s->avctx, AV_LOG_ERROR, "motion offset above limit (%d >= %d)\n",
            motion_offset, s->upper_motion_limit_offset);
        return AVERROR_INVALIDDATA;
    }
    if (src->data[0] == NULL) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid decode type, corrupted header?\n");
        return AVERROR(EINVAL);
    }
    s->dsp.put_pixels_tab[!s->is_16bpp][0](s->pixel_ptr, src->data[0] + motion_offset,
                                           s->current_frame.linesize[0], 8);
    return 0;
}

static int ipvideo_decode_block_opcode_0x0(IpvideoContext *s)
{
    return copy_from(s, &s->last_frame, 0, 0);
}

static int ipvideo_decode_block_opcode_0x1(IpvideoContext *s)
{
    return copy_from(s, &s->second_last_frame, 0, 0);
}

static int ipvideo_decode_block_opcode_0x2(IpvideoContext *s)
{
    unsigned char B;
    int x, y;

    /* copy block from 2 frames ago using a motion vector; need 1 more byte */
    if (!s->is_16bpp) {
        B = bytestream2_get_byte(&s->stream_ptr);
    } else {
        B = bytestream2_get_byte(&s->mv_ptr);
    }

    if (B < 56) {
        x = 8 + (B % 7);
        y = B / 7;
    } else {
        x = -14 + ((B - 56) % 29);
        y =   8 + ((B - 56) / 29);
    }

    av_dlog(s->avctx, "motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    return copy_from(s, &s->second_last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x3(IpvideoContext *s)
{
    unsigned char B;
    int x, y;

    /* copy 8x8 block from current frame from an up/left block */

    /* need 1 more byte for motion */
    if (!s->is_16bpp) {
        B = bytestream2_get_byte(&s->stream_ptr);
    } else {
        B = bytestream2_get_byte(&s->mv_ptr);
    }

    if (B < 56) {
        x = -(8 + (B % 7));
        y = -(B / 7);
    } else {
        x = -(-14 + ((B - 56) % 29));
        y = -(  8 + ((B - 56) / 29));
    }

    av_dlog(s->avctx, "motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    return copy_from(s, &s->current_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x4(IpvideoContext *s)
{
    int x, y;
    unsigned char B, BL, BH;

    /* copy a block from the previous frame; need 1 more byte */
    if (!s->is_16bpp) {
        B = bytestream2_get_byte(&s->stream_ptr);
    } else {
        B = bytestream2_get_byte(&s->mv_ptr);
    }

    BL = B & 0x0F;
    BH = (B >> 4) & 0x0F;
    x = -8 + BL;
    y = -8 + BH;

    av_dlog(s->avctx, "motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    return copy_from(s, &s->last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x5(IpvideoContext *s)
{
    signed char x, y;

    /* copy a block from the previous frame using an expanded range;
     * need 2 more bytes */
    x = bytestream2_get_byte(&s->stream_ptr);
    y = bytestream2_get_byte(&s->stream_ptr);

    av_dlog(s->avctx, "motion bytes = %d, %d\n", x, y);
    return copy_from(s, &s->last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x6(IpvideoContext *s)
{
    /* mystery opcode? skip multiple blocks? */
    av_log(s->avctx, AV_LOG_ERROR, "Help! Mystery opcode 0x6 seen\n");

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x7(IpvideoContext *s)
{
    int x, y;
    unsigned char P[2];
    unsigned int flags;

    /* 2-color encoding */
    P[0] = bytestream2_get_byte(&s->stream_ptr);
    P[1] = bytestream2_get_byte(&s->stream_ptr);

    if (P[0] <= P[1]) {

        /* need 8 more bytes from the stream */
        for (y = 0; y < 8; y++) {
            flags = bytestream2_get_byte(&s->stream_ptr) | 0x100;
            for (; flags != 1; flags >>= 1)
                *s->pixel_ptr++ = P[flags & 1];
            s->pixel_ptr += s->line_inc;
        }

    } else {

        /* need 2 more bytes from the stream */
        flags = bytestream2_get_le16(&s->stream_ptr);
        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, flags >>= 1) {
                s->pixel_ptr[x                ] =
                s->pixel_ptr[x + 1            ] =
                s->pixel_ptr[x +     s->stride] =
                s->pixel_ptr[x + 1 + s->stride] = P[flags & 1];
            }
            s->pixel_ptr += s->stride * 2;
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x8(IpvideoContext *s)
{
    int x, y;
    unsigned char P[4];
    unsigned int flags = 0;

    /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
     * either top and bottom or left and right halves */
    P[0] = bytestream2_get_byte(&s->stream_ptr);
    P[1] = bytestream2_get_byte(&s->stream_ptr);

    if (P[0] <= P[1]) {
        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                if (y) {
                    P[0]  = bytestream2_get_byte(&s->stream_ptr);
                    P[1]  = bytestream2_get_byte(&s->stream_ptr);
                }
                flags = bytestream2_get_le16(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 1)
                *s->pixel_ptr++ = P[flags & 1];
            s->pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
        }

    } else {
        flags = bytestream2_get_le32(&s->stream_ptr);
        P[2] = bytestream2_get_byte(&s->stream_ptr);
        P[3] = bytestream2_get_byte(&s->stream_ptr);

        if (P[2] <= P[3]) {

            /* vertical split; left & right halves are 2-color encoded */

            for (y = 0; y < 16; y++) {
                for (x = 0; x < 4; x++, flags >>= 1)
                    *s->pixel_ptr++ = P[flags & 1];
                s->pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) {
                    s->pixel_ptr -= 8 * s->stride - 4;
                    P[0]  = P[2];
                    P[1]  = P[3];
                    flags = bytestream2_get_le32(&s->stream_ptr);
                }
            }

        } else {

            /* horizontal split; top & bottom halves are 2-color encoded */

            for (y = 0; y < 8; y++) {
                if (y == 4) {
                    P[0]  = P[2];
                    P[1]  = P[3];
                    flags = bytestream2_get_le32(&s->stream_ptr);
                }

                for (x = 0; x < 8; x++, flags >>= 1)
                    *s->pixel_ptr++ = P[flags & 1];
                s->pixel_ptr += s->line_inc;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x9(IpvideoContext *s)
{
    int x, y;
    unsigned char P[4];

    /* 4-color encoding */
    bytestream2_get_buffer(&s->stream_ptr, P, 4);

    if (P[0] <= P[1]) {
        if (P[2] <= P[3]) {

            /* 1 of 4 colors for each pixel, need 16 more bytes */
            for (y = 0; y < 8; y++) {
                /* get the next set of 8 2-bit flags */
                int flags = bytestream2_get_le16(&s->stream_ptr);
                for (x = 0; x < 8; x++, flags >>= 2)
                    *s->pixel_ptr++ = P[flags & 0x03];
                s->pixel_ptr += s->line_inc;
            }

        } else {
            uint32_t flags;

            /* 1 of 4 colors for each 2x2 block, need 4 more bytes */
            flags = bytestream2_get_le32(&s->stream_ptr);

            for (y = 0; y < 8; y += 2) {
                for (x = 0; x < 8; x += 2, flags >>= 2) {
                    s->pixel_ptr[x                ] =
                    s->pixel_ptr[x + 1            ] =
                    s->pixel_ptr[x +     s->stride] =
                    s->pixel_ptr[x + 1 + s->stride] = P[flags & 0x03];
                }
                s->pixel_ptr += s->stride * 2;
            }

        }
    } else {
        uint64_t flags;

        /* 1 of 4 colors for each 2x1 or 1x2 block, need 8 more bytes */
        flags = bytestream2_get_le64(&s->stream_ptr);
        if (P[2] <= P[3]) {
            for (y = 0; y < 8; y++) {
                for (x = 0; x < 8; x += 2, flags >>= 2) {
                    s->pixel_ptr[x    ] =
                    s->pixel_ptr[x + 1] = P[flags & 0x03];
                }
                s->pixel_ptr += s->stride;
            }
        } else {
            for (y = 0; y < 8; y += 2) {
                for (x = 0; x < 8; x++, flags >>= 2) {
                    s->pixel_ptr[x            ] =
                    s->pixel_ptr[x + s->stride] = P[flags & 0x03];
                }
                s->pixel_ptr += s->stride * 2;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xA(IpvideoContext *s)
{
    int x, y;
    unsigned char P[8];
    int flags = 0;

    bytestream2_get_buffer(&s->stream_ptr, P, 4);

    /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
     * either top and bottom or left and right halves */
    if (P[0] <= P[1]) {

        /* 4-color encoding for each quadrant; need 32 bytes */
        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                if (y) bytestream2_get_buffer(&s->stream_ptr, P, 4);
                flags = bytestream2_get_le32(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 2)
                *s->pixel_ptr++ = P[flags & 0x03];

            s->pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
        }

    } else {
        // vertical split?
        int vert;
        uint64_t flags = bytestream2_get_le64(&s->stream_ptr);

        bytestream2_get_buffer(&s->stream_ptr, P + 4, 4);
        vert = P[4] <= P[5];

        /* 4-color encoding for either left and right or top and bottom
         * halves */

        for (y = 0; y < 16; y++) {
            for (x = 0; x < 4; x++, flags >>= 2)
                *s->pixel_ptr++ = P[flags & 0x03];

            if (vert) {
                s->pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
            } else if (y & 1) s->pixel_ptr += s->line_inc;

            // load values for second half
            if (y == 7) {
                memcpy(P, P + 4, 4);
                flags = bytestream2_get_le64(&s->stream_ptr);
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xB(IpvideoContext *s)
{
    int y;

    /* 64-color encoding (each pixel in block is a different color) */
    for (y = 0; y < 8; y++) {
        bytestream2_get_buffer(&s->stream_ptr, s->pixel_ptr, 8);
        s->pixel_ptr  += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xC(IpvideoContext *s)
{
    int x, y;

    /* 16-color block encoding: each 2x2 block is a different color */
    for (y = 0; y < 8; y += 2) {
        for (x = 0; x < 8; x += 2) {
            s->pixel_ptr[x                ] =
            s->pixel_ptr[x + 1            ] =
            s->pixel_ptr[x +     s->stride] =
            s->pixel_ptr[x + 1 + s->stride] = bytestream2_get_byte(&s->stream_ptr);
        }
        s->pixel_ptr += s->stride * 2;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xD(IpvideoContext *s)
{
    int y;
    unsigned char P[2];

    /* 4-color block encoding: each 4x4 block is a different color */
    for (y = 0; y < 8; y++) {
        if (!(y & 3)) {
            P[0] = bytestream2_get_byte(&s->stream_ptr);
            P[1] = bytestream2_get_byte(&s->stream_ptr);
        }
        memset(s->pixel_ptr,     P[0], 4);
        memset(s->pixel_ptr + 4, P[1], 4);
        s->pixel_ptr += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xE(IpvideoContext *s)
{
    int y;
    unsigned char pix;

    /* 1-color encoding: the whole block is 1 solid color */
    pix = bytestream2_get_byte(&s->stream_ptr);

    for (y = 0; y < 8; y++) {
        memset(s->pixel_ptr, pix, 8);
        s->pixel_ptr += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xF(IpvideoContext *s)
{
    int x, y;
    unsigned char sample[2];

    /* dithered encoding */
    sample[0] = bytestream2_get_byte(&s->stream_ptr);
    sample[1] = bytestream2_get_byte(&s->stream_ptr);

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x += 2) {
            *s->pixel_ptr++ = sample[  y & 1 ];
            *s->pixel_ptr++ = sample[!(y & 1)];
        }
        s->pixel_ptr += s->line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x6_16(IpvideoContext *s)
{
    signed char x, y;

    /* copy a block from the second last frame using an expanded range */
    x = bytestream2_get_byte(&s->stream_ptr);
    y = bytestream2_get_byte(&s->stream_ptr);

    av_dlog(s->avctx, "motion bytes = %d, %d\n", x, y);
    return copy_from(s, &s->second_last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x7_16(IpvideoContext *s)
{
    int x, y;
    uint16_t P[2];
    unsigned int flags;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 2-color encoding */
    P[0] = bytestream2_get_le16(&s->stream_ptr);
    P[1] = bytestream2_get_le16(&s->stream_ptr);

    if (!(P[0] & 0x8000)) {

        for (y = 0; y < 8; y++) {
            flags = bytestream2_get_byte(&s->stream_ptr) | 0x100;
            for (; flags != 1; flags >>= 1)
                *pixel_ptr++ = P[flags & 1];
            pixel_ptr += s->line_inc;
        }

    } else {

        flags = bytestream2_get_le16(&s->stream_ptr);
        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, flags >>= 1) {
                pixel_ptr[x                ] =
                pixel_ptr[x + 1            ] =
                pixel_ptr[x +     s->stride] =
                pixel_ptr[x + 1 + s->stride] = P[flags & 1];
            }
            pixel_ptr += s->stride * 2;
        }
    }

    return 0;
}

static int ipvideo_decode_block_opcode_0x8_16(IpvideoContext *s)
{
    int x, y;
    uint16_t P[4];
    unsigned int flags = 0;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
     * either top and bottom or left and right halves */
    P[0] = bytestream2_get_le16(&s->stream_ptr);
    P[1] = bytestream2_get_le16(&s->stream_ptr);

    if (!(P[0] & 0x8000)) {

        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                if (y) {
                    P[0] = bytestream2_get_le16(&s->stream_ptr);
                    P[1] = bytestream2_get_le16(&s->stream_ptr);
                }
                flags = bytestream2_get_le16(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 1)
                *pixel_ptr++ = P[flags & 1];
            pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) pixel_ptr -= 8 * s->stride - 4;
        }

    } else {

        flags = bytestream2_get_le32(&s->stream_ptr);
        P[2]  = bytestream2_get_le16(&s->stream_ptr);
        P[3]  = bytestream2_get_le16(&s->stream_ptr);

        if (!(P[2] & 0x8000)) {

            /* vertical split; left & right halves are 2-color encoded */

            for (y = 0; y < 16; y++) {
                for (x = 0; x < 4; x++, flags >>= 1)
                    *pixel_ptr++ = P[flags & 1];
                pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) {
                    pixel_ptr -= 8 * s->stride - 4;
                    P[0]  = P[2];
                    P[1]  = P[3];
                    flags = bytestream2_get_le32(&s->stream_ptr);
                }
            }

        } else {

            /* horizontal split; top & bottom halves are 2-color encoded */

            for (y = 0; y < 8; y++) {
                if (y == 4) {
                    P[0]  = P[2];
                    P[1]  = P[3];
                    flags = bytestream2_get_le32(&s->stream_ptr);
                }

                for (x = 0; x < 8; x++, flags >>= 1)
                    *pixel_ptr++ = P[flags & 1];
                pixel_ptr += s->line_inc;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x9_16(IpvideoContext *s)
{
    int x, y;
    uint16_t P[4];
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 4-color encoding */
    for (x = 0; x < 4; x++)
        P[x] = bytestream2_get_le16(&s->stream_ptr);

    if (!(P[0] & 0x8000)) {
        if (!(P[2] & 0x8000)) {

            /* 1 of 4 colors for each pixel */
            for (y = 0; y < 8; y++) {
                /* get the next set of 8 2-bit flags */
                int flags = bytestream2_get_le16(&s->stream_ptr);
                for (x = 0; x < 8; x++, flags >>= 2)
                    *pixel_ptr++ = P[flags & 0x03];
                pixel_ptr += s->line_inc;
            }

        } else {
            uint32_t flags;

            /* 1 of 4 colors for each 2x2 block */
            flags = bytestream2_get_le32(&s->stream_ptr);

            for (y = 0; y < 8; y += 2) {
                for (x = 0; x < 8; x += 2, flags >>= 2) {
                    pixel_ptr[x                ] =
                    pixel_ptr[x + 1            ] =
                    pixel_ptr[x +     s->stride] =
                    pixel_ptr[x + 1 + s->stride] = P[flags & 0x03];
                }
                pixel_ptr += s->stride * 2;
            }

        }
    } else {
        uint64_t flags;

        /* 1 of 4 colors for each 2x1 or 1x2 block */
        flags = bytestream2_get_le64(&s->stream_ptr);
        if (!(P[2] & 0x8000)) {
            for (y = 0; y < 8; y++) {
                for (x = 0; x < 8; x += 2, flags >>= 2) {
                    pixel_ptr[x    ] =
                    pixel_ptr[x + 1] = P[flags & 0x03];
                }
                pixel_ptr += s->stride;
            }
        } else {
            for (y = 0; y < 8; y += 2) {
                for (x = 0; x < 8; x++, flags >>= 2) {
                    pixel_ptr[x            ] =
                    pixel_ptr[x + s->stride] = P[flags & 0x03];
                }
                pixel_ptr += s->stride * 2;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xA_16(IpvideoContext *s)
{
    int x, y;
    uint16_t P[8];
    int flags = 0;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    for (x = 0; x < 4; x++)
        P[x] = bytestream2_get_le16(&s->stream_ptr);

    /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
     * either top and bottom or left and right halves */
    if (!(P[0] & 0x8000)) {

        /* 4-color encoding for each quadrant */
        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                if (y)
                    for (x = 0; x < 4; x++)
                        P[x] = bytestream2_get_le16(&s->stream_ptr);
                flags = bytestream2_get_le32(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 2)
                *pixel_ptr++ = P[flags & 0x03];

            pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) pixel_ptr -= 8 * s->stride - 4;
        }

    } else {
        // vertical split?
        int vert;
        uint64_t flags = bytestream2_get_le64(&s->stream_ptr);

        for (x = 4; x < 8; x++)
            P[x] = bytestream2_get_le16(&s->stream_ptr);
        vert = !(P[4] & 0x8000);

        /* 4-color encoding for either left and right or top and bottom
         * halves */

        for (y = 0; y < 16; y++) {
            for (x = 0; x < 4; x++, flags >>= 2)
                *pixel_ptr++ = P[flags & 0x03];

            if (vert) {
                pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) pixel_ptr -= 8 * s->stride - 4;
            } else if (y & 1) pixel_ptr += s->line_inc;

            // load values for second half
            if (y == 7) {
                memcpy(P, P + 4, 8);
                flags = bytestream2_get_le64(&s->stream_ptr);
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xB_16(IpvideoContext *s)
{
    int x, y;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 64-color encoding (each pixel in block is a different color) */
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++)
            pixel_ptr[x] = bytestream2_get_le16(&s->stream_ptr);
        pixel_ptr  += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xC_16(IpvideoContext *s)
{
    int x, y;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 16-color block encoding: each 2x2 block is a different color */
    for (y = 0; y < 8; y += 2) {
        for (x = 0; x < 8; x += 2) {
            pixel_ptr[x                ] =
            pixel_ptr[x + 1            ] =
            pixel_ptr[x +     s->stride] =
            pixel_ptr[x + 1 + s->stride] = bytestream2_get_le16(&s->stream_ptr);
        }
        pixel_ptr += s->stride * 2;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xD_16(IpvideoContext *s)
{
    int x, y;
    uint16_t P[2];
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 4-color block encoding: each 4x4 block is a different color */
    for (y = 0; y < 8; y++) {
        if (!(y & 3)) {
            P[0] = bytestream2_get_le16(&s->stream_ptr);
            P[1] = bytestream2_get_le16(&s->stream_ptr);
        }
        for (x = 0; x < 8; x++)
            pixel_ptr[x] = P[x >> 2];
        pixel_ptr += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xE_16(IpvideoContext *s)
{
    int x, y;
    uint16_t pix;
    uint16_t *pixel_ptr = (uint16_t*)s->pixel_ptr;

    /* 1-color encoding: the whole block is 1 solid color */
    pix = bytestream2_get_le16(&s->stream_ptr);

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++)
            pixel_ptr[x] = pix;
        pixel_ptr += s->stride;
    }

    /* report success */
    return 0;
}

static int (* const ipvideo_decode_block[])(IpvideoContext *s) = {
    ipvideo_decode_block_opcode_0x0, ipvideo_decode_block_opcode_0x1,
    ipvideo_decode_block_opcode_0x2, ipvideo_decode_block_opcode_0x3,
    ipvideo_decode_block_opcode_0x4, ipvideo_decode_block_opcode_0x5,
    ipvideo_decode_block_opcode_0x6, ipvideo_decode_block_opcode_0x7,
    ipvideo_decode_block_opcode_0x8, ipvideo_decode_block_opcode_0x9,
    ipvideo_decode_block_opcode_0xA, ipvideo_decode_block_opcode_0xB,
    ipvideo_decode_block_opcode_0xC, ipvideo_decode_block_opcode_0xD,
    ipvideo_decode_block_opcode_0xE, ipvideo_decode_block_opcode_0xF,
};

static int (* const ipvideo_decode_block16[])(IpvideoContext *s) = {
    ipvideo_decode_block_opcode_0x0,    ipvideo_decode_block_opcode_0x1,
    ipvideo_decode_block_opcode_0x2,    ipvideo_decode_block_opcode_0x3,
    ipvideo_decode_block_opcode_0x4,    ipvideo_decode_block_opcode_0x5,
    ipvideo_decode_block_opcode_0x6_16, ipvideo_decode_block_opcode_0x7_16,
    ipvideo_decode_block_opcode_0x8_16, ipvideo_decode_block_opcode_0x9_16,
    ipvideo_decode_block_opcode_0xA_16, ipvideo_decode_block_opcode_0xB_16,
    ipvideo_decode_block_opcode_0xC_16, ipvideo_decode_block_opcode_0xD_16,
    ipvideo_decode_block_opcode_0xE_16, ipvideo_decode_block_opcode_0x1,
};

static void ipvideo_decode_opcodes(IpvideoContext *s)
{
    int x, y;
    unsigned char opcode;
    int ret;
    GetBitContext gb;

    bytestream2_skip(&s->stream_ptr, 14); /* data starts 14 bytes in */
    if (!s->is_16bpp) {
        /* this is PAL8, so make the palette available */
        memcpy(s->current_frame.data[1], s->pal, AVPALETTE_SIZE);

        s->stride = s->current_frame.linesize[0];
    } else {
        s->stride = s->current_frame.linesize[0] >> 1;
        s->mv_ptr = s->stream_ptr;
        bytestream2_skip(&s->mv_ptr, bytestream2_get_le16(&s->stream_ptr));
    }
    s->line_inc = s->stride - 8;
    s->upper_motion_limit_offset = (s->avctx->height - 8) * s->current_frame.linesize[0]
                                  + (s->avctx->width - 8) * (1 + s->is_16bpp);

    init_get_bits(&gb, s->decoding_map, s->decoding_map_size * 8);
    for (y = 0; y < s->avctx->height; y += 8) {
        for (x = 0; x < s->avctx->width; x += 8) {
            opcode = get_bits(&gb, 4);

            av_dlog(s->avctx,
                    "  block @ (%3d, %3d): encoding 0x%X, data ptr offset %d\n",
                    x, y, opcode, bytestream2_tell(&s->stream_ptr));

            if (!s->is_16bpp) {
                s->pixel_ptr = s->current_frame.data[0] + x
                              + y*s->current_frame.linesize[0];
                ret = ipvideo_decode_block[opcode](s);
            } else {
                s->pixel_ptr = s->current_frame.data[0] + x*2
                              + y*s->current_frame.linesize[0];
                ret = ipvideo_decode_block16[opcode](s);
            }
            if (ret != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "decode problem on frame %d, @ block (%d, %d)\n",
                       s->avctx->frame_number, x, y);
                return;
            }
        }
    }
    if (bytestream2_get_bytes_left(&s->stream_ptr) > 1) {
        av_log(s->avctx, AV_LOG_ERROR,
               "decode finished with %d bytes left over\n",
               bytestream2_get_bytes_left(&s->stream_ptr));
    }
}

static av_cold int ipvideo_decode_init(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    s->avctx = avctx;

    s->is_16bpp = avctx->bits_per_coded_sample == 16;
    avctx->pix_fmt = s->is_16bpp ? AV_PIX_FMT_RGB555 : AV_PIX_FMT_PAL8;

    ff_dsputil_init(&s->dsp, avctx);

    avcodec_get_frame_defaults(&s->second_last_frame);
    avcodec_get_frame_defaults(&s->last_frame);
    avcodec_get_frame_defaults(&s->current_frame);

    s->current_frame.data[0] = s->last_frame.data[0] =
    s->second_last_frame.data[0] = NULL;

    return 0;
}

static int ipvideo_decode_frame(AVCodecContext *avctx,
                                void *data, int *got_frame,
                                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    IpvideoContext *s = avctx->priv_data;
    int ret;

    /* decoding map contains 4 bits of information per 8x8 block */
    s->decoding_map_size = avctx->width * avctx->height / (8 * 8 * 2);

    /* compressed buffer needs to be large enough to at least hold an entire
     * decoding map */
    if (buf_size < s->decoding_map_size)
        return buf_size;

    if (s->last_frame.data[0] && av_packet_get_side_data(avpkt, AV_PKT_DATA_PARAM_CHANGE, NULL)) {
        if (s->last_frame.data[0])
            avctx->release_buffer(avctx, &s->last_frame);
        if (s->second_last_frame.data[0])
            avctx->release_buffer(avctx, &s->second_last_frame);
    }

    s->decoding_map = buf;
    bytestream2_init(&s->stream_ptr, buf + s->decoding_map_size,
                     buf_size - s->decoding_map_size);

    s->current_frame.reference = 3;
    if ((ret = ff_get_buffer(avctx, &s->current_frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    if (!s->is_16bpp) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);
        if (pal) {
            s->current_frame.palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }
    }

    ipvideo_decode_opcodes(s);

    *got_frame = 1;
    *(AVFrame*)data = s->current_frame;

    /* shuffle frames */
    if (s->second_last_frame.data[0])
        avctx->release_buffer(avctx, &s->second_last_frame);
    s->second_last_frame = s->last_frame;
    s->last_frame = s->current_frame;
    s->current_frame.data[0] = NULL;  /* catch any access attempts */

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int ipvideo_decode_end(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);
    if (s->second_last_frame.data[0])
        avctx->release_buffer(avctx, &s->second_last_frame);

    return 0;
}

AVCodec ff_interplay_video_decoder = {
    .name           = "interplayvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_INTERPLAY_VIDEO,
    .priv_data_size = sizeof(IpvideoContext),
    .init           = ipvideo_decode_init,
    .close          = ipvideo_decode_end,
    .decode         = ipvideo_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_PARAM_CHANGE,
    .long_name      = NULL_IF_CONFIG_SMALL("Interplay MVE video"),
};
