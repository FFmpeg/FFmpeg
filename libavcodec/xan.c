/*
 * Wing Commander/Xan Video Decoder
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
 * @file xan.c
 * Xan video decoder for Wing Commander III & IV computer games
 * by Mario Brito (mbrito@student.dei.uc.pt)
 * and Mike Melanson (melanson@pcisys.net)
 *
 * The xan_wc3 decoder outputs the following colorspaces natively:
 *   PAL8 (default), RGB555, RGB565, RGB24, BGR24, RGBA32, YUV444P
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#define PALETTE_COUNT 256
#define PALETTE_CONTROL_SIZE ((256 * 3) + 1)

typedef struct XanContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame last_frame;
    AVFrame current_frame;

    unsigned char *buf;
    int size;

    unsigned char palette[PALETTE_COUNT * 4];

    /* scratch space */
    unsigned char *buffer1;
    unsigned char *buffer2;

} XanContext;

/* RGB -> YUV conversion stuff */
#define SCALEFACTOR 65536
#define CENTERSAMPLE 128

#define COMPUTE_Y(r, g, b) \
  (unsigned char) \
  ((y_r_table[r] + y_g_table[g] + y_b_table[b]) / SCALEFACTOR)
#define COMPUTE_U(r, g, b) \
  (unsigned char) \
  ((u_r_table[r] + u_g_table[g] + u_b_table[b]) / SCALEFACTOR + CENTERSAMPLE)
#define COMPUTE_V(r, g, b) \
  (unsigned char) \
  ((v_r_table[r] + v_g_table[g] + v_b_table[b]) / SCALEFACTOR + CENTERSAMPLE)

#define Y_R (SCALEFACTOR *  0.29900)
#define Y_G (SCALEFACTOR *  0.58700)
#define Y_B (SCALEFACTOR *  0.11400)

#define U_R (SCALEFACTOR * -0.16874)
#define U_G (SCALEFACTOR * -0.33126)
#define U_B (SCALEFACTOR *  0.50000)

#define V_R (SCALEFACTOR *  0.50000)
#define V_G (SCALEFACTOR * -0.41869)
#define V_B (SCALEFACTOR * -0.08131)

/*
 * Precalculate all of the YUV tables since it requires fewer than
 * 10 kilobytes to store them.
 */
static int y_r_table[256];
static int y_g_table[256];
static int y_b_table[256];

static int u_r_table[256];
static int u_g_table[256];
static int u_b_table[256];

static int v_r_table[256];
static int v_g_table[256];
static int v_b_table[256];

