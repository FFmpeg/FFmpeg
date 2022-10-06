/*
 * NewTek SpeedHQ codec
 * Copyright 2017 Steinar H. Gunderson
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
 * NewTek SpeedHQ decoder.
 */

#define BITSTREAM_READER_LE

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "libavutil/thread.h"
#include "mathops.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "mpeg12vlc.h"

#define MAX_INDEX (64 - 1)

/*
 * 5 bits makes for very small tables, with no more than two lookups needed
 * for the longest (10-bit) codes.
 */
#define ALPHA_VLC_BITS 5

typedef struct SHQContext {
    AVCodecContext *avctx;
    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    ScanTable intra_scantable;
    int quant_matrix[64];
    enum { SHQ_SUBSAMPLING_420, SHQ_SUBSAMPLING_422, SHQ_SUBSAMPLING_444 }
        subsampling;
    enum { SHQ_NO_ALPHA, SHQ_RLE_ALPHA, SHQ_DCT_ALPHA } alpha_type;
} SHQContext;


/* AC codes: Very similar but not identical to MPEG-2. */
static const uint16_t speedhq_vlc[123][2] = {
    {0x0001,  2}, {0x0003,  3}, {0x000E,  4}, {0x0007,  5},
    {0x0017,  5}, {0x0028,  6}, {0x0008,  6}, {0x006F,  7},
    {0x001F,  7}, {0x00C4,  8}, {0x0044,  8}, {0x005F,  8},
    {0x00DF,  8}, {0x007F,  8}, {0x00FF,  8}, {0x3E00, 14},
    {0x1E00, 14}, {0x2E00, 14}, {0x0E00, 14}, {0x3600, 14},
    {0x1600, 14}, {0x2600, 14}, {0x0600, 14}, {0x3A00, 14},
    {0x1A00, 14}, {0x2A00, 14}, {0x0A00, 14}, {0x3200, 14},
    {0x1200, 14}, {0x2200, 14}, {0x0200, 14}, {0x0C00, 15},
    {0x7400, 15}, {0x3400, 15}, {0x5400, 15}, {0x1400, 15},
    {0x6400, 15}, {0x2400, 15}, {0x4400, 15}, {0x0400, 15},
    {0x0002,  3}, {0x000C,  5}, {0x004F,  7}, {0x00E4,  8},
    {0x0004,  8}, {0x0D00, 13}, {0x1500, 13}, {0x7C00, 15},
    {0x3C00, 15}, {0x5C00, 15}, {0x1C00, 15}, {0x6C00, 15},
    {0x2C00, 15}, {0x4C00, 15}, {0xC800, 16}, {0x4800, 16},
    {0x8800, 16}, {0x0800, 16}, {0x0300, 13}, {0x1D00, 13},
    {0x0014,  5}, {0x0070,  7}, {0x003F,  8}, {0x00C0, 10},
    {0x0500, 13}, {0x0180, 12}, {0x0280, 12}, {0x0C80, 12},
    {0x0080, 12}, {0x0B00, 13}, {0x1300, 13}, {0x001C,  5},
    {0x0064,  8}, {0x0380, 12}, {0x1900, 13}, {0x0D80, 12},
    {0x0018,  6}, {0x00BF,  8}, {0x0480, 12}, {0x0B80, 12},
    {0x0038,  6}, {0x0040,  9}, {0x0900, 13}, {0x0030,  7},
    {0x0780, 12}, {0x2800, 16}, {0x0010,  7}, {0x0A80, 12},
    {0x0050,  7}, {0x0880, 12}, {0x000F,  7}, {0x1100, 13},
    {0x002F,  7}, {0x0100, 13}, {0x0084,  8}, {0x5800, 16},
    {0x00A4,  8}, {0x9800, 16}, {0x0024,  8}, {0x1800, 16},
    {0x0140,  9}, {0xE800, 16}, {0x01C0,  9}, {0x6800, 16},
    {0x02C0, 10}, {0xA800, 16}, {0x0F80, 12}, {0x0580, 12},
    {0x0980, 12}, {0x0E80, 12}, {0x0680, 12}, {0x1F00, 13},
    {0x0F00, 13}, {0x1700, 13}, {0x0700, 13}, {0x1B00, 13},
    {0xF800, 16}, {0x7800, 16}, {0xB800, 16}, {0x3800, 16},
    {0xD800, 16},
    {0x0020,  6}, /* escape */
    {0x0006,  4}  /* EOB */
};

