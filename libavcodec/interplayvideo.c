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
 *
 */

/**
 * @file interplayvideo.c
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
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#define PALETTE_COUNT 256

/* debugging support */
#define DEBUG_INTERPLAY 0
#if DEBUG_INTERPLAY
#define debug_interplay(x,...) av_log(NULL, AV_LOG_DEBUG, x, __VA_ARGS__)
#else
static inline void debug_interplay(const char *format, ...) { }
#endif

typedef struct IpvideoContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame second_last_frame;
    AVFrame last_frame;
    AVFrame current_frame;
    unsigned char *decoding_map;
    int decoding_map_size;

    unsigned char *buf;
    int size;

    unsigned char *stream_ptr;
    unsigned char *stream_end;
    unsigned char *pixel_ptr;
    int line_inc;
    int stride;
    int upper_motion_limit_offset;

} IpvideoContext;

#define CHECK_STREAM_PTR(n) \
  if ((s->stream_ptr + n) > s->stream_end) { \
    av_log(s->avctx, AV_LOG_ERROR, "Interplay video warning: stream_ptr out of bounds (%p >= %p)\n", \
      s->stream_ptr + n, s->stream_end); \
    return -1; \
  }

#define COPY_FROM_CURRENT() \
    motion_offset = current_offset; \
    motion_offset += y * s->stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > s->upper_motion_limit_offset) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, s->upper_motion_limit_offset); \
        return -1; \
    } \
    s->dsp.put_pixels_tab[0][0](s->pixel_ptr, \
        s->current_frame.data[0] + motion_offset, s->stride, 8);

#define COPY_FROM_PREVIOUS() \
    motion_offset = current_offset; \
    motion_offset += y * s->stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > s->upper_motion_limit_offset) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, s->upper_motion_limit_offset); \
        return -1; \
    } \
    s->dsp.put_pixels_tab[0][0](s->pixel_ptr, \
        s->last_frame.data[0] + motion_offset, s->stride, 8);

#define COPY_FROM_SECOND_LAST() \
    motion_offset = current_offset; \
    motion_offset += y * s->stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > s->upper_motion_limit_offset) { \
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, s->upper_motion_limit_offset); \
        return -1; \
    } \
    s->dsp.put_pixels_tab[0][0](s->pixel_ptr, \
        s->second_last_frame.data[0] + motion_offset, s->stride, 8);