static int xan_decode_init(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;

    if ((avctx->codec->id == CODEC_ID_XAN_WC3) && 
        (s->avctx->palctrl == NULL)) {
        av_log(avctx, AV_LOG_ERROR, " WC3 Xan video: palette expected.\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    /* initialize the RGB -> YUV tables */
    for (i = 0; i < 256; i++) {
        y_r_table[i] = Y_R * i;
        y_g_table[i] = Y_G * i;
        y_b_table[i] = Y_B * i;

        u_r_table[i] = U_R * i;
        u_g_table[i] = U_G * i;
        u_b_table[i] = U_B * i;

        v_r_table[i] = V_R * i;
        v_g_table[i] = V_G * i;
        v_b_table[i] = V_B * i;
    }

    s->buffer1 = av_malloc(avctx->width * avctx->height);
    s->buffer2 = av_malloc(avctx->width * avctx->height);
    if (!s->buffer1 || !s->buffer2)
        return -1;

    return 0;
}

/* This function is used in lieu of memcpy(). This decoder can not use 
 * memcpy because the memory locations often overlap and
 * memcpy doesn't like that; it's not uncommon, for example, for
 * dest = src+1, to turn byte A into  pattern AAAAAAAA.
 * This was originally repz movsb in Intel x86 ASM. */
static inline void bytecopy(unsigned char *dest, unsigned char *src, int count)
{
    int i;

    for (i = 0; i < count; i++)
        dest[i] = src[i];
}

static int xan_huffman_decode(unsigned char *dest, unsigned char *src)
{
    unsigned char byte = *src++;
    unsigned char ival = byte + 0x16;
    unsigned char * ptr = src + byte*2;
    unsigned char val = ival;
    int counter = 0;

    unsigned char bits = *ptr++;

    while ( val != 0x16 ) {
        if ( (1 << counter) & bits )
            val = src[byte + val - 0x17];
        else
            val = src[val - 0x17];

        if ( val < 0x16 ) {
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

static void xan_unpack(unsigned char *dest, unsigned char *src)
{
    unsigned char opcode;
    int size;
    int offset;
    int byte1, byte2, byte3;

    for (;;) {
        opcode = *src++;

        if ( (opcode & 0x80) == 0 ) {

            offset = *src++;

            size = opcode & 3;
            bytecopy(dest, src, size);  dest += size;  src += size;

            size = ((opcode & 0x1c) >> 2) + 3;
            bytecopy (dest, dest - (((opcode & 0x60) << 3) + offset + 1), size);
            dest += size;

        } else if ( (opcode & 0x40) == 0 ) {

            byte1 = *src++;
            byte2 = *src++;

            size = byte1 >> 6;
            bytecopy (dest, src, size);  dest += size;  src += size;

            size = (opcode & 0x3f) + 4;
            bytecopy (dest, dest - (((byte1 & 0x3f) << 8) + byte2 + 1), size);
            dest += size;

        } else if ( (opcode & 0x20) == 0 ) {

            byte1 = *src++;
            byte2 = *src++;
            byte3 = *src++;

            size = opcode & 3;
            bytecopy (dest, src, size);  dest += size;  src += size;

            size = byte3 + 5 + ((opcode & 0xc) << 6);
            bytecopy (dest,
                dest - ((((opcode & 0x10) >> 4) << 0x10) + 1 + (byte1 << 8) + byte2),
                size);
            dest += size;
        } else {
            size = ((opcode & 0x1f) << 2) + 4;

            if (size > 0x70)
                break;

            bytecopy (dest, src, size);  dest += size;  src += size;
        }
    }

    size = opcode & 3;
    bytecopy(dest, src, size);  dest += size;  src += size;
}

static void inline xan_wc3_build_palette(XanContext *s, 
    unsigned int *palette_data)
{
    int i;
    unsigned char r, g, b;
    unsigned short *palette16;
    unsigned int *palette32;
    unsigned int pal_elem;

    /* transform the palette passed through the palette control structure
     * into the necessary internal format depending on colorspace */

    switch (s->avctx->pix_fmt) {

    case PIX_FMT_RGB555:
        palette16 = (unsigned short *)s->palette;
        for (i = 0; i < PALETTE_COUNT; i++) {
            pal_elem = palette_data[i];
            r = (pal_elem >> 16) & 0xff;
            g = (pal_elem >> 8) & 0xff;
            b = pal_elem & 0xff;
            palette16[i] = 
                ((r >> 3) << 10) |
                ((g >> 3) <<  5) |
                ((b >> 3) <<  0);
        }
        break;

    case PIX_FMT_RGB565:
        palette16 = (unsigned short *)s->palette;
        for (i = 0; i < PALETTE_COUNT; i++) {
            pal_elem = palette_data[i];
            r = (pal_elem >> 16) & 0xff;
            g = (pal_elem >> 8) & 0xff;
            b = pal_elem & 0xff;
            palette16[i] = 
                ((r >> 3) << 11) |
                ((g >> 2) <<  5) |
                ((b >> 3) <<  0);
        }
        break;

    case PIX_FMT_RGB24:
        for (i = 0; i < PALETTE_COUNT; i++) {
            pal_elem = palette_data[i];
            r = (pal_elem >> 16) & 0xff;
            g = (pal_elem >> 8) & 0xff;
            b = pal_elem & 0xff;
            s->palette[i * 4 + 0] = r;
            s->palette[i * 4 + 1] = g;
            s->palette[i * 4 + 2] = b;
        }
        break;

    case PIX_FMT_BGR24:
        for (i = 0; i < PALETTE_COUNT; i++) {
            pal_elem = palette_data[i];
            r = (pal_elem >> 16) & 0xff;
            g = (pal_elem >> 8) & 0xff;
            b = pal_elem & 0xff;
            s->palette[i * 4 + 0] = b;
            s->palette[i * 4 + 1] = g;
            s->palette[i * 4 + 2] = r;
        }
        break;

    case PIX_FMT_PAL8:
    case PIX_FMT_RGBA32:
        palette32 = (unsigned int *)s->palette;
        memcpy (palette32, palette_data, PALETTE_COUNT * sizeof(unsigned int));
        break;

    case PIX_FMT_YUV444P:
        for (i = 0; i < PALETTE_COUNT; i++) {
            pal_elem = palette_data[i];
            r = (pal_elem >> 16) & 0xff;
            g = (pal_elem >> 8) & 0xff;
            b = pal_elem & 0xff;
            s->palette[i * 4 + 0] = COMPUTE_Y(r, g, b);
            s->palette[i * 4 + 1] = COMPUTE_U(r, g, b);
            s->palette[i * 4 + 2] = COMPUTE_V(r, g, b);
        }
        break;

    default:
        av_log(s->avctx, AV_LOG_ERROR, " Xan WC3: Unhandled colorspace\n");
        break;
    }
}

/* advance current_x variable; reset accounting variables if current_x
 * moves beyond width */
#define ADVANCE_CURRENT_X() \
    current_x++; \
    if (current_x >= width) { \
        index += line_inc; \
        current_x = 0; \
    }

static void inline xan_wc3_output_pixel_run(XanContext *s, 
    unsigned char *pixel_buffer, int x, int y, int pixel_count)
{
    int stride;
    int line_inc;
    int index;
    int current_x;
    int width = s->avctx->width;
    unsigned char pix;
    unsigned char *palette_plane;
    unsigned char *y_plane;
    unsigned char *u_plane;
    unsigned char *v_plane;
    unsigned char *rgb_plane;
    unsigned short *rgb16_plane;
    unsigned short *palette16;
    unsigned int *rgb32_plane;
    unsigned int *palette32;

    switch (s->avctx->pix_fmt) {

    case PIX_FMT_PAL8:
        palette_plane = s->current_frame.data[0];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width;
        index = y * stride + x;
        current_x = x;
        while(pixel_count--) {

            /* don't do a memcpy() here; keyframes generally copy an entire
             * frame of data and the stride needs to be accounted for */
            palette_plane[index++] = *pixel_buffer++;

            ADVANCE_CURRENT_X();
        }
        break;

    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
        rgb16_plane = (unsigned short *)s->current_frame.data[0];
        palette16 = (unsigned short *)s->palette;
        stride = s->current_frame.linesize[0] / 2;
        line_inc = stride - width;
        index = y * stride + x;
        current_x = x;
        while(pixel_count--) {

            rgb16_plane[index++] = palette16[*pixel_buffer++];

            ADVANCE_CURRENT_X();
        }
        break;

    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        rgb_plane = s->current_frame.data[0];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width * 3;
        index = y * stride + x * 3;
        current_x = x;
        while(pixel_count--) {
            pix = *pixel_buffer++;

            rgb_plane[index++] = s->palette[pix * 4 + 0];
            rgb_plane[index++] = s->palette[pix * 4 + 1];
            rgb_plane[index++] = s->palette[pix * 4 + 2];

            ADVANCE_CURRENT_X();
        }
        break;

    case PIX_FMT_RGBA32:
        rgb32_plane = (unsigned int *)s->current_frame.data[0];
        palette32 = (unsigned int *)s->palette;
        stride = s->current_frame.linesize[0] / 4;
        line_inc = stride - width;
        index = y * stride + x;
        current_x = x;
        while(pixel_count--) {

            rgb32_plane[index++] = palette32[*pixel_buffer++];

            ADVANCE_CURRENT_X();
        }
        break;

    case PIX_FMT_YUV444P:
        y_plane = s->current_frame.data[0];
        u_plane = s->current_frame.data[1];
        v_plane = s->current_frame.data[2];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width;
        index = y * stride + x;
        current_x = x;
        while(pixel_count--) {
            pix = *pixel_buffer++;

            y_plane[index] = s->palette[pix * 4 + 0];
            u_plane[index] = s->palette[pix * 4 + 1];
            v_plane[index] = s->palette[pix * 4 + 2];

            index++;
            ADVANCE_CURRENT_X();
        }
        break;

    default:
        av_log(s->avctx, AV_LOG_ERROR, " Xan WC3: Unhandled colorspace\n");
        break;
    }
}

#define ADVANCE_CURFRAME_X() \
    curframe_x++; \
    if (curframe_x >= width) { \
        curframe_index += line_inc; \
        curframe_x = 0; \
    }

#define ADVANCE_PREVFRAME_X() \
    prevframe_x++; \
    if (prevframe_x >= width) { \
        prevframe_index += line_inc; \
        prevframe_x = 0; \
    }

static void inline xan_wc3_copy_pixel_run(XanContext *s, 
    int x, int y, int pixel_count, int motion_x, int motion_y)
{
    int stride;
    int line_inc;
    int curframe_index, prevframe_index;
    int curframe_x, prevframe_x;
    int width = s->avctx->width;
    unsigned char *palette_plane, *prev_palette_plane;
    unsigned char *y_plane, *u_plane, *v_plane;
    unsigned char *prev_y_plane, *prev_u_plane, *prev_v_plane;
    unsigned char *rgb_plane, *prev_rgb_plane;
    unsigned short *rgb16_plane, *prev_rgb16_plane;
    unsigned int *rgb32_plane, *prev_rgb32_plane;

    switch (s->avctx->pix_fmt) {

    case PIX_FMT_PAL8:
        palette_plane = s->current_frame.data[0];
        prev_palette_plane = s->last_frame.data[0];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width;
        curframe_index = y * stride + x;
        curframe_x = x;
        prevframe_index = (y + motion_y) * stride + x + motion_x;
        prevframe_x = x + motion_x;
        while(pixel_count--) {

            palette_plane[curframe_index++] = 
                prev_palette_plane[prevframe_index++];

            ADVANCE_CURFRAME_X();
            ADVANCE_PREVFRAME_X();
        }
        break;

    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
        rgb16_plane = (unsigned short *)s->current_frame.data[0];
        prev_rgb16_plane = (unsigned short *)s->last_frame.data[0];
        stride = s->current_frame.linesize[0] / 2;
        line_inc = stride - width;
        curframe_index = y * stride + x;
        curframe_x = x;
        prevframe_index = (y + motion_y) * stride + x + motion_x;
        prevframe_x = x + motion_x;
        while(pixel_count--) {

            rgb16_plane[curframe_index++] = 
                prev_rgb16_plane[prevframe_index++];

            ADVANCE_CURFRAME_X();
            ADVANCE_PREVFRAME_X();
        }
        break;

    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        rgb_plane = s->current_frame.data[0];
        prev_rgb_plane = s->last_frame.data[0];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width * 3;
        curframe_index = y * stride + x * 3;
        curframe_x = x;
        prevframe_index = (y + motion_y) * stride + 
            (3 * (x + motion_x));
        prevframe_x = x + motion_x;
        while(pixel_count--) {

            rgb_plane[curframe_index++] = prev_rgb_plane[prevframe_index++];
            rgb_plane[curframe_index++] = prev_rgb_plane[prevframe_index++];
            rgb_plane[curframe_index++] = prev_rgb_plane[prevframe_index++];

            ADVANCE_CURFRAME_X();
            ADVANCE_PREVFRAME_X();
        }
        break;

    case PIX_FMT_RGBA32:
        rgb32_plane = (unsigned int *)s->current_frame.data[0];
        prev_rgb32_plane = (unsigned int *)s->last_frame.data[0];
        stride = s->current_frame.linesize[0] / 4;
        line_inc = stride - width;
        curframe_index = y * stride + x;
        curframe_x = x;
        prevframe_index = (y + motion_y) * stride + x + motion_x;
        prevframe_x = x + motion_x;
        while(pixel_count--) {

            rgb32_plane[curframe_index++] = 
                prev_rgb32_plane[prevframe_index++];

            ADVANCE_CURFRAME_X();
            ADVANCE_PREVFRAME_X();
        }
        break;

    case PIX_FMT_YUV444P:
        y_plane = s->current_frame.data[0];
        u_plane = s->current_frame.data[1];
        v_plane = s->current_frame.data[2];
        prev_y_plane = s->last_frame.data[0];
        prev_u_plane = s->last_frame.data[1];
        prev_v_plane = s->last_frame.data[2];
        stride = s->current_frame.linesize[0];
        line_inc = stride - width;
        curframe_index = y * stride + x;
        curframe_x = x;
        prevframe_index = (y + motion_y) * stride + x + motion_x;
        prevframe_x = x + motion_x;
        while(pixel_count--) {

            y_plane[curframe_index] = prev_y_plane[prevframe_index];
            u_plane[curframe_index] = prev_u_plane[prevframe_index];
            v_plane[curframe_index] = prev_v_plane[prevframe_index];

            curframe_index++;
            ADVANCE_CURFRAME_X();
            prevframe_index++;
            ADVANCE_PREVFRAME_X();
        }
        break;

    default:
        av_log(s->avctx, AV_LOG_ERROR, " Xan WC3: Unhandled colorspace\n");
        break;
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
    unsigned char *imagedata_buffer = s->buffer2;

    /* pointers to segments inside the compressed chunk */
    unsigned char *huffman_segment;
    unsigned char *size_segment;
    unsigned char *vector_segment;
    unsigned char *imagedata_segment;

    huffman_segment =   s->buf + LE_16(&s->buf[0]);
    size_segment =      s->buf + LE_16(&s->buf[2]);
    vector_segment =    s->buf + LE_16(&s->buf[4]);
    imagedata_segment = s->buf + LE_16(&s->buf[6]);

    xan_huffman_decode(opcode_buffer, huffman_segment);

    if (imagedata_segment[0] == 2)
        xan_unpack(imagedata_buffer, &imagedata_segment[1]);
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
            size = BE_16(&size_segment[0]);
            size_segment += 2;
            break;

        case 11:
        case 21:
            size = (size_segment[0] << 16) | (size_segment[1] << 8) |
                size_segment[2];
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

    /* for PAL8, make the palette available on the way out */
    if (s->avctx->pix_fmt == PIX_FMT_PAL8) {
        memcpy(s->current_frame.data[1], s->palette, PALETTE_COUNT * 4);
        s->current_frame.palette_has_changed = 1;
        s->avctx->palctrl->palette_changed = 0;
    }
}

static void xan_wc4_decode_frame(XanContext *s) {
}

static int xan_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    XanContext *s = avctx->priv_data;
    AVPaletteControl *palette_control = avctx->palctrl;
    int keyframe = 0;

    if (palette_control->palette_changed) {
        /* load the new palette and reset the palette control */
        xan_wc3_build_palette(s, palette_control->palette);
        /* If pal8 we clear flag when we copy palette */
        if (s->avctx->pix_fmt != PIX_FMT_PAL8)
            palette_control->palette_changed = 0;
        keyframe = 1;
    }

    if (avctx->get_buffer(avctx, &s->current_frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "  Xan Video: get_buffer() failed\n");
        return -1;
    }
    s->current_frame.reference = 3;

    s->buf = buf;
    s->size = buf_size;

    if (avctx->codec->id == CODEC_ID_XAN_WC3)
        xan_wc3_decode_frame(s);
    else if (avctx->codec->id == CODEC_ID_XAN_WC4)
        xan_wc4_decode_frame(s);

    /* release the last frame if it is allocated */
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);

    /* shuffle frames */
    s->last_frame = s->current_frame;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->current_frame;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static int xan_decode_end(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    /* release the last frame */
    avctx->release_buffer(avctx, &s->last_frame);

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