static const uint8_t speedhq_level[121] = {
     1,  2,  3,  4,  5,  6,  7,  8,
     9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40,
     1,  2,  3,  4,  5,  6,  7,  8,
     9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20,  1,  2,  3,  4,
     5,  6,  7,  8,  9, 10, 11,  1,
     2,  3,  4,  5,  1,  2,  3,  4,
     1,  2,  3,  1,  2,  3,  1,  2,
     1,  2,  1,  2,  1,  2,  1,  2,
     1,  2,  1,  2,  1,  2,  1,  2,
     1,  2,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,
     1,
};

static const uint8_t speedhq_run[121] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  3,
     3,  3,  3,  3,  4,  4,  4,  4,
     5,  5,  5,  6,  6,  6,  7,  7,
     8,  8,  9,  9, 10, 10, 11, 11,
    12, 12, 13, 13, 14, 14, 15, 15,
    16, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30,
    31,
};

RLTable ff_rl_speedhq = {
    121,
    121,
    speedhq_vlc,
    speedhq_run,
    speedhq_level,
};

#if CONFIG_SPEEDHQ_DECODER
/* NOTE: The first element is always 16, unscaled. */
static const uint8_t unscaled_quant_matrix[64] = {
    16, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83
};

static uint8_t speedhq_static_rl_table_store[2][2*MAX_RUN + MAX_LEVEL + 3];

static VLC dc_lum_vlc_le;
static VLC dc_chroma_vlc_le;
static VLC dc_alpha_run_vlc_le;
static VLC dc_alpha_level_vlc_le;

static inline int decode_dc_le(GetBitContext *gb, int component)
{
    int code, diff;

    if (component == 0 || component == 3) {
        code = get_vlc2(gb, dc_lum_vlc_le.table, DC_VLC_BITS, 2);
    } else {
        code = get_vlc2(gb, dc_chroma_vlc_le.table, DC_VLC_BITS, 2);
    }
    if (!code) {
        diff = 0;
    } else {
        diff = get_xbits_le(gb, code);
    }
    return diff;
}

static inline int decode_alpha_block(const SHQContext *s, GetBitContext *gb, uint8_t last_alpha[16], uint8_t *dest, int linesize)
{
    uint8_t block[128];
    int i = 0, x, y;

    memset(block, 0, sizeof(block));

    {
        OPEN_READER(re, gb);

        for ( ;; ) {
            int run, level;

            UPDATE_CACHE_LE(re, gb);
            GET_VLC(run, re, gb, dc_alpha_run_vlc_le.table, ALPHA_VLC_BITS, 2);

            if (run < 0) break;
            i += run;
            if (i >= 128)
                return AVERROR_INVALIDDATA;

            UPDATE_CACHE_LE(re, gb);
            GET_VLC(level, re, gb, dc_alpha_level_vlc_le.table, ALPHA_VLC_BITS, 2);
            block[i++] = level;
        }

        CLOSE_READER(re, gb);
    }

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 16; x++) {
            last_alpha[x] -= block[y * 16 + x];
        }
        memcpy(dest, last_alpha, 16);
        dest += linesize;
    }

    return 0;
}

