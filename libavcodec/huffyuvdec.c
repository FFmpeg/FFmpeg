/*
 * huffyuv decoder
 *
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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
 * huffyuv decoder
 */

#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"
#include "huffyuv.h"
#include "thread.h"

#define classic_shift_luma_table_size 42
static const unsigned char classic_shift_luma[classic_shift_luma_table_size + FF_INPUT_BUFFER_PADDING_SIZE] = {
  34,36,35,69,135,232,9,16,10,24,11,23,12,16,13,10,14,8,15,8,
  16,8,17,20,16,10,207,206,205,236,11,8,10,21,9,23,8,8,199,70,
  69,68, 0,
  0,0,0,0,0,0,0,0,
};

#define classic_shift_chroma_table_size 59
static const unsigned char classic_shift_chroma[classic_shift_chroma_table_size + FF_INPUT_BUFFER_PADDING_SIZE] = {
  66,36,37,38,39,40,41,75,76,77,110,239,144,81,82,83,84,85,118,183,
  56,57,88,89,56,89,154,57,58,57,26,141,57,56,58,57,58,57,184,119,
  214,245,116,83,82,49,80,79,78,77,44,75,41,40,39,38,37,36,34, 0,
  0,0,0,0,0,0,0,0,
};

static const unsigned char classic_add_luma[256] = {
    3,  9,  5, 12, 10, 35, 32, 29, 27, 50, 48, 45, 44, 41, 39, 37,
   73, 70, 68, 65, 64, 61, 58, 56, 53, 50, 49, 46, 44, 41, 38, 36,
   68, 65, 63, 61, 58, 55, 53, 51, 48, 46, 45, 43, 41, 39, 38, 36,
   35, 33, 32, 30, 29, 27, 26, 25, 48, 47, 46, 44, 43, 41, 40, 39,
   37, 36, 35, 34, 32, 31, 30, 28, 27, 26, 24, 23, 22, 20, 19, 37,
   35, 34, 33, 31, 30, 29, 27, 26, 24, 23, 21, 20, 18, 17, 15, 29,
   27, 26, 24, 22, 21, 19, 17, 16, 14, 26, 25, 23, 21, 19, 18, 16,
   15, 27, 25, 23, 21, 19, 17, 16, 14, 26, 25, 23, 21, 18, 17, 14,
   12, 17, 19, 13,  4,  9,  2, 11,  1,  7,  8,  0, 16,  3, 14,  6,
   12, 10,  5, 15, 18, 11, 10, 13, 15, 16, 19, 20, 22, 24, 27, 15,
   18, 20, 22, 24, 26, 14, 17, 20, 22, 24, 27, 15, 18, 20, 23, 25,
   28, 16, 19, 22, 25, 28, 32, 36, 21, 25, 29, 33, 38, 42, 45, 49,
   28, 31, 34, 37, 40, 42, 44, 47, 49, 50, 52, 54, 56, 57, 59, 60,
   62, 64, 66, 67, 69, 35, 37, 39, 40, 42, 43, 45, 47, 48, 51, 52,
   54, 55, 57, 59, 60, 62, 63, 66, 67, 69, 71, 72, 38, 40, 42, 43,
   46, 47, 49, 51, 26, 28, 30, 31, 33, 34, 18, 19, 11, 13,  7,  8,
};

static const unsigned char classic_add_chroma[256] = {
    3,  1,  2,  2,  2,  2,  3,  3,  7,  5,  7,  5,  8,  6, 11,  9,
    7, 13, 11, 10,  9,  8,  7,  5,  9,  7,  6,  4,  7,  5,  8,  7,
   11,  8, 13, 11, 19, 15, 22, 23, 20, 33, 32, 28, 27, 29, 51, 77,
   43, 45, 76, 81, 46, 82, 75, 55, 56,144, 58, 80, 60, 74,147, 63,
  143, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
   80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 27, 30, 21, 22,
   17, 14,  5,  6,100, 54, 47, 50, 51, 53,106,107,108,109,110,111,
  112,113,114,115,  4,117,118, 92, 94,121,122,  3,124,103,  2,  1,
    0,129,130,131,120,119,126,125,136,137,138,139,140,141,142,134,
  135,132,133,104, 64,101, 62, 57,102, 95, 93, 59, 61, 28, 97, 96,
   52, 49, 48, 29, 32, 25, 24, 46, 23, 98, 45, 44, 43, 20, 42, 41,
   19, 18, 99, 40, 15, 39, 38, 16, 13, 12, 11, 37, 10,  9,  8, 36,
    7,128,127,105,123,116, 35, 34, 33,145, 31, 79, 42,146, 78, 26,
   83, 48, 49, 50, 44, 47, 26, 31, 30, 18, 17, 19, 21, 24, 25, 13,
   14, 16, 17, 18, 20, 21, 12, 14, 15,  9, 10,  6,  9,  6,  5,  8,
    6, 12,  8, 10,  7,  9,  6,  4,  6,  2,  2,  3,  3,  3,  3,  2,
};

