/*
 * Duck TrueMotion 1.0 Decoder
 * Copyright (C) 2003 Alex Beregszaszi & Mike Melanson
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
 */

/**
 * @file truemotion1.c
 * Duck TrueMotion v1 Video Decoder by 
 * Alex Beregszaszi (alex@fsn.hu) and
 * Mike Melanson (melanson@pcisys.net)
 *
 * The TrueMotion v1 decoder presently only decodes 16-bit TM1 data and
 * outputs RGB555 data. 24-bit TM1 data is not supported yet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#include "truemotion1data.h"

typedef struct TrueMotion1Context {
    AVCodecContext *avctx;
    AVFrame frame;
    AVFrame prev_frame;

    unsigned char *buf;
    int size;

    unsigned char *mb_change_bits;
    int mb_change_bits_row_size;
    unsigned char *index_stream;
    int index_stream_size;

    int flags;
    int x, y, w, h;
    
    uint32_t y_predictor_table[1024];
    uint32_t c_predictor_table[1024];
    
    int compression;
    int block_type;
    int block_width;
    int block_height;

    int16_t ydt[8];
    int16_t cdt[8];
    int16_t fat_ydt[8];
    int16_t fat_cdt[8];
    
    int last_deltaset, last_vectable;

    unsigned int *vert_pred;

} TrueMotion1Context;

#define FLAG_SPRITE         32
#define FLAG_KEYFRAME       16
#define FLAG_INTERFRAME      8
#define FLAG_INTERPOLATED    4

struct frame_header {
    uint8_t header_size;
    uint8_t compression;
    uint8_t deltaset;
    uint8_t vectable;
    uint16_t ysize;
    uint16_t xsize;
    uint16_t checksum;
    uint8_t version;
    uint8_t header_type;
    uint8_t flags;
    uint8_t control;
    uint16_t xoffset;
    uint16_t yoffset;
    uint16_t width;
    uint16_t height;
};

#define ALGO_NOP        0
#define ALGO_RGB16V     1
#define ALGO_RGB16H     2
#define ALGO_RGB24H     3

/* these are the various block sizes that can occupy a 4x4 block */
#define BLOCK_2x2  0
#define BLOCK_2x4  1
#define BLOCK_4x2  2
#define BLOCK_4x4  3

typedef struct comp_types {
    int algorithm;
    int block_width;
    int block_height;
    int block_type;
} comp_types;

/* { valid for metatype }, algorithm, num of deltas, horiz res, vert res */
static comp_types compression_types[17] = {
    { ALGO_NOP,    0, 0, 0 },

    { ALGO_RGB16V, 4, 4, BLOCK_4x4 },
    { ALGO_RGB16H, 4, 4, BLOCK_4x4 },
    { ALGO_RGB16V, 4, 2, BLOCK_4x2 },
    { ALGO_RGB16H, 4, 2, BLOCK_4x2 },

    { ALGO_RGB16V, 2, 4, BLOCK_2x4 },
    { ALGO_RGB16H, 2, 4, BLOCK_2x4 },
    { ALGO_RGB16V, 2, 2, BLOCK_2x2 },
    { ALGO_RGB16H, 2, 2, BLOCK_2x2 },

    { ALGO_NOP,    4, 4, BLOCK_4x4 },
    { ALGO_RGB24H, 4, 4, BLOCK_4x4 },
    { ALGO_NOP,    4, 2, BLOCK_4x2 },
    { ALGO_RGB24H, 4, 2, BLOCK_4x2 },

    { ALGO_NOP,    2, 4, BLOCK_2x4 },
    { ALGO_RGB24H, 2, 4, BLOCK_2x4 },
    { ALGO_NOP,    2, 2, BLOCK_2x2 },
    { ALGO_RGB24H, 2, 2, BLOCK_2x2 }
};

static void select_delta_tables(TrueMotion1Context *s, int delta_table_index)
{
    int i;

    if (delta_table_index > 3)
        return;

    memcpy(s->ydt, ydts[delta_table_index], 8 * sizeof(int16_t));
    memcpy(s->cdt, cdts[delta_table_index], 8 * sizeof(int16_t));
    memcpy(s->fat_ydt, fat_ydts[delta_table_index], 8 * sizeof(int16_t));
    memcpy(s->fat_cdt, fat_cdts[delta_table_index], 8 * sizeof(int16_t));

    /* Y skinny deltas need to be halved for some reason; maybe the
     * skinny Y deltas should be modified */
    for (i = 0; i < 8; i++)
    {
        /* drop the lsb before dividing by 2-- net effect: round down
         * when dividing a negative number (e.g., -3/2 = -2, not -1) */
        s->ydt[i] &= 0xFFFE;
        s->ydt[i] /= 2;
    }
}