static inline int decode_dct_block(const SHQContext *s, GetBitContext *gb, int last_dc[4], int component, uint8_t *dest, int linesize)
{
    const int *quant_matrix = s->quant_matrix;
    const uint8_t *scantable = s->intra_scantable.permutated;
    LOCAL_ALIGNED_32(int16_t, block, [64]);
    int dc_offset;

    s->bdsp.clear_block(block);

    dc_offset = decode_dc_le(gb, component);
    last_dc[component] -= dc_offset;  /* Note: Opposite of most codecs. */
    block[scantable[0]] = last_dc[component];  /* quant_matrix[0] is always 16. */

    /* Read AC coefficients. */
    {
        int i = 0;
        OPEN_READER(re, gb);
        for ( ;; ) {
            int level, run;
            UPDATE_CACHE_LE(re, gb);
            GET_RL_VLC(level, run, re, gb, ff_rl_speedhq.rl_vlc[0],
                       TEX_VLC_BITS, 2, 0);
            if (level == 127) {
                break;
            } else if (level) {
                i += run;
                if (i > MAX_INDEX)
                    return AVERROR_INVALIDDATA;
                /* If next bit is 1, level = -level */
                level = (level ^ SHOW_SBITS(re, gb, 1)) -
                        SHOW_SBITS(re, gb, 1);
                LAST_SKIP_BITS(re, gb, 1);
            } else {
                /* Escape. */
#if MIN_CACHE_BITS < 6 + 6 + 12
#error MIN_CACHE_BITS is too small for the escape code, add UPDATE_CACHE
#endif
                run = SHOW_UBITS(re, gb, 6) + 1;
                SKIP_BITS(re, gb, 6);
                level = SHOW_UBITS(re, gb, 12) - 2048;
                LAST_SKIP_BITS(re, gb, 12);

                i += run;
                if (i > MAX_INDEX)
                    return AVERROR_INVALIDDATA;
            }

            block[scantable[i]] = (level * quant_matrix[i]) >> 4;
        }
        CLOSE_READER(re, gb);
    }

    s->idsp.idct_put(dest, linesize, block);

    return 0;
}

static int decode_speedhq_border(const SHQContext *s, GetBitContext *gb, AVFrame *frame, int field_number, int line_stride)
{
    int linesize_y  = frame->linesize[0] * line_stride;
    int linesize_cb = frame->linesize[1] * line_stride;
    int linesize_cr = frame->linesize[2] * line_stride;
    int linesize_a;
    int ret;

    if (s->alpha_type != SHQ_NO_ALPHA)
        linesize_a = frame->linesize[3] * line_stride;

    for (int y = 0; y < frame->height; y += 16 * line_stride) {
        int last_dc[4] = { 1024, 1024, 1024, 1024 };
        uint8_t *dest_y, *dest_cb, *dest_cr, *dest_a;
        uint8_t last_alpha[16];
        int x = frame->width - 8;

        dest_y = frame->data[0] + frame->linesize[0] * (y + field_number) + x;
        if (s->subsampling == SHQ_SUBSAMPLING_420) {
            dest_cb = frame->data[1] + frame->linesize[1] * (y/2 + field_number) + x / 2;
            dest_cr = frame->data[2] + frame->linesize[2] * (y/2 + field_number) + x / 2;
        } else if (s->subsampling == SHQ_SUBSAMPLING_422) {
            dest_cb = frame->data[1] + frame->linesize[1] * (y + field_number) + x / 2;
            dest_cr = frame->data[2] + frame->linesize[2] * (y + field_number) + x / 2;
        }
        if (s->alpha_type != SHQ_NO_ALPHA) {
            memset(last_alpha, 255, sizeof(last_alpha));
            dest_a = frame->data[3] + frame->linesize[3] * (y + field_number) + x;
        }

        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8 * linesize_y, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8 * linesize_y + 8, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 1, dest_cb, linesize_cb)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 2, dest_cr, linesize_cr)) < 0)
            return ret;

        if (s->subsampling != SHQ_SUBSAMPLING_420) {
            if ((ret = decode_dct_block(s, gb, last_dc, 1, dest_cb + 8 * linesize_cb, linesize_cb)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 2, dest_cr + 8 * linesize_cr, linesize_cr)) < 0)
                return ret;
        }

        if (s->alpha_type == SHQ_RLE_ALPHA) {
            /* Alpha coded using 16x8 RLE blocks. */
            if ((ret = decode_alpha_block(s, gb, last_alpha, dest_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_alpha_block(s, gb, last_alpha, dest_a + 8 * linesize_a, linesize_a)) < 0)
                return ret;
        } else if (s->alpha_type == SHQ_DCT_ALPHA) {
            /* Alpha encoded exactly like luma. */
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8 * linesize_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8 * linesize_a + 8, linesize_a)) < 0)
                return ret;
        }
    }

    return 0;
}

