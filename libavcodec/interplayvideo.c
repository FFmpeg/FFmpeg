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
 * @file libavcodec/interplayvideo.c
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

#include "avcodec.h"
#include "bytestream.h"
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
    const unsigned char *decoding_map;
    int decoding_map_size;

    const unsigned char *buf;
    int size;

    const unsigned char *stream_ptr;
    const unsigned char *stream_end;
    unsigned char *pixel_ptr;
    int line_inc;
    int stride;
    int upper_motion_limit_offset;

} IpvideoContext;

#define CHECK_STREAM_PTR(n) \
  if (s->stream_end - s->stream_ptr < n) { \
    av_log(s->avctx, AV_LOG_ERROR, "Interplay video warning: stream_ptr out of bounds (%p >= %p)\n", \
      s->stream_ptr + n, s->stream_end); \
    return -1; \
  }

static int copy_from(IpvideoContext *s, AVFrame *src, int delta_x, int delta_y)
{
    int current_offset = s->pixel_ptr - s->current_frame.data[0];
    int motion_offset = current_offset + delta_y * s->stride + delta_x;
    if (motion_offset < 0) {
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset < 0 (%d)\n", motion_offset);
        return -1;
    } else if (motion_offset > s->upper_motion_limit_offset) {
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: motion offset above limit (%d >= %d)\n",
            motion_offset, s->upper_motion_limit_offset);
        return -1;
    }
    s->dsp.put_pixels_tab[1][0](s->pixel_ptr, src->data[0] + motion_offset, s->stride, 8);
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
    return copy_from(s, &s->second_last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x3(IpvideoContext *s)
{
    unsigned char B;
    int x, y;

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
    return copy_from(s, &s->current_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x4(IpvideoContext *s)
{
    int x, y;
    unsigned char B, BL, BH;

    /* copy a block from the previous frame; need 1 more byte */
    CHECK_STREAM_PTR(1);

    B = *s->stream_ptr++;
    BL = B & 0x0F;
    BH = (B >> 4) & 0x0F;
    x = -8 + BL;
    y = -8 + BH;

    debug_interplay ("    motion byte = %d, (x, y) = (%d, %d)\n", B, x, y);
    return copy_from(s, &s->last_frame, x, y);
}

static int ipvideo_decode_block_opcode_0x5(IpvideoContext *s)
{
    signed char x, y;

    /* copy a block from the previous frame using an expanded range;
     * need 2 more bytes */
    CHECK_STREAM_PTR(2);

    x = *s->stream_ptr++;
    y = *s->stream_ptr++;

    debug_interplay ("    motion bytes = %d, %d\n", x, y);
    return copy_from(s, &s->last_frame, x, y);
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
    unsigned char P[2];
    unsigned int flags;

    /* 2-color encoding */
    CHECK_STREAM_PTR(2);

    P[0] = *s->stream_ptr++;
    P[1] = *s->stream_ptr++;

    if (P[0] <= P[1]) {

        /* need 8 more bytes from the stream */
        CHECK_STREAM_PTR(8);

        for (y = 0; y < 8; y++) {
            flags = *s->stream_ptr++ | 0x100;
            for (; flags != 1; flags >>= 1)
                *s->pixel_ptr++ = P[flags & 1];
            s->pixel_ptr += s->line_inc;
        }

    } else {

        /* need 2 more bytes from the stream */
        CHECK_STREAM_PTR(2);

        flags = bytestream_get_le16(&s->stream_ptr);
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
    unsigned char P[2];
    unsigned int flags = 0;

    /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
     * either top and bottom or left and right halves */
    CHECK_STREAM_PTR(2);

    P[0] = *s->stream_ptr++;
    P[1] = *s->stream_ptr++;

    if (P[0] <= P[1]) {

        CHECK_STREAM_PTR(14);
        s->stream_ptr -= 2;

        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                P[0] = *s->stream_ptr++; P[1] = *s->stream_ptr++;
                flags = bytestream_get_le16(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 1)
                *s->pixel_ptr++ = P[flags & 1];
            s->pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
        }

    } else {

        /* need 10 more bytes */
        CHECK_STREAM_PTR(10);

        if (s->stream_ptr[4] <= s->stream_ptr[5]) {

            flags = bytestream_get_le32(&s->stream_ptr);

            /* vertical split; left & right halves are 2-color encoded */

            for (y = 0; y < 16; y++) {
                for (x = 0; x < 4; x++, flags >>= 1)
                    *s->pixel_ptr++ = P[flags & 1];
                s->pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) {
                    s->pixel_ptr -= 8 * s->stride - 4;
                    P[0] = *s->stream_ptr++; P[1] = *s->stream_ptr++;
                    flags = bytestream_get_le32(&s->stream_ptr);
                }
            }

        } else {

            /* horizontal split; top & bottom halves are 2-color encoded */

            for (y = 0; y < 8; y++) {
                if (y == 4) {
                    P[0] = *s->stream_ptr++;
                    P[1] = *s->stream_ptr++;
                }
                flags = *s->stream_ptr++ | 0x100;

                for (; flags != 1; flags >>= 1)
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
    CHECK_STREAM_PTR(4);

    memcpy(P, s->stream_ptr, 4);
    s->stream_ptr += 4;

    if (P[0] <= P[1]) {
        if (P[2] <= P[3]) {

            /* 1 of 4 colors for each pixel, need 16 more bytes */
            CHECK_STREAM_PTR(16);

            for (y = 0; y < 8; y++) {
                /* get the next set of 8 2-bit flags */
                int flags = bytestream_get_le16(&s->stream_ptr);
                for (x = 0; x < 8; x++, flags >>= 2)
                    *s->pixel_ptr++ = P[flags & 0x03];
                s->pixel_ptr += s->line_inc;
            }

        } else {
            uint32_t flags;

            /* 1 of 4 colors for each 2x2 block, need 4 more bytes */
            CHECK_STREAM_PTR(4);

            flags = bytestream_get_le32(&s->stream_ptr);

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
        CHECK_STREAM_PTR(8);

        flags = bytestream_get_le64(&s->stream_ptr);
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
    unsigned char P[4];
    int flags = 0;

    /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
     * either top and bottom or left and right halves */
    CHECK_STREAM_PTR(24);

    if (s->stream_ptr[0] <= s->stream_ptr[1]) {

        /* 4-color encoding for each quadrant; need 32 bytes */
        CHECK_STREAM_PTR(32);

        for (y = 0; y < 16; y++) {
            // new values for each 4x4 block
            if (!(y & 3)) {
                memcpy(P, s->stream_ptr, 4);
                s->stream_ptr += 4;
                flags = bytestream_get_le32(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 2)
                *s->pixel_ptr++ = P[flags & 0x03];

            s->pixel_ptr += s->stride - 4;
            // switch to right half
            if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
        }

    } else {
        // vertical split?
        int vert = s->stream_ptr[12] <= s->stream_ptr[13];
        uint64_t flags = 0;

        /* 4-color encoding for either left and right or top and bottom
         * halves */

        for (y = 0; y < 16; y++) {
            // load values for each half
            if (!(y & 7)) {
                memcpy(P, s->stream_ptr, 4);
                s->stream_ptr += 4;
                flags = bytestream_get_le64(&s->stream_ptr);
            }

            for (x = 0; x < 4; x++, flags >>= 2)
                *s->pixel_ptr++ = P[flags & 0x03];

            if (vert) {
                s->pixel_ptr += s->stride - 4;
                // switch to right half
                if (y == 7) s->pixel_ptr -= 8 * s->stride - 4;
            } else if (y & 1) s->pixel_ptr += s->line_inc;
        }
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xB(IpvideoContext *s)
{
    int y;

    /* 64-color encoding (each pixel in block is a different color) */
    CHECK_STREAM_PTR(64);

    for (y = 0; y < 8; y++) {
        memcpy(s->pixel_ptr, s->stream_ptr, 8);
        s->stream_ptr += 8;
        s->pixel_ptr  += s->stride;
    }

    /* report success */
    return 0;
}

static int ipvideo_decode_block_opcode_0xC(IpvideoContext *s)
{
    int x, y;

    /* 16-color block encoding: each 2x2 block is a different color */
    CHECK_STREAM_PTR(16);

    for (y = 0; y < 8; y += 2) {
        for (x = 0; x < 8; x += 2) {
            s->pixel_ptr[x                ] =
            s->pixel_ptr[x + 1            ] =
            s->pixel_ptr[x +     s->stride] =
            s->pixel_ptr[x + 1 + s->stride] = *s->stream_ptr++;
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
    CHECK_STREAM_PTR(4);

    for (y = 0; y < 8; y++) {
        if (!(y & 3)) {
            P[0] = *s->stream_ptr++;
            P[1] = *s->stream_ptr++;
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
    CHECK_STREAM_PTR(1);
    pix = *s->stream_ptr++;

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
    CHECK_STREAM_PTR(2);
    sample[0] = *s->stream_ptr++;
    sample[1] = *s->stream_ptr++;

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

static void ipvideo_decode_opcodes(IpvideoContext *s)
{
    int x, y;
    int index = 0;
    unsigned char opcode;
    int ret;
    int code_counts[16] = {0};
    static int frame = 0;

    debug_interplay("------------------ frame %d\n", frame);
    frame++;

    /* this is PAL8, so make the palette available */
    memcpy(s->current_frame.data[1], s->avctx->palctrl->palette, PALETTE_COUNT * 4);

    s->stride = s->current_frame.linesize[0];
    s->stream_ptr = s->buf + 14;  /* data starts 14 bytes in */
    s->stream_end = s->buf + s->size;
    s->line_inc = s->stride - 8;
    s->upper_motion_limit_offset = (s->avctx->height - 8) * s->stride
        + s->avctx->width - 8;

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
    if (s->stream_end - s->stream_ptr > 1) {
        av_log(s->avctx, AV_LOG_ERROR, " Interplay video: decode finished with %td bytes left over\n",
            s->stream_end - s->stream_ptr);
    }
}

static av_cold int ipvideo_decode_init(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    s->avctx = avctx;

    if (s->avctx->palctrl == NULL) {
        av_log(avctx, AV_LOG_ERROR, " Interplay video: palette expected.\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;
    dsputil_init(&s->dsp, avctx);

    /* decoding map contains 4 bits of information per 8x8 block */
    s->decoding_map_size = avctx->width * avctx->height / (8 * 8 * 2);

    s->current_frame.data[0] = s->last_frame.data[0] =
    s->second_last_frame.data[0] = NULL;

    return 0;
}

static int ipvideo_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
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
    .long_name = NULL_IF_CONFIG_SMALL("Interplay MVE video"),
};