#ifdef WORDS_BIGENDIAN
static int make_ydt_entry(int p2, int p1, int16_t *ydt)
#else
static int make_ydt_entry(int p1, int p2, int16_t *ydt)
#endif
{
    int lo, hi;
    
    lo = ydt[p1];
    lo += (lo << 5) + (lo << 10);
    hi = ydt[p2];
    hi += (hi << 5) + (hi << 10);
    return ((lo + (hi << 16)) << 1);
}

#ifdef WORDS_BIGENDIAN
static int make_cdt_entry(int p2, int p1, int16_t *cdt)
#else
static int make_cdt_entry(int p1, int p2, int16_t *cdt)
#endif
{
    int r, b, lo;
    
    b = cdt[p2];
    r = cdt[p1] << 10;
    lo = b + r;
    return ((lo + (lo << 16)) << 1);
}

static void gen_vector_table(TrueMotion1Context *s, uint8_t *sel_vector_table)
{
    int len, i, j;
    unsigned char delta_pair;
    
    for (i = 0; i < 1024; i += 4)
    {
        len = *sel_vector_table++ / 2;
        for (j = 0; j < len; j++)
        {
            delta_pair = *sel_vector_table++;
            s->y_predictor_table[i+j] = 0xfffffffe & 
                make_ydt_entry(delta_pair >> 4, delta_pair & 0xf, s->ydt);
            s->c_predictor_table[i+j] = 0xfffffffe & 
                make_cdt_entry(delta_pair >> 4, delta_pair & 0xf, s->cdt);
        }
        s->y_predictor_table[i+(j-1)] |= 1;
        s->c_predictor_table[i+(j-1)] |= 1;
    }
}

/* Returns the number of bytes consumed from the bytestream. Returns -1 if
 * there was an error while decoding the header */ 
static int truemotion1_decode_header(TrueMotion1Context *s)
{
    int i;
    struct frame_header header;
    uint8_t header_buffer[128];  /* logical maximum size of the header */
    uint8_t *sel_vector_table;

    /* There is 1 change bit per 4 pixels, so each change byte represents
     * 32 pixels; divide width by 4 to obtain the number of change bits and
     * then round up to the nearest byte. */
    s->mb_change_bits_row_size = ((s->avctx->width >> 2) + 7) >> 3;

    header.header_size = ((s->buf[0] >> 5) | (s->buf[0] << 3)) & 0x7f;
    if (s->buf[0] < 0x10)
    {
        av_log(s->avctx, AV_LOG_ERROR, "invalid header size\n");
        return -1;
    }

    /* unscramble the header bytes with a XOR operation */
    memset(header_buffer, 0, 128);
    for (i = 1; i < header.header_size; i++)
    header_buffer[i - 1] = s->buf[i] ^ s->buf[i + 1];
    header.compression = header_buffer[0];
    header.deltaset = header_buffer[1];
    header.vectable = header_buffer[2];
    header.ysize = LE_16(&header_buffer[3]);
    header.xsize = LE_16(&header_buffer[5]);
    header.checksum = LE_16(&header_buffer[7]);
    header.version = header_buffer[9];
    header.header_type = header_buffer[10];
    header.flags = header_buffer[11];
    header.control = header_buffer[12];

    /* Version 2 */
    if (header.version >= 2)
    {
        if (header.header_type > 3)
        {
            av_log(s->avctx, AV_LOG_ERROR, "truemotion1: invalid header type\n");
            return -1;
        } else if ((header.header_type == 2) || (header.header_type == 3)) {
            s->flags = header.flags;
            if (!(s->flags & FLAG_INTERFRAME))
                s->flags |= FLAG_KEYFRAME;
        } else
            s->flags = FLAG_KEYFRAME;
    } else /* Version 1 */
        s->flags = FLAG_KEYFRAME;
    
    if (s->flags & FLAG_SPRITE) {
        s->w = header.width;
        s->h = header.height;
        s->x = header.xoffset;
        s->y = header.yoffset;
    } else {
        s->w = header.xsize;
        s->h = header.ysize;
        if (header.header_type < 2) {
            if ((s->w < 213) && (s->h >= 176))
                s->flags |= FLAG_INTERPOLATED;
        }
    }

    if (header.compression > 17) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid compression type (%d)\n", header.compression);
        return -1;
    }
    
    if ((header.deltaset != s->last_deltaset) || 
        (header.vectable != s->last_vectable))
        select_delta_tables(s, header.deltaset);

    if ((header.compression & 1) && header.header_type)
        sel_vector_table = pc_tbl2;
    else {
        if (header.vectable < 4)
            sel_vector_table = tables[header.vectable - 1];
        else {
            av_log(s->avctx, AV_LOG_ERROR, "invalid vector table id (%d)\n", header.vectable);
            return -1;
        }
    }

    if ((header.deltaset != s->last_deltaset) || (header.vectable != s->last_vectable))
    {
        if (compression_types[header.compression].algorithm == ALGO_RGB24H)
        {
            av_log(s->avctx, AV_LOG_ERROR, "24bit compression not yet supported\n");
        }
        else
            gen_vector_table(s, sel_vector_table);
    }

    /* set up pointers to the other key data chunks */
    s->mb_change_bits = s->buf + header.header_size;
    if (s->flags & FLAG_KEYFRAME) {
        /* no change bits specified for a keyframe; only index bytes */
        s->index_stream = s->mb_change_bits;
    } else {
        /* one change bit per 4x4 block */
        s->index_stream = s->mb_change_bits + 
            (s->mb_change_bits_row_size * (s->avctx->height >> 2));
    }
    s->index_stream_size = s->size - (s->index_stream - s->buf);

    s->last_deltaset = header.deltaset;
    s->last_vectable = header.vectable;
    s->compression = header.compression;
    s->block_width = compression_types[header.compression].block_width;
    s->block_height = compression_types[header.compression].block_height;
    s->block_type = compression_types[header.compression].block_type;

    return header.header_size;    
}