static int decode_speedhq_field(const SHQContext *s, const uint8_t *buf, int buf_size, AVFrame *frame, int field_number, int start, int end, int line_stride)
{
    int ret, slice_number, slice_offsets[5];
    int linesize_y  = frame->linesize[0] * line_stride;
    int linesize_cb = frame->linesize[1] * line_stride;
    int linesize_cr = frame->linesize[2] * line_stride;
    int linesize_a;
    GetBitContext gb;

    if (s->alpha_type != SHQ_NO_ALPHA)
        linesize_a = frame->linesize[3] * line_stride;

    if (end < start || end - start < 3 || end > buf_size)
        return AVERROR_INVALIDDATA;

    slice_offsets[0] = start;
    slice_offsets[4] = end;
    for (slice_number = 1; slice_number < 4; slice_number++) {
        uint32_t last_offset, slice_len;

        last_offset = slice_offsets[slice_number - 1];
        slice_len = AV_RL24(buf + last_offset);
        slice_offsets[slice_number] = last_offset + slice_len;

        if (slice_len < 3 || slice_offsets[slice_number] > end - 3)
            return AVERROR_INVALIDDATA;
    }

    for (slice_number = 0; slice_number < 4; slice_number++) {
        uint32_t slice_begin, slice_end;
        int x, y;

        slice_begin = slice_offsets[slice_number];
        slice_end = slice_offsets[slice_number + 1];

        if ((ret = init_get_bits8(&gb, buf + slice_begin + 3, slice_end - slice_begin - 3)) < 0)
            return ret;

        for (y = slice_number * 16 * line_stride; y < frame->height; y += line_stride * 64) {
            uint8_t *dest_y, *dest_cb, *dest_cr, *dest_a;
            int last_dc[4] = { 1024, 1024, 1024, 1024 };
            uint8_t last_alpha[16];

            memset(last_alpha, 255, sizeof(last_alpha));

            dest_y = frame->data[0] + frame->linesize[0] * (y + field_number);
            if (s->subsampling == SHQ_SUBSAMPLING_420) {
                dest_cb = frame->data[1] + frame->linesize[1] * (y/2 + field_number);
                dest_cr = frame->data[2] + frame->linesize[2] * (y/2 + field_number);
            } else {
                dest_cb = frame->data[1] + frame->linesize[1] * (y + field_number);
                dest_cr = frame->data[2] + frame->linesize[2] * (y + field_number);
            }
            if (s->alpha_type != SHQ_NO_ALPHA) {
                dest_a = frame->data[3] + frame->linesize[3] * (y + field_number);
            }

            for (x = 0; x < frame->width - 8 * (s->subsampling != SHQ_SUBSAMPLING_444); x += 16) {
                /* Decode the four luma blocks. */
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8 * linesize_y, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8 * linesize_y + 8, linesize_y)) < 0)
                    return ret;

                /*
                 * Decode the first chroma block. For 4:2:0, this is the only one;
                 * for 4:2:2, it's the top block; for 4:4:4, it's the top-left block.
                 */
                if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb, linesize_cb)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr, linesize_cr)) < 0)
                    return ret;

                if (s->subsampling != SHQ_SUBSAMPLING_420) {
                    /* For 4:2:2, this is the bottom block; for 4:4:4, it's the bottom-left block. */
                    if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8 * linesize_cb, linesize_cb)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8 * linesize_cr, linesize_cr)) < 0)
                        return ret;

                    if (s->subsampling == SHQ_SUBSAMPLING_444) {
                        /* Top-right and bottom-right blocks. */
                        if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8, linesize_cb)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8, linesize_cr)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8 * linesize_cb + 8, linesize_cb)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8 * linesize_cr + 8, linesize_cr)) < 0)
                            return ret;

                        dest_cb += 8;
                        dest_cr += 8;
                    }
                }
                dest_y += 16;
                dest_cb += 8;
                dest_cr += 8;

                if (s->alpha_type == SHQ_RLE_ALPHA) {
                    /* Alpha coded using 16x8 RLE blocks. */
                    if ((ret = decode_alpha_block(s, &gb, last_alpha, dest_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_alpha_block(s, &gb, last_alpha, dest_a + 8 * linesize_a, linesize_a)) < 0)
                        return ret;
                    dest_a += 16;
                } else if (s->alpha_type == SHQ_DCT_ALPHA) {
                    /* Alpha encoded exactly like luma. */
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8 * linesize_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8 * linesize_a + 8, linesize_a)) < 0)
                        return ret;
                    dest_a += 16;
                }
            }
        }
    }

    if (s->subsampling != SHQ_SUBSAMPLING_444 && (frame->width & 15))
        return decode_speedhq_border(s, &gb, frame, field_number, line_stride);

    return 0;
}