static int read_len_table(uint8_t *dst, GetBitContext *gb)
{
    int i, val, repeat;

    for (i = 0; i < 256;) {
        repeat = get_bits(gb, 3);
        val    = get_bits(gb, 5);
        if (repeat == 0)
            repeat = get_bits(gb, 8);
        if (i + repeat > 256 || get_bits_left(gb) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error reading huffman table\n");
            return -1;
        }
        while (repeat--)
            dst[i++] = val;
    }
    return 0;
}

static void generate_joint_tables(HYuvContext *s)
{
    uint16_t symbols[1 << VLC_BITS];
    uint16_t bits[1 << VLC_BITS];
    uint8_t len[1 << VLC_BITS];
    if (s->bitstream_bpp < 24) {
        int p, i, y, u;
        for (p = 0; p < 3; p++) {
            for (i = y = 0; y < 256; y++) {
                int len0 = s->len[0][y];
                int limit = VLC_BITS - len0;
                if(limit <= 0)
                    continue;
                for (u = 0; u < 256; u++) {
                    int len1 = s->len[p][u];
                    if (len1 > limit)
                        continue;
                    len[i] = len0 + len1;
                    bits[i] = (s->bits[0][y] << len1) + s->bits[p][u];
                    symbols[i] = (y << 8) + u;
                    if(symbols[i] != 0xffff) // reserved to mean "invalid"
                        i++;
                }
            }
            ff_free_vlc(&s->vlc[3 + p]);
            ff_init_vlc_sparse(&s->vlc[3 + p], VLC_BITS, i, len, 1, 1,
                               bits, 2, 2, symbols, 2, 2, 0);
        }
    } else {
        uint8_t (*map)[4] = (uint8_t(*)[4])s->pix_bgr_map;
        int i, b, g, r, code;
        int p0 = s->decorrelate;
        int p1 = !s->decorrelate;
        // restrict the range to +/-16 because that's pretty much guaranteed to
        // cover all the combinations that fit in 11 bits total, and it doesn't
        // matter if we miss a few rare codes.
        for (i = 0, g = -16; g < 16; g++) {
            int len0 = s->len[p0][g & 255];
            int limit0 = VLC_BITS - len0;
            if (limit0 < 2)
                continue;
            for (b = -16; b < 16; b++) {
                int len1 = s->len[p1][b & 255];
                int limit1 = limit0 - len1;
                if (limit1 < 1)
                    continue;
                code = (s->bits[p0][g & 255] << len1) + s->bits[p1][b & 255];
                for (r = -16; r < 16; r++) {
                    int len2 = s->len[2][r & 255];
                    if (len2 > limit1)
                        continue;
                    len[i] = len0 + len1 + len2;
                    bits[i] = (code << len2) + s->bits[2][r & 255];
                    if (s->decorrelate) {
                        map[i][G] = g;
                        map[i][B] = g + b;
                        map[i][R] = g + r;
                    } else {
                        map[i][B] = g;
                        map[i][G] = b;
                        map[i][R] = r;
                    }
                    i++;
                }
            }
        }
        ff_free_vlc(&s->vlc[3]);
        init_vlc(&s->vlc[3], VLC_BITS, i, len, 1, 1, bits, 2, 2, 0);
    }
}

static int read_huffman_tables(HYuvContext *s, const uint8_t *src, int length)
{
    GetBitContext gb;
    int i;

    init_get_bits(&gb, src, length * 8);

    for (i = 0; i < 3; i++) {
        if (read_len_table(s->len[i], &gb) < 0)
            return -1;
        if (ff_huffyuv_generate_bits_table(s->bits[i], s->len[i]) < 0) {
            return -1;
        }
        ff_free_vlc(&s->vlc[i]);
        init_vlc(&s->vlc[i], VLC_BITS, 256, s->len[i], 1, 1,
                 s->bits[i], 4, 4, 0);
    }

    generate_joint_tables(s);

    return (get_bits_count(&gb) + 7) / 8;
}