static int truemotion1_decode_init(AVCodecContext *avctx)
{
    TrueMotion1Context *s = (TrueMotion1Context *)avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_RGB555;
    avctx->has_b_frames = 0;
    s->frame.data[0] = s->prev_frame.data[0] = NULL;

    /* there is a vertical predictor for each pixel in a line; each vertical
     * predictor is 0 to start with */
    s->vert_pred = 
        (unsigned int *)av_malloc(s->avctx->width * sizeof(unsigned short));

    return 0;
}

#define GET_NEXT_INDEX() \
{\
    if (index_stream_index >= s->index_stream_size) { \
        av_log(s->avctx, AV_LOG_INFO, " help! truemotion1 decoder went out of bounds\n"); \
        return; \
    } \
    index = s->index_stream[index_stream_index++] * 4; \
}

#define APPLY_C_PREDICTOR() \
    predictor_pair = s->c_predictor_table[index]; \
    horiz_pred += (predictor_pair >> 1); \
    if (predictor_pair & 1) { \
        GET_NEXT_INDEX() \
        if (!index) { \
            GET_NEXT_INDEX() \
            predictor_pair = s->c_predictor_table[index]; \
            horiz_pred += ((predictor_pair >> 1) * 5); \
            if (predictor_pair & 1) \
                GET_NEXT_INDEX() \
            else \
                index++; \
        } \
    } else \
        index++;

#define APPLY_Y_PREDICTOR() \
    predictor_pair = s->y_predictor_table[index]; \
    horiz_pred += (predictor_pair >> 1); \
    if (predictor_pair & 1) { \
        GET_NEXT_INDEX() \
        if (!index) { \
            GET_NEXT_INDEX() \
            predictor_pair = s->y_predictor_table[index]; \
            horiz_pred += ((predictor_pair >> 1) * 5); \
            if (predictor_pair & 1) \
                GET_NEXT_INDEX() \
            else \
                index++; \
        } \
    } else \
        index++;

#define OUTPUT_PIXEL_PAIR() \
    *current_pixel_pair = *vert_pred + horiz_pred; \
    *vert_pred++ = *current_pixel_pair++; \
    prev_pixel_pair++;