static void compute_quant_matrix(int *output, int qscale)
{
    int i;
    for (i = 0; i < 64; i++) output[i] = unscaled_quant_matrix[ff_zigzag_direct[i]] * qscale;
}

static int speedhq_decode_frame(AVCodecContext *avctx,
                                void *data, int *got_frame,
                                AVPacket *avpkt)
{
    SHQContext * const s = avctx->priv_data;
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    AVFrame *frame       = data;
    uint8_t quality;
    uint32_t second_field_offset;
    int ret;

    if (buf_size < 4 || avctx->width < 8 || avctx->width % 8 != 0)
        return AVERROR_INVALIDDATA;
    if (buf_size < avctx->width*avctx->height / 64 / 4)
        return AVERROR_INVALIDDATA;

    quality = buf[0];
    if (quality >= 100) {
        return AVERROR_INVALIDDATA;
    }

    compute_quant_matrix(s->quant_matrix, 100 - quality);

    second_field_offset = AV_RL24(buf + 1);
    if (second_field_offset >= buf_size - 3) {
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_width = FFALIGN(avctx->width, 16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        return ret;
    }
    frame->key_frame = 1;

    if (second_field_offset == 4 || second_field_offset == (buf_size-4)) {
        /*
         * Overlapping first and second fields is used to signal
         * encoding only a single field. In this case, "height"
         * is ambiguous; it could mean either the height of the
         * frame as a whole, or of the field. The former would make
         * more sense for compatibility with legacy decoders,
         * but this matches the convention used in NDI, which is
         * the primary user of this trick.
         */
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 0, 4, buf_size, 1)) < 0)
            return ret;
    } else {
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 0, 4, second_field_offset, 2)) < 0)
            return ret;
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 1, second_field_offset, buf_size, 2)) < 0)
            return ret;
    }

    *got_frame = 1;
    return buf_size;
}

/*
 * Alpha VLC. Run and level are independently coded, and would be
 * outside the default limits for MAX_RUN/MAX_LEVEL, so we don't
 * bother with combining them into one table.
 */
static av_cold void compute_alpha_vlcs(void)
{
    uint16_t run_code[134], level_code[266];
    uint8_t run_bits[134], level_bits[266];
    int16_t run_symbols[134], level_symbols[266];
    int entry, i, sign;

    /* Initialize VLC for alpha run. */
    entry = 0;

    /* 0 -> 0. */
    run_code[entry] = 0;
    run_bits[entry] = 1;
    run_symbols[entry] = 0;
    ++entry;

    /* 10xx -> xx plus 1. */
    for (i = 0; i < 4; ++i) {
        run_code[entry] = (i << 2) | 1;
        run_bits[entry] = 4;
        run_symbols[entry] = i + 1;
        ++entry;
    }

    /* 111xxxxxxx -> xxxxxxx. */
    for (i = 0; i < 128; ++i) {
        run_code[entry] = (i << 3) | 7;
        run_bits[entry] = 10;
        run_symbols[entry] = i;
        ++entry;
    }

    /* 110 -> EOB. */
    run_code[entry] = 3;
    run_bits[entry] = 3;
    run_symbols[entry] = -1;
    ++entry;

    av_assert0(entry == FF_ARRAY_ELEMS(run_code));

    INIT_LE_VLC_SPARSE_STATIC(&dc_alpha_run_vlc_le, ALPHA_VLC_BITS,
                              FF_ARRAY_ELEMS(run_code),
                              run_bits, 1, 1,
                              run_code, 2, 2,
                              run_symbols, 2, 2, 160);

    /* Initialize VLC for alpha level. */
    entry = 0;

    for (sign = 0; sign <= 1; ++sign) {
        /* 1s -> -1 or +1 (depending on sign bit). */
        level_code[entry] = (sign << 1) | 1;
        level_bits[entry] = 2;
        level_symbols[entry] = sign ? -1 : 1;
        ++entry;

        /* 01sxx -> xx plus 2 (2..5 or -2..-5, depending on sign bit). */
        for (i = 0; i < 4; ++i) {
            level_code[entry] = (i << 3) | (sign << 2) | 2;
            level_bits[entry] = 5;
            level_symbols[entry] = sign ? -(i + 2) : (i + 2);
            ++entry;
        }
    }

    /*
     * 00xxxxxxxx -> xxxxxxxx, in two's complement. There are many codes
     * here that would better be encoded in other ways (e.g. 0 would be
     * encoded by increasing run, and +/- 1 would be encoded with a
     * shorter code), but it doesn't hurt to allow everything.
     */
    for (i = 0; i < 256; ++i) {
        level_code[entry] = i << 2;
        level_bits[entry] = 10;
        level_symbols[entry] = i;
        ++entry;
    }

    av_assert0(entry == FF_ARRAY_ELEMS(level_code));

    INIT_LE_VLC_SPARSE_STATIC(&dc_alpha_level_vlc_le, ALPHA_VLC_BITS,
                              FF_ARRAY_ELEMS(level_code),
                              level_bits, 1, 1,
                              level_code, 2, 2,
                              level_symbols, 2, 2, 288);
}

