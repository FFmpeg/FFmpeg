/*
 * Interplay MVE Video Decoder
 * Copyright (C) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#define debug_interplay printf
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

    unsigned char palette[PALETTE_COUNT * 4];

} IpvideoContext;

#define CHECK_STREAM_PTR(n) \
  if ((sg_stream_ptr + n) > sg_stream_end) { \
    printf ("Interplay video warning: stream_ptr out of bounds (%p >= %p)\n", \
      sg_stream_ptr + n, sg_stream_end); \
    return -1; \
  }

static void ipvideo_new_palette(IpvideoContext *s, unsigned char *palette) {

    int i;
    unsigned char r, g, b;
    unsigned int *palette32;

    switch (s->avctx->pix_fmt) {

    case PIX_FMT_PAL8:
        palette32 = (unsigned int *)s->palette;
        for (i = 0; i < PALETTE_COUNT; i++) {
            r = *palette++;
            g = *palette++;
            b = *palette++;
            palette32[i] = (r << 16) | (g << 8) | (b);
        }
        break;

    default:
        printf ("Interplay video: Unhandled video format\n");
        break;
    }
}

static unsigned char *sg_stream_ptr;
static unsigned char *sg_stream_end;
static unsigned char *sg_current_plane;
static unsigned char *sg_output_plane;
static unsigned char *sg_last_plane;
static unsigned char *sg_second_last_plane;
static int sg_line_inc;
static int sg_stride;
static int sg_upper_motion_limit_offset;
static DSPContext sg_dsp;

static int ipvideo_decode_block_opcode_0x0(void)
{
    int x, y;
    unsigned char *src_block;

    /* skip block, which actually means to copy from previous frame */
    src_block = sg_last_plane + (sg_output_plane - sg_current_plane);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *sg_output_plane++ = *src_block++;
        }
        sg_output_plane += sg_line_inc;
        src_block += sg_line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x1(void)
{
    int x, y;
    unsigned char *src_block;

    /* copy block from two frames behind */
    src_block = sg_second_last_plane + (sg_output_plane - sg_current_plane);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *sg_output_plane++ = *src_block++;
        }
        sg_output_plane += sg_line_inc;
        src_block += sg_line_inc;
    }

    /* report success */
    return 0;
}

#define COPY_FROM_CURRENT() \
    motion_offset = current_offset; \
    motion_offset += y * sg_stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        printf (" Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > sg_upper_motion_limit_offset) { \
        printf (" Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, sg_upper_motion_limit_offset); \
        return -1; \
    } \
    sg_dsp.put_pixels_tab[0][0](sg_output_plane, \
        sg_current_plane + motion_offset, sg_stride, 8);

#define COPY_FROM_PREVIOUS() \
    motion_offset = current_offset; \
    motion_offset += y * sg_stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        printf (" Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > sg_upper_motion_limit_offset) { \
        printf (" Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, sg_upper_motion_limit_offset); \
        return -1; \
    } \
    sg_dsp.put_pixels_tab[0][0](sg_output_plane, \
        sg_last_plane + motion_offset, sg_stride, 8);

#define COPY_FROM_SECOND_LAST() \
    motion_offset = current_offset; \
    motion_offset += y * sg_stride; \
    motion_offset += x; \
    if (motion_offset < 0) { \
        printf (" Interplay video: motion offset < 0 (%d)\n", motion_offset); \
        return -1; \
    } else if (motion_offset > sg_upper_motion_limit_offset) { \
        printf (" Interplay video: motion offset above limit (%d >= %d)\n", \
            motion_offset, sg_upper_motion_limit_offset); \
        return -1; \
    } \
    sg_dsp.put_pixels_tab[0][0](sg_output_plane, \
        sg_second_last_plane + motion_offset, sg_stride, 8);