static void truemotion1_decode_16bit(TrueMotion1Context *s)
{
    int y;
    int pixels_left;  /* remaining pixels on this line */
    unsigned int predictor_pair;
    unsigned int horiz_pred;
    unsigned int *vert_pred;
    unsigned int *current_pixel_pair;
    unsigned int *prev_pixel_pair;
    unsigned char *current_line = s->frame.data[0];
    unsigned char *prev_line = s->prev_frame.data[0];
    int keyframe = s->flags & FLAG_KEYFRAME;

    /* these variables are for managing the stream of macroblock change bits */
    unsigned char *mb_change_bits = s->mb_change_bits;
    unsigned char mb_change_byte;
    unsigned char mb_change_byte_mask;
    int mb_change_index;

    /* these variables are for managing the main index stream */
    int index_stream_index = 0;  /* yes, the index into the index stream */
    int index;

    /* clean out the line buffer */
    memset(s->vert_pred, 0, s->avctx->width * sizeof(unsigned short));

    GET_NEXT_INDEX();

    for (y = 0; y < s->avctx->height; y++) {

        /* re-init variables for the next line iteration */
        horiz_pred = 0;
        current_pixel_pair = (unsigned int *)current_line;
        prev_pixel_pair = (unsigned int *)prev_line;
        vert_pred = s->vert_pred;
        mb_change_index = 0;
        mb_change_byte = mb_change_bits[mb_change_index++];
        mb_change_byte_mask = 0x01;
        pixels_left = s->avctx->width;

        while (pixels_left > 0) {

            if (keyframe || ((mb_change_byte & mb_change_byte_mask) == 0)) {

                switch (y & 3) {
                case 0:
                    /* if macroblock width is 2, apply C-Y-C-Y; else 
                     * apply C-Y-Y */
                    if (s->block_width == 2) {
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                    } else {
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                    }
                    break;

                case 1:
                case 3:
                    /* always apply 2 Y predictors on these iterations */
                    APPLY_Y_PREDICTOR();
                    OUTPUT_PIXEL_PAIR();
                    APPLY_Y_PREDICTOR();
                    OUTPUT_PIXEL_PAIR();
                    break;

                case 2:
                    /* this iteration might be C-Y-C-Y, Y-Y, or C-Y-Y 
                     * depending on the macroblock type */
                    if (s->block_type == BLOCK_2x2) {
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                    } else if (s->block_type == BLOCK_4x2) {
                        APPLY_C_PREDICTOR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                    } else {
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                        APPLY_Y_PREDICTOR();
                        OUTPUT_PIXEL_PAIR();
                    }
                    break;
                }

            } else {

                /* skip (copy) four pixels, but reassign the horizontal 
                 * predictor */
                *current_pixel_pair = *prev_pixel_pair++;
                *vert_pred++ = *current_pixel_pair++;
                *current_pixel_pair = *prev_pixel_pair++;
                horiz_pred = *current_pixel_pair - *vert_pred;
                *vert_pred++ = *current_pixel_pair++;
                
            }

            if (!keyframe) {
                mb_change_byte_mask <<= 1;

                /* next byte */
                if (!mb_change_byte_mask) {
                    mb_change_byte = mb_change_bits[mb_change_index++];
                    mb_change_byte_mask = 0x01;
                }
            }

            pixels_left -= 4;
        }

        /* next change row */
        if (((y + 1) & 3) == 0)
            mb_change_bits += s->mb_change_bits_row_size;

        current_line += s->frame.linesize[0];
        prev_line += s->prev_frame.linesize[0];
    }
}

static int truemotion1_decode_frame(AVCodecContext *avctx,
                                    void *data, int *data_size,
                                    uint8_t *buf, int buf_size)
{
    TrueMotion1Context *s = (TrueMotion1Context *)avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    if (avctx->get_buffer(avctx, &s->frame) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "truemotion1: get_buffer() failed\n");
        return -1;
    }

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    if (truemotion1_decode_header(s) == -1)
        return -1;

    /* check for a do-nothing frame and copy the previous frame */
    if (compression_types[s->compression].algorithm == ALGO_NOP)
    {
        memcpy(s->frame.data[0], s->prev_frame.data[0],
            s->frame.linesize[0] * s->avctx->height);
    } else if (compression_types[s->compression].algorithm == ALGO_RGB24H) {
        av_log(s->avctx, AV_LOG_ERROR, "24bit compression not yet supported\n");
    } else {
        truemotion1_decode_16bit(s);
    }

    if (s->prev_frame.data[0])
        avctx->release_buffer(avctx, &s->prev_frame);

    /* shuffle frames */
    s->prev_frame = s->frame;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int truemotion1_decode_end(AVCodecContext *avctx)
{
    TrueMotion1Context *s = (TrueMotion1Context *)avctx->priv_data;

    /* release the last frame */
    if (s->prev_frame.data[0])
        avctx->release_buffer(avctx, &s->prev_frame);

    av_free(s->vert_pred);

    return 0;
}

AVCodec truemotion1_decoder = {
    "truemotion1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_TRUEMOTION1,
    sizeof(TrueMotion1Context),
    truemotion1_decode_init,
    NULL,
    truemotion1_decode_end,
    truemotion1_decode_frame,
    CODEC_CAP_DR1,
};