static av_cold void speedhq_static_init(void)
{
    /* Exactly the same as MPEG-2, except for a little-endian reader. */
    INIT_CUSTOM_VLC_STATIC(&dc_lum_vlc_le, DC_VLC_BITS, 12,
                           ff_mpeg12_vlc_dc_lum_bits, 1, 1,
                           ff_mpeg12_vlc_dc_lum_code, 2, 2,
                           INIT_VLC_OUTPUT_LE, 512);
    INIT_CUSTOM_VLC_STATIC(&dc_chroma_vlc_le, DC_VLC_BITS, 12,
                           ff_mpeg12_vlc_dc_chroma_bits, 1, 1,
                           ff_mpeg12_vlc_dc_chroma_code, 2, 2,
                           INIT_VLC_OUTPUT_LE, 514);

    ff_rl_init(&ff_rl_speedhq, speedhq_static_rl_table_store);
    INIT_2D_VLC_RL(ff_rl_speedhq, 674, INIT_VLC_LE);

    compute_alpha_vlcs();
}

static av_cold int speedhq_decode_init(AVCodecContext *avctx)
{
    int ret;
    static AVOnce init_once = AV_ONCE_INIT;
    SHQContext * const s = avctx->priv_data;

    s->avctx = avctx;

    ret = ff_thread_once(&init_once, speedhq_static_init);
    if (ret)
        return AVERROR_UNKNOWN;

    ff_blockdsp_init(&s->bdsp, avctx);
    ff_idctdsp_init(&s->idsp, avctx);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable, ff_zigzag_direct);

    switch (avctx->codec_tag) {
    case MKTAG('S', 'H', 'Q', '0'):
        s->subsampling = SHQ_SUBSAMPLING_420;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        break;
    case MKTAG('S', 'H', 'Q', '1'):
        s->subsampling = SHQ_SUBSAMPLING_420;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA420P;
        break;
    case MKTAG('S', 'H', 'Q', '2'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        break;
    case MKTAG('S', 'H', 'Q', '3'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        break;
    case MKTAG('S', 'H', 'Q', '4'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        break;
    case MKTAG('S', 'H', 'Q', '5'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    case MKTAG('S', 'H', 'Q', '7'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_DCT_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        break;
    case MKTAG('S', 'H', 'Q', '9'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_DCT_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown NewTek SpeedHQ FOURCC provided (%08X)\n",
               avctx->codec_tag);
        return AVERROR_INVALIDDATA;
    }

    /* This matches what NDI's RGB -> Y'CbCr 4:2:2 converter uses. */
    avctx->colorspace = AVCOL_SPC_BT470BG;
    avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;

    return 0;
}

AVCodec ff_speedhq_decoder = {
    .name           = "speedhq",
    .long_name      = NULL_IF_CONFIG_SMALL("NewTek SpeedHQ"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SPEEDHQ,
    .priv_data_size = sizeof(SHQContext),
    .init           = speedhq_decode_init,
    .decode         = speedhq_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif /* CONFIG_SPEEDHQ_DECODER */