static int read_old_huffman_tables(HYuvContext *s)
{
    GetBitContext gb;
    int i;

    init_get_bits(&gb, classic_shift_luma,
                  classic_shift_luma_table_size * 8);
    if (read_len_table(s->len[0], &gb) < 0)
        return -1;

    init_get_bits(&gb, classic_shift_chroma,
                  classic_shift_chroma_table_size * 8);
    if (read_len_table(s->len[1], &gb) < 0)
        return -1;

    for(i=0; i<256; i++) s->bits[0][i] = classic_add_luma  [i];
    for(i=0; i<256; i++) s->bits[1][i] = classic_add_chroma[i];

    if (s->bitstream_bpp >= 24) {
        memcpy(s->bits[1], s->bits[0], 256 * sizeof(uint32_t));
        memcpy(s->len[1] , s->len [0], 256 * sizeof(uint8_t));
    }
    memcpy(s->bits[2], s->bits[1], 256 * sizeof(uint32_t));
    memcpy(s->len[2] , s->len [1], 256 * sizeof(uint8_t));

    for (i = 0; i < 3; i++) {
        ff_free_vlc(&s->vlc[i]);
        init_vlc(&s->vlc[i], VLC_BITS, 256, s->len[i], 1, 1,
                 s->bits[i], 4, 4, 0);
    }

    generate_joint_tables(s);

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    ff_huffyuv_common_init(avctx);
    memset(s->vlc, 0, 3 * sizeof(VLC));

    avctx->coded_frame = &s->picture;
    avcodec_get_frame_defaults(&s->picture);
    s->interlaced = s->height > 288;

    s->bgr32 = 1;

    if (avctx->extradata_size) {
        if ((avctx->bits_per_coded_sample & 7) &&
            avctx->bits_per_coded_sample != 12)
            s->version = 1; // do such files exist at all?
        else
            s->version = 2;
    } else
        s->version = 0;

    if (s->version == 2) {
        int method, interlace;

        if (avctx->extradata_size < 4)
            return -1;

        method = ((uint8_t*)avctx->extradata)[0];
        s->decorrelate = method & 64 ? 1 : 0;
        s->predictor = method & 63;
        s->bitstream_bpp = ((uint8_t*)avctx->extradata)[1];
        if (s->bitstream_bpp == 0)
            s->bitstream_bpp = avctx->bits_per_coded_sample & ~7;
        interlace = (((uint8_t*)avctx->extradata)[2] & 0x30) >> 4;
        s->interlaced = (interlace == 1) ? 1 : (interlace == 2) ? 0 : s->interlaced;
        s->context = ((uint8_t*)avctx->extradata)[2] & 0x40 ? 1 : 0;

        if ( read_huffman_tables(s, ((uint8_t*)avctx->extradata) + 4,
                                 avctx->extradata_size - 4) < 0)
            return AVERROR_INVALIDDATA;
    }else{
        switch (avctx->bits_per_coded_sample & 7) {
        case 1:
            s->predictor = LEFT;
            s->decorrelate = 0;
            break;
        case 2:
            s->predictor = LEFT;
            s->decorrelate = 1;
            break;
        case 3:
            s->predictor = PLANE;
            s->decorrelate = avctx->bits_per_coded_sample >= 24;
            break;
        case 4:
            s->predictor = MEDIAN;
            s->decorrelate = 0;
            break;
        default:
            s->predictor = LEFT; //OLD
            s->decorrelate = 0;
            break;
        }
        s->bitstream_bpp = avctx->bits_per_coded_sample & ~7;
        s->context = 0;

        if (read_old_huffman_tables(s) < 0)
            return AVERROR_INVALIDDATA;
    }

    switch (s->bitstream_bpp) {
    case 12:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        break;
    case 16:
        if (s->yuy2) {
            avctx->pix_fmt = AV_PIX_FMT_YUYV422;
        } else {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        }
        break;
    case 24:
    case 32:
        if (s->bgr32) {
            avctx->pix_fmt = AV_PIX_FMT_RGB32;
        } else {
            avctx->pix_fmt = AV_PIX_FMT_BGR24;
        }
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    if ((avctx->pix_fmt == AV_PIX_FMT_YUV422P || avctx->pix_fmt == AV_PIX_FMT_YUV420P) && avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "width must be even for this colorspace\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->predictor == MEDIAN && avctx->pix_fmt == AV_PIX_FMT_YUV422P && avctx->width%4) {
        av_log(avctx, AV_LOG_ERROR, "width must be a multiple of 4 this colorspace and predictor\n");
        return AVERROR_INVALIDDATA;
    }
    if (ff_huffyuv_alloc_temp(s)) {
        ff_huffyuv_common_end(s);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int decode_init_thread_copy(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i;

    avctx->coded_frame= &s->picture;
    if (ff_huffyuv_alloc_temp(s)) {
        ff_huffyuv_common_end(s);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < 6; i++)
        s->vlc[i].table = NULL;

    if (s->version == 2) {
        if (read_huffman_tables(s, ((uint8_t*)avctx->extradata) + 4,
                                avctx->extradata_size) < 0)
            return AVERROR_INVALIDDATA;
    } else {
        if (read_old_huffman_tables(s) < 0)
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

/* TODO instead of restarting the read when the code isn't in the first level
 * of the joint table, jump into the 2nd level of the individual table. */
#define READ_2PIX(dst0, dst1, plane1){\
    uint16_t code = get_vlc2(&s->gb, s->vlc[3+plane1].table, VLC_BITS, 1);\
    if(code != 0xffff){\
        dst0 = code>>8;\
        dst1 = code;\
    }else{\
        dst0 = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);\
        dst1 = get_vlc2(&s->gb, s->vlc[plane1].table, VLC_BITS, 3);\
    }\
}

static void decode_422_bitstream(HYuvContext *s, int count)
{
    int i;

    count /= 2;

    if (count >= (get_bits_left(&s->gb)) / (31 * 4)) {
        for (i = 0; i < count && get_bits_left(&s->gb) > 0; i++) {
            READ_2PIX(s->temp[0][2 * i    ], s->temp[1][i], 1);
            READ_2PIX(s->temp[0][2 * i + 1], s->temp[2][i], 2);
        }
    } else {
        for (i = 0; i < count; i++) {
            READ_2PIX(s->temp[0][2 * i    ], s->temp[1][i], 1);
            READ_2PIX(s->temp[0][2 * i + 1], s->temp[2][i], 2);
        }
    }
}

static void decode_gray_bitstream(HYuvContext *s, int count)
{
    int i;

    count/=2;

    if (count >= (get_bits_left(&s->gb)) / (31 * 2)) {
        for (i = 0; i < count && get_bits_left(&s->gb) > 0; i++) {
            READ_2PIX(s->temp[0][2 * i], s->temp[0][2 * i + 1], 0);
        }
    } else {
        for(i=0; i<count; i++){
            READ_2PIX(s->temp[0][2 * i], s->temp[0][2 * i + 1], 0);
        }
    }
}

static av_always_inline void decode_bgr_1(HYuvContext *s, int count,
                                          int decorrelate, int alpha)
{
    int i;
    for (i = 0; i < count; i++) {
        int code = get_vlc2(&s->gb, s->vlc[3].table, VLC_BITS, 1);
        if (code != -1) {
            *(uint32_t*)&s->temp[0][4 * i] = s->pix_bgr_map[code];
        } else if(decorrelate) {
            s->temp[0][4 * i + G] = get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3);
            s->temp[0][4 * i + B] = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3) +
                                    s->temp[0][4 * i + G];
            s->temp[0][4 * i + R] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3) +
                                    s->temp[0][4 * i + G];
        } else {
            s->temp[0][4 * i + B] = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);
            s->temp[0][4 * i + G] = get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3);
            s->temp[0][4 * i + R] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3);
        }
        if (alpha)
            s->temp[0][4 * i + A] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3);
    }
}