static int ipvideo_decode_block_opcode_0x2(void)
{
    unsigned char B;
    int x, y;
    int motion_offset;
    int current_offset = sg_output_plane - sg_current_plane;

    /* need 1 more byte for motion */
    CHECK_STREAM_PTR(1);
    B = *sg_stream_ptr++;

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

static int ipvideo_decode_block_opcode_0x3(void)
{
    unsigned char B;
    int x, y;
    int motion_offset;
    int current_offset = sg_output_plane - sg_current_plane;

    /* copy 8x8 block from current frame from an up/left block */

    /* need 1 more byte for motion */
    CHECK_STREAM_PTR(1);
    B = *sg_stream_ptr++;

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

static int ipvideo_decode_block_opcode_0x4(void)
{
    int x, y;
    unsigned char B, BL, BH;
    int motion_offset;
    int current_offset = sg_output_plane - sg_current_plane;

    /* copy a block from the previous frame; need 1 more byte */
    CHECK_STREAM_PTR(1);

    B = *sg_stream_ptr++;
    BL = B & 0x0F;
    BH = (B >> 4) & 0x0F;
    x = -8 + BL;
    y = -8 + BH;

    debug_interplay ("    motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    COPY_FROM_PREVIOUS();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x5(void)
{
    signed char x, y;
    int motion_offset;
    int current_offset = sg_output_plane - sg_current_plane;

    /* copy a block from the previous frame using an expanded range;
     * need 2 more bytes */
    CHECK_STREAM_PTR(2);

    x = *sg_stream_ptr++;
    y = *sg_stream_ptr++;

    debug_interplay ("    motion bytes = %d, %d\n", x, y);
    COPY_FROM_PREVIOUS();

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x6(void)
{
    /* mystery opcode? skip multiple blocks? */
    printf ("  Interplay video: Help! Mystery opcode 0x6 seen\n");

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x7(void)
{
    int x, y;
    unsigned char P0, P1;
    unsigned char B[8];
    unsigned int flags;
    int bitmask;

    /* 2-color encoding */
    CHECK_STREAM_PTR(2);

    P0 = *sg_stream_ptr++;
    P1 = *sg_stream_ptr++;

    if (P0 <= P1) {

        /* need 8 more bytes from the stream */
        CHECK_STREAM_PTR(8);
        for (y = 0; y < 8; y++)
            B[y] = *sg_stream_ptr++;

        for (y = 0; y < 8; y++) {
            flags = B[y];
            for (x = 0x01; x <= 0x80; x <<= 1) {
                if (flags & x)
                    *sg_output_plane++ = P1;
                else
                    *sg_output_plane++ = P0;
            }
            sg_output_plane += sg_line_inc;
        }

    } else {

        /* need 2 more bytes from the stream */
        CHECK_STREAM_PTR(2);
        B[0] = *sg_stream_ptr++;
        B[1] = *sg_stream_ptr++;

        flags = (B[1] << 8) | B[0];
        bitmask = 0x0001;
        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, bitmask <<= 1) {
                if (flags & bitmask) {
                    *(sg_output_plane + x) = P1;
                    *(sg_output_plane + x + 1) = P1;
                    *(sg_output_plane + sg_stride + x) = P1;
                    *(sg_output_plane + sg_stride + x + 1) = P1;
                } else {
                    *(sg_output_plane + x) = P0;
                    *(sg_output_plane + x + 1) = P0;
                    *(sg_output_plane + sg_stride + x) = P0;
                    *(sg_output_plane + sg_stride + x + 1) = P0;
                }
            }
            sg_output_plane += sg_stride * 2;
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x8(void)
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

    P[0] = *sg_stream_ptr++;
    P[1] = *sg_stream_ptr++;

    if (P[0] <= P[1]) {

        /* need 12 more bytes */
        CHECK_STREAM_PTR(12);
        B[0] = *sg_stream_ptr++;  B[1] = *sg_stream_ptr++;
        P[2] = *sg_stream_ptr++;  P[3] = *sg_stream_ptr++;
        B[2] = *sg_stream_ptr++;  B[3] = *sg_stream_ptr++;
        P[4] = *sg_stream_ptr++;  P[5] = *sg_stream_ptr++;
        B[4] = *sg_stream_ptr++;  B[5] = *sg_stream_ptr++;
        P[6] = *sg_stream_ptr++;  P[7] = *sg_stream_ptr++;
        B[6] = *sg_stream_ptr++;  B[7] = *sg_stream_ptr++;

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
                    *sg_output_plane++ = P1;
                else
                    *sg_output_plane++ = P0;
            }
            sg_output_plane += sg_line_inc;
        }

    } else {

        /* need 10 more bytes */
        CHECK_STREAM_PTR(10);
        B[0] = *sg_stream_ptr++;  B[1] = *sg_stream_ptr++;
        B[2] = *sg_stream_ptr++;  B[3] = *sg_stream_ptr++;
        P[2] = *sg_stream_ptr++;  P[3] = *sg_stream_ptr++;
        B[4] = *sg_stream_ptr++;  B[5] = *sg_stream_ptr++;
        B[6] = *sg_stream_ptr++;  B[7] = *sg_stream_ptr++;

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
                        *sg_output_plane++ = P1;
                    else
                        *sg_output_plane++ = P0;
                }
                sg_output_plane += sg_line_inc;
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
                        *sg_output_plane++ = P1;
                    else
                        *sg_output_plane++ = P0;
                }
                sg_output_plane += sg_line_inc;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0x9(void)
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
        P[y] = *sg_stream_ptr++;

    if ((P[0] <= P[1]) && (P[2] <= P[3])) {

        /* 1 of 4 colors for each pixel, need 16 more bytes */
        CHECK_STREAM_PTR(16);

        for (y = 0; y < 8; y++) {
            /* get the next set of 8 2-bit flags */
            flags = (sg_stream_ptr[1] << 8) | sg_stream_ptr[0];
            sg_stream_ptr += 2;
            for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                *sg_output_plane++ = P[(flags >> shifter) & 0x03];
            }
            sg_output_plane += sg_line_inc;
        }

    } else if ((P[0] <= P[1]) && (P[2] > P[3])) {

        /* 1 of 4 colors for each 2x2 block, need 4 more bytes */
        CHECK_STREAM_PTR(4);

        B[0] = *sg_stream_ptr++;
        B[1] = *sg_stream_ptr++;
        B[2] = *sg_stream_ptr++;
        B[3] = *sg_stream_ptr++;
        flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
        shifter = 0;

        for (y = 0; y < 8; y += 2) {
            for (x = 0; x < 8; x += 2, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(sg_output_plane + x) = pix;
                *(sg_output_plane + x + 1) = pix;
                *(sg_output_plane + sg_stride + x) = pix;
                *(sg_output_plane + sg_stride + x + 1) = pix;
            }
            sg_output_plane += sg_stride * 2;
        }

    } else if ((P[0] > P[1]) && (P[2] <= P[3])) {

        /* 1 of 4 colors for each 2x1 block, need 8 more bytes */
        CHECK_STREAM_PTR(8);

        for (y = 0; y < 8; y++) {
            /* time to reload flags? */
            if ((y == 0) || (y == 4)) {
                B[0] = *sg_stream_ptr++;
                B[1] = *sg_stream_ptr++;
                B[2] = *sg_stream_ptr++;
                B[3] = *sg_stream_ptr++;
                flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
                shifter = 0;
            }
            for (x = 0; x < 8; x += 2, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(sg_output_plane + x) = pix;
                *(sg_output_plane + x + 1) = pix;
            }
            sg_output_plane += sg_stride;
        }

    } else {

        /* 1 of 4 colors for each 1x2 block, need 8 more bytes */
        CHECK_STREAM_PTR(8);

        for (y = 0; y < 8; y += 2) {
            /* time to reload flags? */
            if ((y == 0) || (y == 4)) {
                B[0] = *sg_stream_ptr++;
                B[1] = *sg_stream_ptr++;
                B[2] = *sg_stream_ptr++;
                B[3] = *sg_stream_ptr++;
                flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
                shifter = 0;
            }
            for (x = 0; x < 8; x++, shifter += 2) {
                pix = P[(flags >> shifter) & 0x03];
                *(sg_output_plane + x) = pix;
                *(sg_output_plane + sg_stride + x) = pix;
            }
            sg_output_plane += sg_stride * 2;
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xA(void)
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
        P[y] = *sg_stream_ptr++;

    if (P[0] <= P[1]) {

        /* 4-color encoding for each quadrant; need 28 more bytes */
        CHECK_STREAM_PTR(28);

        for (y = 0; y < 4; y++)
            B[y] = *sg_stream_ptr++;
        for (y = 4; y < 16; y += 4) {
            for (x = y; x < y + 4; x++)
                P[x] = *sg_stream_ptr++;
            for (x = y; x < y + 4; x++)
                B[x] = *sg_stream_ptr++;
        }

        for (y = 0; y < 8; y++) {

            lower_half = (y >= 4) ? 4 : 0;
            flags = (B[y + 8] << 8) | B[y];

            for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                split = (x >= 4) ? 8 : 0;
                index = split + lower_half + ((flags >> shifter) & 0x03);
                *sg_output_plane++ = P[index];
            }

            sg_output_plane += sg_line_inc;
        }

    } else {

        /* 4-color encoding for either left and right or top and bottom
         * halves; need 20 more bytes */
        CHECK_STREAM_PTR(20);

        for (y = 0; y < 8; y++)
            B[y] = *sg_stream_ptr++;
        for (y = 4; y < 8; y++)
            P[y] = *sg_stream_ptr++;
        for (y = 8; y < 16; y++)
            B[y] = *sg_stream_ptr++;

        if (P[4] <= P[5]) {

            /* block is divided into left and right halves */
            for (y = 0; y < 8; y++) {

                flags = (B[y + 8] << 8) | B[y];
                split = 0;

                for (x = 0, shifter = 0; x < 8; x++, shifter += 2) {
                    if (x == 4)
                        split = 4;
                    *sg_output_plane++ = P[split + ((flags >> shifter) & 0x03)];
                }

                sg_output_plane += sg_line_inc;
            }

        } else {

            /* block is divided into top and bottom halves */
            split = 0;
            for (y = 0; y < 8; y++) {

                flags = (B[y * 2 + 1] << 8) | B[y * 2];
                if (y == 4)
                    split = 4;

                for (x = 0, shifter = 0; x < 8; x++, shifter += 2)
                    *sg_output_plane++ = P[split + ((flags >> shifter) & 0x03)];

                sg_output_plane += sg_line_inc;
            }
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xB(void)
{
    int x, y;

    /* 64-color encoding (each pixel in block is a different color) */
    CHECK_STREAM_PTR(64);

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *sg_output_plane++ = *sg_stream_ptr++;
        }
        sg_output_plane += sg_line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xC(void)
{
    int x, y;
    unsigned char pix;

    /* 16-color block encoding: each 2x2 block is a different color */
    CHECK_STREAM_PTR(16);

    for (y = 0; y < 8; y += 2) {
        for (x = 0; x < 8; x += 2) {
            pix = *sg_stream_ptr++;
            *(sg_output_plane + x) = pix;
            *(sg_output_plane + x + 1) = pix;
            *(sg_output_plane + sg_stride + x) = pix;
            *(sg_output_plane + sg_stride + x + 1) = pix;
        }
        sg_output_plane += sg_stride * 2;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xD(void)
{
    int x, y;
    unsigned char P[4];
    unsigned char index = 0;

    /* 4-color block encoding: each 4x4 block is a different color */
    CHECK_STREAM_PTR(4);

    for (y = 0; y < 4; y++)
        P[y] = *sg_stream_ptr++;

    for (y = 0; y < 8; y++) {
        if (y < 4)
            index = 0;
        else
            index = 2;

        for (x = 0; x < 8; x++) {
            if (x == 4)
                index++;
            *sg_output_plane++ = P[index];
        }
        sg_output_plane += sg_line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xE(void)
{
    int x, y;
    unsigned char pix;

    /* 1-color encoding: the whole block is 1 solid color */
    CHECK_STREAM_PTR(1);
    pix = *sg_stream_ptr++;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            *sg_output_plane++ = pix;
        }
        sg_output_plane += sg_line_inc;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xF(void)
{
    int x, y;
    unsigned char sample0, sample1;

    /* dithered encoding */
    CHECK_STREAM_PTR(2);
    sample0 = *sg_stream_ptr++;
    sample1 = *sg_stream_ptr++;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x += 2) {
            if (y & 1) {
                *sg_output_plane++ = sample1;
                *sg_output_plane++ = sample0;
            } else {
                *sg_output_plane++ = sample0;
                *sg_output_plane++ = sample1;
            }
        }
        sg_output_plane += sg_line_inc;
    }

    /* report success */
    return 0;
}

static int (*ipvideo_decode_block[16])(void);

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
    if (s->avctx->pix_fmt == PIX_FMT_PAL8)
        memcpy(s->current_frame.data[1], s->palette, PALETTE_COUNT * 4);

    switch (s->avctx->pix_fmt) {

    case PIX_FMT_PAL8:
        sg_stride = s->current_frame.linesize[0];
        sg_stream_ptr = s->buf + 14;  /* data starts 14 bytes in */
        sg_stream_end = s->buf + s->size;
        sg_line_inc = sg_stride - 8;
        sg_current_plane = s->current_frame.data[0];
        sg_last_plane = s->last_frame.data[0];
        sg_second_last_plane = s->second_last_frame.data[0];
        sg_upper_motion_limit_offset = (s->avctx->height - 8) * sg_stride
            + s->avctx->width - 8;
        sg_dsp = s->dsp;

        for (y = 0; y < (sg_stride * s->avctx->height); y += sg_stride * 8) {
            for (x = y; x < y + s->avctx->width; x += 8) {
                /* bottom nibble first, then top nibble (which makes it
                 * hard to use a GetBitcontext) */
                if (index & 1)
                    opcode = s->decoding_map[index >> 1] >> 4;
                else
                    opcode = s->decoding_map[index >> 1] & 0xF;
                index++;

                debug_interplay("  block @ (%3d, %3d): encoding 0x%X, data ptr @ %p\n",
                    x - y, y / sg_stride, opcode, sg_stream_ptr);
                code_counts[opcode]++;

                sg_output_plane = sg_current_plane + x;
                ret = ipvideo_decode_block[opcode]();
                if (ret != 0) {
                    printf(" Interplay video: decode problem on frame %d, @ block (%d, %d)\n",
                        frame, x - y, y / sg_stride);
                    return;
                }
            }
        }
        if ((sg_stream_ptr != sg_stream_end) &&
            (sg_stream_ptr + 1 != sg_stream_end)) {
            printf (" Interplay video: decode finished with %d bytes left over\n",
                sg_stream_end - sg_stream_ptr);
        }
        break;

    default:
        printf ("Interplay video: Unhandled video format\n");
        break;
    }

}

static int ipvideo_decode_init(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    s->avctx = avctx;

    if (s->avctx->extradata_size != sizeof(AVPaletteControl)) {
        printf (" Interplay video: expected extradata_size of %d\n",
            sizeof(AVPaletteControl));
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
    AVPaletteControl *palette_control = (AVPaletteControl *)avctx->extradata;

    if (palette_control->palette_changed) {
        /* load the new palette and reset the palette control */
        ipvideo_new_palette(s, palette_control->palette);
        palette_control->palette_changed = 0;
    }

    s->decoding_map = buf;
    s->buf = buf + s->decoding_map_size;
    s->size = buf_size - s->decoding_map_size;

    s->current_frame.reference = 3;
    if (avctx->get_buffer(avctx, &s->current_frame)) {
        printf ("  Interplay Video: get_buffer() failed\n");
        return -1;
    }

    ipvideo_decode_opcodes(s);

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
