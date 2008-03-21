/*
 * Wing Commander/Xan Video Decoder
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
 * @file xan.c
 * Xan video decoder for Wing Commander III computer game
 * by Mario Brito (mbrito@student.dei.uc.pt)
 * and Mike Melanson (melanson@pcisys.net)
 *
 * The xan_wc3 decoder outputs PAL8 data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avcodec.h"

typedef struct XanContext {

    AVCodecContext *avctx;
    AVFrame last_frame;
    AVFrame current_frame;

    const unsigned char *buf;
    int size;

    /* scratch space */
    unsigned char *buffer1;
    int buffer1_size;
    unsigned char *buffer2;
    int buffer2_size;

    int frame_size;

} XanContext;

static av_cold int xan_decode_init(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->frame_size = 0;

    if ((avctx->codec->id == CODEC_ID_XAN_WC3) &&
        (s->avctx->palctrl == NULL)) {
        av_log(avctx, AV_LOG_ERROR, " WC3 Xan video: palette expected.\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;

    if(avcodec_check_dimensions(avctx, avctx->width, avctx->height))
        return -1;

    s->buffer1_size = avctx->width * avctx->height;
    s->buffer1 = av_malloc(s->buffer1_size);
    s->buffer2_size = avctx->width * avctx->height;
    s->buffer2 = av_malloc(s->buffer2_size);
    if (!s->buffer1 || !s->buffer2)
        return -1;

    return 0;
}

/* This function is used in lieu of memcpy(). This decoder cannot use
 * memcpy because the memory locations often overlap and
 * memcpy doesn't like that; it's not uncommon, for example, for
 * dest = src+1, to turn byte A into  pattern AAAAAAAA.
 * This was originally repz movsb in Intel x86 ASM. */
static inline void bytecopy(unsigned char *dest, const unsigned char *src, int count)
{
    int i;

    for (i = 0; i < count; i++)
        dest[i] = src[i];
}

static int xan_huffman_decode(unsigned char *dest, const unsigned char *src,
    int dest_len)
{
    unsigned char byte = *src++;
    unsigned char ival = byte + 0x16;
    const unsigned char * ptr = src + byte*2;
    unsigned char val = ival;
    int counter = 0;
    unsigned char *dest_end = dest + dest_len;

    unsigned char bits = *ptr++;

    while ( val != 0x16 ) {
        if ( (1 << counter) & bits )
            val = src[byte + val - 0x17];
        else
            val = src[val - 0x17];

        if ( val < 0x16 ) {
            if (dest + 1 > dest_end)
                return 0;
            *dest++ = val;
            val = ival;
        }

        if (counter++ == 7) {
            counter = 0;
            bits = *ptr++;
        }
    }

    return 0;
}

static void xan_unpack(unsigned char *dest, const unsigned char *src, int dest_len)
{
    unsigned char opcode;
    int size;
    int offset;
    int byte1, byte2, byte3;
    unsigned char *dest_end = dest + dest_len;

    for (;;) {
        opcode = *src++;

        if ( (opcode & 0x80) == 0 ) {

            offset = *src++;

            size = opcode & 3;
            if (dest + size > dest_end)
                return;
            bytecopy(dest, src, size);  dest += size;  src += size;

            size = ((opcode & 0x1c) >> 2) + 3;
            if (dest + size > dest_end)
                return;
            bytecopy (dest, dest - (((opcode & 0x60) << 3) + offset + 1), size);
            dest += size;

        } else if ( (opcode & 0x40) == 0 ) {

            byte1 = *src++;
            byte2 = *src++;

            size = byte1 >> 6;
            if (dest + size > dest_end)
                return;
            bytecopy (dest, src, size);  dest += size;  src += size;

            size = (opcode & 0x3f) + 4;
            if (dest + size > dest_end)
                return;
            bytecopy (dest, dest - (((byte1 & 0x3f) << 8) + byte2 + 1), size);
            dest += size;

        } else if ( (opcode & 0x20) == 0 ) {

            byte1 = *src++;
            byte2 = *src++;
            byte3 = *src++;

            size = opcode & 3;
            if (dest + size > dest_end)
                return;
            bytecopy (dest, src, size);  dest += size;  src += size;

            size = byte3 + 5 + ((opcode & 0xc) << 6);
            if (dest + size > dest_end)
                return;
            bytecopy (dest,
                dest - ((((opcode & 0x10) >> 4) << 0x10) + 1 + (byte1 << 8) + byte2),
                size);
            dest += size;
        } else {
            size = ((opcode & 0x1f) << 2) + 4;

            if (size > 0x70)
                break;

            if (dest + size > dest_end)
                return;
            bytecopy (dest, src, size);  dest += size;  src += size;
        }
    }

    size = opcode & 3;
    bytecopy(dest, src, size);  dest += size;  src += size;
}

static inline void xan_wc3_output_pixel_run(XanContext *s,
    const unsigned char *pixel_buffer, int x, int y, int pixel_count)
{
    int stride;
    int line_inc;
    int index;
    int current_x;
    int width = s->avctx->width;
    unsigned char *palette_plane;

    palette_plane = s->current_frame.data[0];
    stride = s->current_frame.linesize[0];
    line_inc = stride - width;
    index = y * stride + x;
    current_x = x;
    while((pixel_count--) && (index < s->frame_size)) {

        /* don't do a memcpy() here; keyframes generally copy an entire
         * frame of data and the stride needs to be accounted for */
        palette_plane[index++] = *pixel_buffer++;

        current_x++;
        if (current_x >= width) {
            index += line_inc;
            current_x = 0;
        }
    }
}

static inline void xan_wc3_copy_pixel_run(XanContext *s,
    int x, int y, int pixel_count, int motion_x, int motion_y)
{
    int stride;
    int line_inc;
    int curframe_index, prevframe_index;
    int curframe_x, prevframe_x;
    int width = s->avctx->width;
    unsigned char *palette_plane, *prev_palette_plane;

    palette_plane = s->current_frame.data[0];
    prev_palette_plane = s->last_frame.data[0];
    stride = s->current_frame.linesize[0];
    line_inc = stride - width;
    curframe_index = y * stride + x;
    curframe_x = x;
    prevframe_index = (y + motion_y) * stride + x + motion_x;
    prevframe_x = x + motion_x;
    while((pixel_count--) && (curframe_index < s->frame_size)) {

        palette_plane[curframe_index++] =
            prev_palette_plane[prevframe_index++];

        curframe_x++;
        if (curframe_x >= width) {
            curframe_index += line_inc;
            curframe_x = 0;
        }

        prevframe_x++;
        if (prevframe_x >= width) {
            prevframe_index += line_inc;
            prevframe_x = 0;
        }
    }
}

static void xan_wc3_decode_frame(XanContext *s) {

    int width = s->avctx->width;
    int height = s->avctx->height;
    int total_pixels = width * height;
    unsigned char opcode;
    unsigned char flag = 0;
    int size = 0;
    int motion_x, motion_y;
    int x, y;

    unsigned char *opcode_buffer = s->buffer1;
    int opcode_buffer_size = s->buffer1_size;
    const unsigned char *imagedata_buffer = s->buffer2;

    /* pointers to segments inside the compressed chunk */
    const unsigned char *huffman_segment;
    const unsigned char *size_segment;
    const unsigned char *vector_segment;
    const unsigned char *imagedata_segment;

    huffman_segment =   s->buf + AV_RL16(&s->buf[0]);
    size_segment =      s->buf + AV_RL16(&s->buf[2]);
    vector_segment =    s->buf + AV_RL16(&s->buf[4]);
    imagedata_segment = s->buf + AV_RL16(&s->buf[6]);

    xan_huffman_decode(opcode_buffer, huffman_segment, opcode_buffer_size);

    if (imagedata_segment[0] == 2)
        xan_unpack(s->buffer2, &imagedata_segment[1], s->buffer2_size);
    else
        imagedata_buffer = &imagedata_segment[1];

    /* use the decoded data segments to build the frame */
    x = y = 0;
    while (total_pixels) {

        opcode = *opcode_buffer++;
        size = 0;

        switch (opcode) {

        case 0:
            flag ^= 1;
            continue;

        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            size = opcode;
            break;

        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
            size += (opcode - 10);
            break;

        case 9:
        case 19:
            size = *size_segment++;
            break;

        case 10:
        case 20:
            size = AV_RB16(&size_segment[0]);
            size_segment += 2;
            break;

        case 11:
        case 21:
            size = AV_RB24(size_segment);
            size_segment += 3;
            break;
        }

        if (opcode < 12) {
            flag ^= 1;
            if (flag) {
                /* run of (size) pixels is unchanged from last frame */
                xan_wc3_copy_pixel_run(s, x, y, size, 0, 0);
            } else {
                /* output a run of pixels from imagedata_buffer */
                xan_wc3_output_pixel_run(s, imagedata_buffer, x, y, size);
                imagedata_buffer += size;
            }
        } else {
            /* run-based motion compensation from last frame */
            motion_x = (*vector_segment >> 4) & 0xF;
            motion_y = *vector_segment & 0xF;
            vector_segment++;

            /* sign extension */
            if (motion_x & 0x8)
                motion_x |= 0xFFFFFFF0;
            if (motion_y & 0x8)
                motion_y |= 0xFFFFFFF0;

            /* copy a run of pixels from the previous frame */
            xan_wc3_copy_pixel_run(s, x, y, size, motion_x, motion_y);

            flag = 0;
        }

        /* coordinate accounting */
        total_pixels -= size;
        while (size) {
            if (x + size >= width) {
                y++;
                size -= (width - x);
                x = 0;
            } else {
                x += size;
                size = 0;
            }
        }
    }
}

static void xan_wc4_decode_frame(XanContext *s) {
}

static int xan_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    XanContext *s = avctx->priv_data;
    AVPaletteControl *palette_control = avctx->palctrl;

    if (avctx->get_buffer(avctx, &s->current_frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "  Xan Video: get_buffer() failed\n");
        return -1;
    }
    s->current_frame.reference = 3;

    if (!s->frame_size)
        s->frame_size = s->current_frame.linesize[0] * s->avctx->height;

    palette_control->palette_changed = 0;
    memcpy(s->current_frame.data[1], palette_control->palette,
        AVPALETTE_SIZE);
    s->current_frame.palette_has_changed = 1;

    s->buf = buf;
    s->size = buf_size;

    if (avctx->codec->id == CODEC_ID_XAN_WC3)
        xan_wc3_decode_frame(s);
    else if (avctx->codec->id == CODEC_ID_XAN_WC4)
        xan_wc4_decode_frame(s);

    /* release the last frame if it is allocated */
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->current_frame;

    /* shuffle frames */
    FFSWAP(AVFrame, s->current_frame, s->last_frame);

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int xan_decode_end(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    /* release the frames */
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);
    if (s->current_frame.data[0])
        avctx->release_buffer(avctx, &s->current_frame);

    av_free(s->buffer1);
    av_free(s->buffer2);

    return 0;
}

AVCodec xan_wc3_decoder = {
    "xan_wc3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_XAN_WC3,
    sizeof(XanContext),
    xan_decode_init,
    NULL,
    xan_decode_end,
    xan_decode_frame,
    CODEC_CAP_DR1,
};

/*
AVCodec xan_wc4_decoder = {
    "xan_wc4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_XAN_WC4,
    sizeof(XanContext),
    xan_decode_init,
    NULL,
    xan_decode_end,
    xan_decode_frame,
    CODEC_CAP_DR1,
};
*/