static void decode_bgr_bitstream(HYuvContext *s, int count)
{
    if (s->decorrelate) {
        if (s->bitstream_bpp==24)
            decode_bgr_1(s, count, 1, 0);
        else
            decode_bgr_1(s, count, 1, 1);
    } else {
        if (s->bitstream_bpp==24)
            decode_bgr_1(s, count, 0, 0);
        else
            decode_bgr_1(s, count, 0, 1);
    }
}

static void draw_slice(HYuvContext *s, int y)
{
    int h, cy, i;
    int offset[AV_NUM_DATA_POINTERS];

    if (s->avctx->draw_horiz_band==NULL)
        return;

    h = y - s->last_slice_end;
    y -= h;

    if (s->bitstream_bpp == 12) {
        cy = y>>1;
    } else {
        cy = y;
    }

    offset[0] = s->picture.linesize[0]*y;
    offset[1] = s->picture.linesize[1]*cy;
    offset[2] = s->picture.linesize[2]*cy;
    for (i = 3; i < AV_NUM_DATA_POINTERS; i++)
        offset[i] = 0;
    emms_c();

    s->avctx->draw_horiz_band(s->avctx, &s->picture, offset, y, 3, h);

    s->last_slice_end = y + h;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    HYuvContext *s = avctx->priv_data;
    const int width = s->width;
    const int width2 = s->width>>1;
    const int height = s->height;
    int fake_ystride, fake_ustride, fake_vstride;
    AVFrame * const p = &s->picture;
    int table_size = 0, ret;

    AVFrame *picture = data;

    av_fast_padded_malloc(&s->bitstream_buffer,
                   &s->bitstream_buffer_size,
                   buf_size);
    if (!s->bitstream_buffer)
        return AVERROR(ENOMEM);

    s->dsp.bswap_buf((uint32_t*)s->bitstream_buffer,
                     (const uint32_t*)buf, buf_size / 4);

    if (p->data[0])
        ff_thread_release_buffer(avctx, p);

    p->reference = 0;
    if ((ret = ff_thread_get_buffer(avctx, p)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    if (s->context) {
        table_size = read_huffman_tables(s, s->bitstream_buffer, buf_size);
        if (table_size < 0)
            return AVERROR_INVALIDDATA;
    }

    if ((unsigned)(buf_size-table_size) >= INT_MAX / 8)
        return AVERROR_INVALIDDATA;

    init_get_bits(&s->gb, s->bitstream_buffer+table_size,
                  (buf_size-table_size) * 8);

    fake_ystride = s->interlaced ? p->linesize[0] * 2  : p->linesize[0];
    fake_ustride = s->interlaced ? p->linesize[1] * 2  : p->linesize[1];
    fake_vstride = s->interlaced ? p->linesize[2] * 2  : p->linesize[2];

    s->last_slice_end = 0;

    if (s->bitstream_bpp < 24) {
        int y, cy;
        int lefty, leftu, leftv;
        int lefttopy, lefttopu, lefttopv;

        if (s->yuy2) {
            p->data[0][3] = get_bits(&s->gb, 8);
            p->data[0][2] = get_bits(&s->gb, 8);
            p->data[0][1] = get_bits(&s->gb, 8);
            p->data[0][0] = get_bits(&s->gb, 8);

            av_log(avctx, AV_LOG_ERROR,
                   "YUY2 output is not implemented yet\n");
            return AVERROR_PATCHWELCOME;
        } else {

            leftv = p->data[2][0] = get_bits(&s->gb, 8);
            lefty = p->data[0][1] = get_bits(&s->gb, 8);
            leftu = p->data[1][0] = get_bits(&s->gb, 8);
                    p->data[0][0] = get_bits(&s->gb, 8);

            switch (s->predictor) {
            case LEFT:
            case PLANE:
                decode_422_bitstream(s, width-2);
                lefty = s->dsp.add_hfyu_left_prediction(p->data[0] + 2, s->temp[0], width-2, lefty);
                if (!(s->flags&CODEC_FLAG_GRAY)) {
                    leftu = s->dsp.add_hfyu_left_prediction(p->data[1] + 1, s->temp[1], width2 - 1, leftu);
                    leftv = s->dsp.add_hfyu_left_prediction(p->data[2] + 1, s->temp[2], width2 - 1, leftv);
                }

                for (cy = y = 1; y < s->height; y++, cy++) {
                    uint8_t *ydst, *udst, *vdst;

                    if (s->bitstream_bpp == 12) {
                        decode_gray_bitstream(s, width);

                        ydst = p->data[0] + p->linesize[0] * y;

                        lefty = s->dsp.add_hfyu_left_prediction(ydst, s->temp[0], width, lefty);
                        if (s->predictor == PLANE) {
                            if (y > s->interlaced)
                                s->dsp.add_bytes(ydst, ydst - fake_ystride, width);
                        }
                        y++;
                        if (y >= s->height) break;
                    }

                    draw_slice(s, y);

                    ydst = p->data[0] + p->linesize[0]*y;
                    udst = p->data[1] + p->linesize[1]*cy;
                    vdst = p->data[2] + p->linesize[2]*cy;

                    decode_422_bitstream(s, width);
                    lefty = s->dsp.add_hfyu_left_prediction(ydst, s->temp[0], width, lefty);
                    if (!(s->flags & CODEC_FLAG_GRAY)) {
                        leftu= s->dsp.add_hfyu_left_prediction(udst, s->temp[1], width2, leftu);
                        leftv= s->dsp.add_hfyu_left_prediction(vdst, s->temp[2], width2, leftv);
                    }
                    if (s->predictor == PLANE) {
                        if (cy > s->interlaced) {
                            s->dsp.add_bytes(ydst, ydst - fake_ystride, width);
                            if (!(s->flags & CODEC_FLAG_GRAY)) {
                                s->dsp.add_bytes(udst, udst - fake_ustride, width2);
                                s->dsp.add_bytes(vdst, vdst - fake_vstride, width2);
                            }
                        }
                    }
                }
                draw_slice(s, height);

                break;
            case MEDIAN:
                /* first line except first 2 pixels is left predicted */
                decode_422_bitstream(s, width - 2);
                lefty= s->dsp.add_hfyu_left_prediction(p->data[0] + 2, s->temp[0], width - 2, lefty);
                if (!(s->flags & CODEC_FLAG_GRAY)) {
                    leftu = s->dsp.add_hfyu_left_prediction(p->data[1] + 1, s->temp[1], width2 - 1, leftu);
                    leftv = s->dsp.add_hfyu_left_prediction(p->data[2] + 1, s->temp[2], width2 - 1, leftv);
                }

                cy = y = 1;

                /* second line is left predicted for interlaced case */
                if (s->interlaced) {
                    decode_422_bitstream(s, width);
                    lefty = s->dsp.add_hfyu_left_prediction(p->data[0] + p->linesize[0], s->temp[0], width, lefty);
                    if (!(s->flags & CODEC_FLAG_GRAY)) {
                        leftu = s->dsp.add_hfyu_left_prediction(p->data[1] + p->linesize[2], s->temp[1], width2, leftu);
                        leftv = s->dsp.add_hfyu_left_prediction(p->data[2] + p->linesize[1], s->temp[2], width2, leftv);
                    }
                    y++; cy++;
                }

                /* next 4 pixels are left predicted too */
                decode_422_bitstream(s, 4);
                lefty = s->dsp.add_hfyu_left_prediction(p->data[0] + fake_ystride, s->temp[0], 4, lefty);
                if (!(s->flags&CODEC_FLAG_GRAY)) {
                    leftu = s->dsp.add_hfyu_left_prediction(p->data[1] + fake_ustride, s->temp[1], 2, leftu);
                    leftv = s->dsp.add_hfyu_left_prediction(p->data[2] + fake_vstride, s->temp[2], 2, leftv);
                }

                /* next line except the first 4 pixels is median predicted */
                lefttopy = p->data[0][3];
                decode_422_bitstream(s, width - 4);
                s->dsp.add_hfyu_median_prediction(p->data[0] + fake_ystride+4, p->data[0]+4, s->temp[0], width-4, &lefty, &lefttopy);
                if (!(s->flags&CODEC_FLAG_GRAY)) {
                    lefttopu = p->data[1][1];
                    lefttopv = p->data[2][1];
                    s->dsp.add_hfyu_median_prediction(p->data[1] + fake_ustride+2, p->data[1] + 2, s->temp[1], width2 - 2, &leftu, &lefttopu);
                    s->dsp.add_hfyu_median_prediction(p->data[2] + fake_vstride+2, p->data[2] + 2, s->temp[2], width2 - 2, &leftv, &lefttopv);
                }
                y++; cy++;

                for (; y<height; y++, cy++) {
                    uint8_t *ydst, *udst, *vdst;

                    if (s->bitstream_bpp == 12) {
                        while (2 * cy > y) {
                            decode_gray_bitstream(s, width);
                            ydst = p->data[0] + p->linesize[0] * y;
                            s->dsp.add_hfyu_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                            y++;
                        }
                        if (y >= height) break;
                    }
                    draw_slice(s, y);

                    decode_422_bitstream(s, width);

                    ydst = p->data[0] + p->linesize[0] * y;
                    udst = p->data[1] + p->linesize[1] * cy;
                    vdst = p->data[2] + p->linesize[2] * cy;

                    s->dsp.add_hfyu_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                    if (!(s->flags & CODEC_FLAG_GRAY)) {
                        s->dsp.add_hfyu_median_prediction(udst, udst - fake_ustride, s->temp[1], width2, &leftu, &lefttopu);
                        s->dsp.add_hfyu_median_prediction(vdst, vdst - fake_vstride, s->temp[2], width2, &leftv, &lefttopv);
                    }
                }

                draw_slice(s, height);
                break;
            }
        }
    } else {
        int y;
        int leftr, leftg, leftb, lefta;
        const int last_line = (height - 1) * p->linesize[0];

        if (s->bitstream_bpp == 32) {
            lefta = p->data[0][last_line+A] = get_bits(&s->gb, 8);
            leftr = p->data[0][last_line+R] = get_bits(&s->gb, 8);
            leftg = p->data[0][last_line+G] = get_bits(&s->gb, 8);
            leftb = p->data[0][last_line+B] = get_bits(&s->gb, 8);
        } else {
            leftr = p->data[0][last_line+R] = get_bits(&s->gb, 8);
            leftg = p->data[0][last_line+G] = get_bits(&s->gb, 8);
            leftb = p->data[0][last_line+B] = get_bits(&s->gb, 8);
            lefta = p->data[0][last_line+A] = 255;
            skip_bits(&s->gb, 8);
        }

        if (s->bgr32) {
            switch (s->predictor) {
            case LEFT:
            case PLANE:
                decode_bgr_bitstream(s, width - 1);
                s->dsp.add_hfyu_left_prediction_bgr32(p->data[0] + last_line+4, s->temp[0], width - 1, &leftr, &leftg, &leftb, &lefta);

                for (y = s->height - 2; y >= 0; y--) { //Yes it is stored upside down.
                    decode_bgr_bitstream(s, width);

                    s->dsp.add_hfyu_left_prediction_bgr32(p->data[0] + p->linesize[0]*y, s->temp[0], width, &leftr, &leftg, &leftb, &lefta);
                    if (s->predictor == PLANE) {
                        if (s->bitstream_bpp != 32) lefta = 0;
                        if ((y & s->interlaced) == 0 &&
                            y < s->height - 1 - s->interlaced) {
                            s->dsp.add_bytes(p->data[0] + p->linesize[0] * y,
                                             p->data[0] + p->linesize[0] * y +
                                             fake_ystride, fake_ystride);
                        }
                    }
                }
                // just 1 large slice as this is not possible in reverse order
                draw_slice(s, height);
                break;
            default:
                av_log(avctx, AV_LOG_ERROR,
                       "prediction type not supported!\n");
            }
        }else{
            av_log(avctx, AV_LOG_ERROR,
                   "BGR24 output is not implemented yet\n");
            return AVERROR_PATCHWELCOME;
        }
    }
    emms_c();

    *picture = *p;
    *got_frame = 1;

    return (get_bits_count(&s->gb) + 31) / 32 * 4 + table_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    ff_huffyuv_common_end(s);
    av_freep(&s->bitstream_buffer);

    for (i = 0; i < 6; i++) {
        ff_free_vlc(&s->vlc[i]);
    }

    return 0;
}

#if CONFIG_HUFFYUV_DECODER
AVCodec ff_huffyuv_decoder = {
    .name             = "huffyuv",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HUFFYUV,
    .priv_data_size   = sizeof(HYuvContext),
    .init             = decode_init,
    .close            = decode_end,
    .decode           = decode_frame,
    .capabilities     = CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND |
                        CODEC_CAP_FRAME_THREADS,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .long_name        = NULL_IF_CONFIG_SMALL("Huffyuv / HuffYUV"),
};
#endif

#if CONFIG_FFVHUFF_DECODER
AVCodec ff_ffvhuff_decoder = {
    .name             = "ffvhuff",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_FFVHUFF,
    .priv_data_size   = sizeof(HYuvContext),
    .init             = decode_init,
    .close            = decode_end,
    .decode           = decode_frame,
    .capabilities     = CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND |
                        CODEC_CAP_FRAME_THREADS,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .long_name        = NULL_IF_CONFIG_SMALL("Huffyuv FFmpeg variant"),
};
#endif