static int ipvideo_decode_block_opcode_0x0(IpvideoContext *s)
{
    int x, y;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy a block from the previous frame */
    x = y = 0;
    COPY_FROM_PREVIOUS();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x1(IpvideoContext *s)
{
    int x, y;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy block from 2 frames ago */
    x = y = 0;
    COPY_FROM_SECOND_LAST();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x2(IpvideoContext *s)
{
    unsigned char B;
    int x, y;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy block from 2 frames ago using a motion vector; need 1 more byte */
    CHECK_STREAM_PTR(1);
    B = *s->stream_ptr++;

    if (B < 56) {
        x = 8 + (B % 7);
        y = B / 7;
    } else {
        x = -14 + ((B - 56) % 29);
        y =   8 + ((B - 56) / 29);
    }

    debug_interplay ("    motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    COPY_FROM_SECOND_LAST();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x3(IpvideoContext *s)
{
    unsigned char B;
    int x, y;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy 8x8 block from current frame from an up/left block */

    /* need 1 more byte for motion */
    CHECK_STREAM_PTR(1);
    B = *s->stream_ptr++;

    if (B < 56) {
        x = -(8 + (B % 7));
        y = -(B / 7);
    } else {
        x = -(-14 + ((B - 56) % 29));
        y = -(  8 + ((B - 56) / 29));
    }

    debug_interplay ("    motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    COPY_FROM_CURRENT();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x4(IpvideoContext *s)
{
    int x, y;
    unsigned char B, BL, BH;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy a block from the previous frame; need 1 more byte */
    CHECK_STREAM_PTR(1);

    B = *s->stream_ptr++;
    BL = B & 0x0F;
    BH = (B >> 4) & 0x0F;
    x = -8 + BL;
    y = -8 + BH;

    debug_interplay ("    motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    COPY_FROM_PREVIOUS();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x5(IpvideoContext *s)
{
    signed char x, y;
    int motion_offset;
    int current_offset = s->pixel_ptr - s->current_frame.data[0];

    /* copy a block from the previous frame using an expanded range;
     * need 2 more bytes */
    CHECK_STREAM_PTR(2);

    x = *s->stream_ptr++;
    y = *s->stream_ptr++;

    debug_interplay ("    motion bytes = %d, %d\n", x, y);
    COPY_FROM_PREVIOUS();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x6(IpvideoContext *s)
{
    /* mystery opcode? skip multiple blocks? */
    av_log(s->avctx, AV_LOG_ERROR, "  Interplay video: Help! Mystery opcode 0x6 seen\n");

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x7(IpvideoContext *s)
{
    int x, y;
    unsigned char P0, P1;
    unsigned char B[8];
    unsigned int flags;
    int bitmask;

    /* 2-color encoding */
    CHECK_STREAM_PTR(2);

    P0 = *s->stream_ptr++;
    P1 = *s->stream_ptr++;

    if (P0 <= P1) {

        /* need 8 more bytes from the stream */
        CHECK_STREAM_PTR(8);
        for (y = 0; y < 8; y++)
            B[y] = *s->stream_ptr++;

        for (y = 0; y < 8; y++) {
            flags = B[y];
            for (x = 0x01; x <= 0x80; x <<= 1) {
                if (flags & x)
                    *s->pixel_ptr++ = P1;
                else
                    *s->pixel_ptr++ = P0;
            }
            s->pixel_ptr += s->line_inc;
        }

    } else {

        /* need 2 more bytes from the stream */
        CHECK_STREAM_PTR(2);
        B[0] = *s->stream_ptr++;
        B[1] = *s->stream_ptr++;

        flags = (B[1] << 8) | B[0];
        bitmask = 0x0001;
        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, bitmask <<= 1) {
                if (flags & bitmask) {
                    *(s->pixel_ptr + x) = P1;
                    *(s->pixel_ptr + x + 1) = P1;
                    *(s->pixel_ptr + s->stride + x) = P1;
                    *(s->pixel_ptr + s->stride + x + 1) = P1;
                } else {
                    *(s->pixel_ptr + x) = P0;
                    *(s->pixel_ptr + x + 1) = P0;
                    *(s->pixel_ptr + s->stride + x) = P0;
                    *(s->pixel_ptr + s->stride + x + 1) = P0;
                }
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
    unsigned char P[8];
    unsigned char B[8];
    unsigned int flags = 0;
    unsigned int bitmask = 0;
    unsigned char P0 = 0, P1 = 0;
    int lower_half = 0;

    /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
     * either top and bottom or left and right halves */
    CHECK_STREAM_PTR(2);

    P[0] = *s->stream_ptr++;
    P[1] = *s->stream_ptr++;

    if (P[0] <= P[1]) {

        /* need 12 more bytes */
        CHECK_STREAM_PTR(12);
        B[0] = *s->stream_ptr++;  B[1] = *s->stream_ptr++;
        P[2] = *s->stream_ptr++;  P[3] = *s->stream_ptr++;
        B[2] = *s->stream_ptr++;  B[3] = *s->stream_ptr++;
        P[4] = *s->stream_ptr++;  P[5] = *s->stream_ptr++;
        B[4] = *s->stream_ptr++;  B[5] = *s->stream_ptr++;
        P[6] = *s->stream_ptr++;  P[7] = *s->stream_ptr++;
        B[6] = *s->stream_ptr++;  B[7] = *s->stream_ptr++;

        for (y = 0; y < 8; y++) {

            /* time to reload flags? */
            if (y == 0) {
                flags =
                    ((B[0] & 0xF0) <<  4) | ((B[4] & 0xF0) <<  8) |
                    ((B[0] & 0x0F)      ) | ((B[4] & 0x0F) <<  4) |
                    ((B[1] & 0xF0) << 20) | ((B[5] & 0xF0) << 24) |
                    ((B[1] & 0x0F) << 16) | ((B[5] & 0x0F) << 20);
                bitmask = 0x00000001;
                lower_half = 0;  /* still on top half */
            } else if (y == 4) {
                flags =
                    ((B[2] & 0xF0) <<  4) | ((B[6] & 0xF0) <<  8) |
                    ((B[2] & 0x0F)      ) | ((B[6] & 0x0F) <<  4) |
                    ((B[3] & 0xF0) << 20) | ((B[7] & 0xF0) << 24) |
                    ((B[3] & 0x0F) << 16) | ((B[7] & 0x0F) << 20);
                bitmask = 0x00000001;
                lower_half = 2;
            }

            for (x = 0; x < 8; x++, bitmask <<= 1) {
                /* get the pixel values ready for this quadrant */
                if (x == 0) {
                    P0 = P[lower_half + 0];
                    P1 = P[lower_half + 1];
                } else if (x == 4) {
                    P0 = P[lower_half + 4];
                    P1 = P[lower_half + 5];
                }

                if (flags & bitmask)
                    *s->pixel_ptr++ = P1;
                else
                    *s->pixel_ptr++ = P0;
            }
            s->pixel_ptr += s->line_inc;
        }

    } else {

        /* need 10 more bytes */
        CHECK_STREAM_PTR(10);
        B[0] = *s->stream_ptr++;  B[1] = *s->stream_ptr++;
        B[2] = *s->stream_ptr++;  B[3] = *s->stream_ptr++;
        P[2] = *s->stream_ptr++;  P[3] = *s->stream_ptr++;
        B[4] = *s->stream_ptr++;  B[5] = *s->stream_ptr++;
        B[6] = *s->stream_ptr++;  B[7] = *s->stream_ptr++;

        if (P[2] <= P[3]) {

            /* vertical split; left & right halves are 2-color encoded */

            for (y = 0; y < 8; y++) {

                /* time to reload flags? */
                if (y == 0) {
                    flags =
                        ((B[0] & 0xF0) <<  4) | ((B[4] & 0xF0) <<  8) |
                        ((B[0] & 0x0F)      ) | ((B[4] & 0x0F) <<  4) |
                        ((B[1] & 0xF0) << 20) | ((B[5] & 0xF0) << 24) |
                        ((B[1] & 0x0F) << 16) | ((B[5] & 0x0F) << 20);
                    bitmask = 0x00000001;
                } else if (y == 4) {
                    flags =
                        ((B[2] & 0xF0) <<  4) | ((B[6] & 0xF0) <<  8) |
                        ((B[2] & 0x0F)      ) | ((B[6] & 0x0F) <<  4) |
                        ((B[3] & 0xF0) << 20) | ((B[7] & 0xF0) << 24) |
                        ((B[3] & 0x0F) << 16) | ((B[7] & 0x0F) << 20);
                    bitmask = 0x00000001;
                }

                for (x = 0; x < 8; x++, bitmask <<= 1) {
                    /* get the pixel values ready for this half */
                    if (x == 0) {
                        P0 = P[0];
                        P1 = P[1];
                    } else if (x == 4) {
                        P0 = P[2];
                        P1 = P[3];
                    }

                    if (flags & bitmask)
                        *s->pixel_ptr++ = P1;
                    else
                        *s->pixel_ptr++ = P0;
                }
                s->pixel_ptr += s->line_inc;
            }

        } else {

            /* horizontal split; top & bottom halves are 2-color encoded */

            for (y = 0; y < 8; y++) {

                flags = B[y];
                if (y == 0) {
                    P0 = P[0];
                    P1 = P[1];
                } else if (y == 4) {
                    P0 = P[2];
                    P1 = P[3];
                }

                for (bitmask = 0x01; bitmask <= 0x80; bitmask <<= 1) {

                    if (flags & bitmask)
                        *s->pixel_ptr++ = P1;
                    else
                        *s->pixel_ptr++ = P0;
                }
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
    unsigned char B[4];
    unsigned int flags = 0;
    int shifter = 0;
    unsigned char pix;

    /* 4-color encoding */
    CHECK_STREAM_PTR(4);

    for (y = 0; y < 4; y++)
        P[y] = *s->stream_ptr++;

    if ((P[0] <= P[1]) && (P[2] <= P[3])) {

        /* 1 of 4 colors for each pixel, need 16 more bytes */
        CHECK_STREAM_PTR(16);

        for (y = 0; y < 8; y++) {
            /* get the next set of 8 2-bit flags */
            flags = (s->stream_ptr[1] << 8) | s->stream_ptr[0];
            s->stream_ptr += 2;
            for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                *s->pixel_ptr++ = P[(flags >> shifter) & 0x03];
            }
            s->pixel_ptr += s->line_inc;
        }

    } else if ((P[0] <= P[1]) && (P[2] > P[3])) {

        /* 1 of 4 colors for each 2x2 block, need 4 more bytes */
        CHECK_STREAM_PTR(4);

        B[0] = *s->stream_ptr++;
        B[1] = *s->stream_ptr++;
        B[2] = *s->stream_ptr++;
        B[3] = *s->stream_ptr++;
        flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
        shifter = 0;

        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(s->pixel_ptr + x) = pix;
                *(s->pixel_ptr + x + 1) = pix;
                *(s->pixel_ptr + s->stride + x) = pix;
                *(s->pixel_ptr + s->stride + x + 1) = pix;
            }
            s->pixel_ptr += s->stride * 2;
        }

    } else if ((P[0] > P[1]) && (P[2] <= P[3])) {

        /* 1 of 4 colors for each 2x1 block, need 8 more bytes */
        CHECK_STREAM_PTR(8);

        for (y = 0; y < 8; y++) {
            /* time to reload flags? */
            if ((y == 0) || (y == 4)) {
                B[0] = *s->stream_ptr++;
                B[1] = *s->stream_ptr++;
                B[2] = *s->stream_ptr++;
                B[3] = *s->stream_ptr++;
                flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
                shifter = 0;
            }
            for (x = 0; x < 8; x += 2, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(s->pixel_ptr + x) = pix;
                *(s->pixel_ptr + x + 1) = pix;
            }
            s->pixel_ptr += s->stride;
        }

    } else {

        /* 1 of 4 colors for each 1x2 block, need 8 more bytes */
        CHECK_STREAM_PTR(8);

        for (y = 0; y < 8; y += 2) {
            /* time to reload flags? */
            if ((y == 0) || (y == 4)) {
                B[0] = *s->stream_ptr++;
                B[1] = *s->stream_ptr++;
                B[2] = *s->stream_ptr++;
                B[3] = *s->stream_ptr++;
                flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
                shifter = 0;
            }
            for (x = 0; x < 8; x++, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(s->pixel_ptr + x) = pix;
                *(s->pixel_ptr + s->stride + x) = pix;
            }
            s->pixel_ptr += s->stride * 2;
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xA(IpvideoContext *s)
{
    int x, y;
    unsigned char P[16];
    unsigned char B[16];
    int flags = 0;
    int shifter = 0;
    int index;
    int split;
    int lower_half;

    /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
     * either top and bottom or left and right halves */
    CHECK_STREAM_PTR(4);

    for (y = 0; y < 4; y++)
        P[y] = *s->stream_ptr++;

    if (P[0] <= P[1]) {

        /* 4-color encoding for each quadrant; need 28 more bytes */
        CHECK_STREAM_PTR(28);

        for (y = 0; y < 4; y++)
            B[y] = *s->stream_ptr++;
        for (y = 4; y < 16; y += 4) {
            for (x = y; x < y + 4; x++)
                P[x] = *s->stream_ptr++;
            for (x = y; x < y + 4; x++)
                B[x] = *s->stream_ptr++;
        }

        for (y = 0; y < 8; y++) {

            lower_half = (y >= 4) ? 4 : 0;
            flags = (B[y + 8] << 8) | B[y];

            for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                split = (x >= 4) ? 8 : 0;
                index = split + lower_half + ((flags >> shifter) & 0x03);
                *s->pixel_ptr++ = P[index];
            }

            s->pixel_ptr += s->line_inc;
        }

    } else {

        /* 4-color encoding for either left and right or top and bottom
         * halves; need 20 more bytes */
        CHECK_STREAM_PTR(20);

        for (y = 0; y < 8; y++)
            B[y] = *s->stream_ptr++;
        for (y = 4; y < 8; y++)
            P[y] = *s->stream_ptr++;
        for (y = 8; y < 16; y++)
            B[y] = *s->stream_ptr++;

        if (P[4] <= P[5]) {

            /* block is divided into left and right halves */
            for (y = 0; y < 8; y++) {

                flags = (B[y + 8] << 8) | B[y];
                split = 0;

                for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                    if (x == 4)
                        split = 4;
                    *s->pixel_ptr++ = P[split + ((flags >> shifter) & 0x03)];
                }

                s->pixel_ptr += s->line_inc;
            }

        } else {

            /* block is divided into top and bottom halves */
            split = 0;
            for (y = 0; y < 8; y++) {

                flags = (B[y * 2 + 1] << 8) | B[y * 2];
                if (y == 4)
                    split = 4;

                for (x = 0, shifter = 0; x < 8; x++, shifter += 2)
                    *s->pixel_ptr++ = P[split + ((flags >> shifter) & 0x03)];

                s->pixel_ptr += s->line_inc;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xB(IpvideoContext *s)
{
    int x, y;

    /* 64-color encoding (each pixel in block is a different color) */
    CHECK_STREAM_PTR(64);

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *s->pixel_ptr++ = *s->stream_ptr++;
        }
        s->pixel_ptr += s->line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xC(IpvideoContext *s)
{
    int x, y;
    unsigned char pix;

    /* 16-color block encoding: each 2x2 block is a different color */
    CHECK_STREAM_PTR(16);

    for (y = 0; y < 8; y += 2) {
        for (x = 0; x < 8; x += 2) {
            pix = *s->stream_ptr++;
            *(s->pixel_ptr + x) = pix;
            *(s->pixel_ptr + x + 1) = pix;
            *(s->pixel_ptr + s->stride + x) = pix;
            *(s->pixel_ptr + s->stride + x + 1) = pix;
        }
        s->pixel_ptr += s->stride * 2;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xD(IpvideoContext *s)
{
    int x, y;
    unsigned char P[4];
    unsigned char index = 0;

    /* 4-color block encoding: each 4x4 block is a different color */
    CHECK_STREAM_PTR(4);

    for (y = 0; y < 4; y++)
        P[y] = *s->stream_ptr++;

    for (y = 0; y < 8; y++) {
        if (y < 4)
            index = 0;
        else
            index = 2;

        for (x = 0; x < 8; x++) {
            if (x == 4)
                index++;
            *s->pixel_ptr++ = P[index];
        }
        s->pixel_ptr += s->line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xE(IpvideoContext *s)
{
    int x, y;
    unsigned char pix;

    /* 1-color encoding: the whole block is 1 solid color */
    CHECK_STREAM_PTR(1);
    pix = *s->stream_ptr++;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *s->pixel_ptr++ = pix;
        }
        s->pixel_ptr += s->line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xF(IpvideoContext *s)
{
    int x, y;
    unsigned char sample0, sample1;

    /* dithered encoding */
    CHECK_STREAM_PTR(2);
    sample0 = *s->stream_ptr++;
    sample1 = *s->stream_ptr++;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x += 2) {
            if (y & 1) {
                *s->pixel_ptr++ = sample1;
                *s->pixel_ptr++ = sample0;
            } else {
                *s->pixel_ptr++ = sample0;
                *s->pixel_ptr++ = sample1;
            }
        }
        s->pixel_ptr += s->line_inc;
    }

    /* report success */
    return 0;
}

static int (*ipvideo_decode_block[16])(IpvideoContext *s);

static void ipvideo_decode_opcodes(IpvideoContext *s)
{
    int x, y;
    int index = 0;
    unsigned char opcode;
    int ret;
    int code_counts[16];
    static int frame = 0;

    debug_interplay("------------------ frame %d\n", frame);
    frame++;

    for (x = 0; x < 16; x++)
        code_counts[x] = 0;

    /* this is PAL8, so make the palette available */
    memcpy(s->current_frame.data[1], s->avctx->palctrl->palette, PALETTE_COUNT * 4);

    s->stride = s->current_frame.linesize[0];
    s->stream_ptr = s->buf + 14;  /* data starts 14 bytes in */
    s->stream_end = s->buf + s->size;
    s->line_inc = s->stride - 8;
    s->upper_motion_limit_offset = (s->avctx->height - 8) * s->stride
        + s->avctx->width - 8;
    s->dsp = s->dsp;

    for (y = 0; y < (s->stride * s->avctx->height); y += s->stride * 8) {
        for (x = y; x < y + s->avctx->width; x += 8) {
            /* bottom nibble first, then top nibble (which makes it
             * hard to use a GetBitcontext) */
            if (index & 1)
                opcode = s->decoding_map[index >> 1] >> 4;
            else
                opcode = s->decoding_map[index >> 1] & 0xF;
            index++;

            debug_interplay("  block @ (%3d, %3d): encoding 0x%X, data ptr @ %p\n",
                x - y, y / s->stride, opcode, s->stream_ptr);
            code_counts[opcode]++;

            s->pixel_ptr = s->current_frame.data[0] + x;
            ret = ipvideo_decode_block[opcode](s);
            if (ret != 0) {
                av_log(s->avctx, AV_LOG_ERROR, " Interplay video: decode problem on frame %d, @ block (%d, %d)\n",
                    frame, x - y, y / s->stride);
                return;
            }
        }
    }
    if ((s->stream_ptr != s->stream_end) &&
        (s->stream_ptr + 1 != s->stream_end)) {
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: decode finished with %td bytes left over\n",
            s->stream_end - s->stream_ptr);
    }
}

static int ipvideo_decode_init(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    s->avctx = avctx;

    if (s->avctx->palctrl == NULL) {
        av_log(avctx, AV_LOG_ERROR, " Interplay video: palette expected.\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    /* decoding map contains 4 bits of information per 8x8 block */
    s->decoding_map_size = avctx->width * avctx->height / (8 * 8 * 2);

    /* assign block decode functions */
    ipvideo_decode_block[0x0] = ipvideo_decode_block_opcode_0x0;
    ipvideo_decode_block[0x1] = ipvideo_decode_block_opcode_0x1;
    ipvideo_decode_block[0x2] = ipvideo_decode_block_opcode_0x2;
    ipvideo_decode_block[0x3] = ipvideo_decode_block_opcode_0x3;
    ipvideo_decode_block[0x4] = ipvideo_decode_block_opcode_0x4;
    ipvideo_decode_block[0x5] = ipvideo_decode_block_opcode_0x5;
    ipvideo_decode_block[0x6] = ipvideo_decode_block_opcode_0x6;
    ipvideo_decode_block[0x7] = ipvideo_decode_block_opcode_0x7;
    ipvideo_decode_block[0x8] = ipvideo_decode_block_opcode_0x8;
    ipvideo_decode_block[0x9] = ipvideo_decode_block_opcode_0x9;
    ipvideo_decode_block[0xA] = ipvideo_decode_block_opcode_0xA;
    ipvideo_decode_block[0xB] = ipvideo_decode_block_opcode_0xB;
    ipvideo_decode_block[0xC] = ipvideo_decode_block_opcode_0xC;
    ipvideo_decode_block[0xD] = ipvideo_decode_block_opcode_0xD;
    ipvideo_decode_block[0xE] = ipvideo_decode_block_opcode_0xE;
    ipvideo_decode_block[0xF] = ipvideo_decode_block_opcode_0xF;

    s->current_frame.data[0] = s->last_frame.data[0] =
    s->second_last_frame.data[0] = NULL;

    return 0;
}

static int ipvideo_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                uint8_t *buf, int buf_size)
{
    IpvideoContext *s = avctx->priv_data;
    AVPaletteControl *palette_control = avctx->palctrl;

    /* compressed buffer needs to be large enough to at least hold an entire
     * decoding map */
    if (buf_size < s->decoding_map_size)
        return buf_size;

    s->decoding_map = buf;
    s->buf = buf + s->decoding_map_size;
    s->size = buf_size - s->decoding_map_size;

    s->current_frame.reference = 3;
    if (avctx->get_buffer(avctx, &s->current_frame)) {
        av_log(avctx, AV_LOG_ERROR, "  Interplay Video: get_buffer() failed\n");
        return -1;
    }

    ipvideo_decode_opcodes(s);

    if (palette_control->palette_changed) {
        palette_control->palette_changed = 0;
        s->current_frame.palette_has_changed = 1;
    }

    *data_size = sizeof(AVFrame);
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

static int ipvideo_decode_end(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);
    if (s->second_last_frame.data[0])
        avctx->release_buffer(avctx, &s->second_last_frame);

    return 0;
}

AVCodec interplay_video_decoder = {
    "interplayvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_INTERPLAY_VIDEO,
    sizeof(IpvideoContext),
    ipvideo_decode_init,
    NULL,
    ipvideo_decode_end,
    ipvideo_decode_frame,
    CODEC_CAP_DR1,
};
