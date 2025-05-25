/*
 * FFV1 encoder
 *
 * Copyright (c) 2003-2013 Michael Niedermayer <michaelni@gmx.at>
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
 * FF Video Codec 1 (a lossless codec) encoder
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"

#include "avcodec.h"
#include "encode.h"
#include "codec_internal.h"
#include "put_bits.h"
#include "put_golomb.h"
#include "rangecoder.h"
#include "ffv1.h"
#include "ffv1enc.h"

static const int8_t quant5_10bit[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -0, -0, -0, -0, -0, -0, -0, -0, -0, -0,
};

static const int8_t quant5[256] = {
     0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1,
};

static const int8_t quant9_10bit[256] = {
     0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -3, -3, -3,
    -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
    -3, -3, -3, -3, -3, -3, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -1, -1, -1, -1, -1, -1, -1, -1, -0, -0, -0, -0,
};

static const int8_t quant11[256] = {
     0,  1,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -4, -4,
    -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
    -4, -4, -4, -4, -4, -3, -3, -3, -3, -3, -3, -3, -2, -2, -2, -1,
};

static const uint8_t ver2_state[256] = {
      0,  10,  10,  10,  10,  16,  16,  16, 28,   16,  16,  29,  42,  49,  20,  49,
     59,  25,  26,  26,  27,  31,  33,  33, 33,   34,  34,  37,  67,  38,  39,  39,
     40,  40,  41,  79,  43,  44,  45,  45, 48,   48,  64,  50,  51,  52,  88,  52,
     53,  74,  55,  57,  58,  58,  74,  60, 101,  61,  62,  84,  66,  66,  68,  69,
     87,  82,  71,  97,  73,  73,  82,  75, 111,  77,  94,  78,  87,  81,  83,  97,
     85,  83,  94,  86,  99,  89,  90,  99, 111,  92,  93,  134, 95,  98, 105,  98,
    105, 110, 102, 108, 102, 118, 103, 106, 106, 113, 109, 112, 114, 112, 116, 125,
    115, 116, 117, 117, 126, 119, 125, 121, 121, 123, 145, 124, 126, 131, 127, 129,
    165, 130, 132, 138, 133, 135, 145, 136, 137, 139, 146, 141, 143, 142, 144, 148,
    147, 155, 151, 149, 151, 150, 152, 157, 153, 154, 156, 168, 158, 162, 161, 160,
    172, 163, 169, 164, 166, 184, 167, 170, 177, 174, 171, 173, 182, 176, 180, 178,
    175, 189, 179, 181, 186, 183, 192, 185, 200, 187, 191, 188, 190, 197, 193, 196,
    197, 194, 195, 196, 198, 202, 199, 201, 210, 203, 207, 204, 205, 206, 208, 214,
    209, 211, 221, 212, 213, 215, 224, 216, 217, 218, 219, 220, 222, 228, 223, 225,
    226, 224, 227, 229, 240, 230, 231, 232, 233, 234, 235, 236, 238, 239, 237, 242,
    241, 243, 242, 244, 245, 246, 247, 248, 249, 250, 251, 252, 252, 253, 254, 255,
};

static void find_best_state(uint8_t best_state[256][256],
                            const uint8_t one_state[256])
{
    int i, j, k, m;
    uint32_t l2tab[256];

    for (i = 1; i < 256; i++)
        l2tab[i] = -log2(i / 256.0) * ((1U << 31) / 8);

    for (i = 0; i < 256; i++) {
        uint64_t best_len[256];

        for (j = 0; j < 256; j++)
            best_len[j] = UINT64_MAX;

        for (j = FFMAX(i - 10, 1); j < FFMIN(i + 11, 256); j++) {
            uint32_t occ[256] = { 0 };
            uint64_t len      = 0;
            occ[j] = UINT32_MAX;

            if (!one_state[j])
                continue;

            for (k = 0; k < 256; k++) {
                uint32_t newocc[256] = { 0 };
                for (m = 1; m < 256; m++)
                    if (occ[m]) {
                        len += (occ[m]*((       i *(uint64_t)l2tab[    m]
                                         + (256-i)*(uint64_t)l2tab[256-m])>>8)) >> 8;
                    }
                if (len < best_len[k]) {
                    best_len[k]      = len;
                    best_state[i][k] = j;
                }
                for (m = 1; m < 256; m++)
                    if (occ[m]) {
                        newocc[      one_state[      m]] += occ[m] * (uint64_t)       i  >> 8;
                        newocc[256 - one_state[256 - m]] += occ[m] * (uint64_t)(256 - i) >> 8;
                    }
                memcpy(occ, newocc, sizeof(occ));
            }
        }
    }
}

static av_always_inline av_flatten void put_symbol_inline(RangeCoder *c,
                                                          uint8_t *state, int v,
                                                          int is_signed,
                                                          uint64_t rc_stat[256][2],
                                                          uint64_t rc_stat2[32][2])
{
    int i;

#define put_rac(C, S, B)                        \
    do {                                        \
        if (rc_stat) {                          \
            rc_stat[*(S)][B]++;                 \
            rc_stat2[(S) - state][B]++;         \
        }                                       \
        put_rac(C, S, B);                       \
    } while (0)

    if (v) {
        const unsigned a = is_signed ? FFABS(v) : v;
        const int e = av_log2(a);
        put_rac(c, state + 0, 0);
        if (e <= 9) {
            for (i = 0; i < e; i++)
                put_rac(c, state + 1 + i, 1);  // 1..10
            put_rac(c, state + 1 + i, 0);

            for (i = e - 1; i >= 0; i--)
                put_rac(c, state + 22 + i, (a >> i) & 1);  // 22..31

            if (is_signed)
                put_rac(c, state + 11 + e, v < 0);  // 11..21
        } else {
            for (i = 0; i < e; i++)
                put_rac(c, state + 1 + FFMIN(i, 9), 1);  // 1..10
            put_rac(c, state + 1 + 9, 0);

            for (i = e - 1; i >= 0; i--)
                put_rac(c, state + 22 + FFMIN(i, 9), (a >> i) & 1);  // 22..31

            if (is_signed)
                put_rac(c, state + 11 + 10, v < 0);  // 11..21
        }
    } else {
        put_rac(c, state + 0, 1);
    }
#undef put_rac
}

static av_noinline void put_symbol(RangeCoder *c, uint8_t *state,
                                   int v, int is_signed)
{
    put_symbol_inline(c, state, v, is_signed, NULL, NULL);
}


static inline void put_vlc_symbol(PutBitContext *pb, VlcState *const state,
                                  int v, int bits)
{
    int i, k, code;
    v = fold(v - state->bias, bits);

    i = state->count;
    k = 0;
    while (i < state->error_sum) { // FIXME: optimize
        k++;
        i += i;
    }

    av_assert2(k <= 16);

    code = v ^ ((2 * state->drift + state->count) >> 31);

    ff_dlog(NULL, "v:%d/%d bias:%d error:%d drift:%d count:%d k:%d\n", v, code,
            state->bias, state->error_sum, state->drift, state->count, k);
    set_sr_golomb(pb, code, k, 12, bits);

    update_vlc_state(state, v);
}

#define TYPE int16_t
#define RENAME(name) name
#include "ffv1enc_template.c"
#undef TYPE
#undef RENAME

#define TYPE int32_t
#define RENAME(name) name ## 32
#include "ffv1enc_template.c"

static int encode_plane(FFV1Context *f, FFV1SliceContext *sc,
                        const uint8_t *src, int w, int h,
                        int stride, int plane_index, int remap_index, int pixel_stride, int ac)
{
    int x, y, i, ret;
    const int pass1 = !!(f->avctx->flags & AV_CODEC_FLAG_PASS1);
    const int ring_size = f->context_model ? 3 : 2;
    int16_t *sample[3];
    sc->run_index = 0;

    memset(sc->sample_buffer, 0, ring_size * (w + 6) * sizeof(*sc->sample_buffer));

    for (y = 0; y < h; y++) {
        for (i = 0; i < ring_size; i++)
            sample[i] = sc->sample_buffer + (w + 6) * ((h + i - y) % ring_size) + 3;

        sample[0][-1]= sample[1][0  ];
        sample[1][ w]= sample[1][w-1];

        if (f->bits_per_raw_sample <= 8) {
            for (x = 0; x < w; x++)
                sample[0][x] = src[x * pixel_stride + stride * y];
            if (sc->remap)
                for (x = 0; x < w; x++)
                    sample[0][x] = sc->fltmap[remap_index][ sample[0][x] ];

            if((ret = encode_line(f, sc, f->avctx, w, sample, plane_index, 8, ac, pass1)) < 0)
                return ret;
        } else {
            if (f->packed_at_lsb) {
                for (x = 0; x < w; x++) {
                    sample[0][x] = ((uint16_t*)(src + stride*y))[x * pixel_stride];
                }
            } else {
                for (x = 0; x < w; x++) {
                    sample[0][x] = ((uint16_t*)(src + stride*y))[x * pixel_stride] >> (16 - f->bits_per_raw_sample);
                }
            }
            if (sc->remap)
                for (x = 0; x < w; x++)
                    sample[0][x] = sc->fltmap[remap_index][ (uint16_t)sample[0][x] ];

            if((ret = encode_line(f, sc, f->avctx, w, sample, plane_index, f->bits_per_raw_sample, ac, pass1)) < 0)
                return ret;
        }
    }
    return 0;
}

static void load_plane(FFV1Context *f, FFV1SliceContext *sc,
                      const uint8_t *src, int w, int h,
                      int stride, int remap_index, int pixel_stride)
{
    int x, y;

    memset(sc->fltmap[remap_index], 0, 65536 * sizeof(*sc->fltmap[remap_index]));

    for (y = 0; y < h; y++) {
        if (f->bits_per_raw_sample <= 8) {
            for (x = 0; x < w; x++)
                sc->fltmap[remap_index][ src[x * pixel_stride + stride * y] ] = 1;
        } else {
            if (f->packed_at_lsb) {
                for (x = 0; x < w; x++)
                    sc->fltmap[remap_index][ ((uint16_t*)(src + stride*y))[x * pixel_stride] ] = 1;
            } else {
                for (x = 0; x < w; x++)
                    sc->fltmap[remap_index][ ((uint16_t*)(src + stride*y))[x * pixel_stride] >> (16 - f->bits_per_raw_sample) ] = 1;
            }
        }
    }
}

static void write_quant_table(RangeCoder *c, int16_t *quant_table)
{
    int last = 0;
    int i;
    uint8_t state[CONTEXT_SIZE];
    memset(state, 128, sizeof(state));

    for (i = 1; i < MAX_QUANT_TABLE_SIZE/2; i++)
        if (quant_table[i] != quant_table[i - 1]) {
            put_symbol(c, state, i - last - 1, 0);
            last = i;
        }
    put_symbol(c, state, i - last - 1, 0);
}

static void write_quant_tables(RangeCoder *c,
                               int16_t quant_table[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE])
{
    int i;
    for (i = 0; i < 5; i++)
        write_quant_table(c, quant_table[i]);
}

static int contains_non_128(uint8_t (*initial_state)[CONTEXT_SIZE],
                            int nb_contexts)
{
    if (!initial_state)
        return 0;
    for (int i = 0; i < nb_contexts; i++)
        for (int j = 0; j < CONTEXT_SIZE; j++)
            if (initial_state[i][j] != 128)
                return 1;
    return 0;
}

static void write_header(FFV1Context *f)
{
    uint8_t state[CONTEXT_SIZE];
    int i, j;
    RangeCoder *const c = &f->slices[0].c;

    memset(state, 128, sizeof(state));

    if (f->version < 2) {
        put_symbol(c, state, f->version, 0);
        put_symbol(c, state, f->ac, 0);
        if (f->ac == AC_RANGE_CUSTOM_TAB) {
            for (i = 1; i < 256; i++)
                put_symbol(c, state,
                           f->state_transition[i] - c->one_state[i], 1);
        }
        put_symbol(c, state, f->colorspace, 0); //YUV cs type
        if (f->version > 0)
            put_symbol(c, state, f->bits_per_raw_sample, 0);
        put_rac(c, state, f->chroma_planes);
        put_symbol(c, state, f->chroma_h_shift, 0);
        put_symbol(c, state, f->chroma_v_shift, 0);
        put_rac(c, state, f->transparency);

        write_quant_tables(c, f->quant_tables[f->context_model]);
    } else if (f->version < 3) {
        put_symbol(c, state, f->slice_count, 0);
        for (i = 0; i < f->slice_count; i++) {
            FFV1SliceContext *fs = &f->slices[i];
            put_symbol(c, state,
                       (fs->slice_x      + 1) * f->num_h_slices / f->width, 0);
            put_symbol(c, state,
                       (fs->slice_y      + 1) * f->num_v_slices / f->height, 0);
            put_symbol(c, state,
                       (fs->slice_width  + 1) * f->num_h_slices / f->width - 1,
                       0);
            put_symbol(c, state,
                       (fs->slice_height + 1) * f->num_v_slices / f->height - 1,
                       0);
            for (j = 0; j < f->plane_count; j++) {
                put_symbol(c, state, fs->plane[j].quant_table_index, 0);
                av_assert0(fs->plane[j].quant_table_index == f->context_model);
            }
        }
    }
}

static void set_micro_version(FFV1Context *f)
{
    f->combined_version = f->version << 16;
    if (f->version > 2) {
        if (f->version == 3) {
            f->micro_version = 4;
        } else if (f->version == 4) {
            f->micro_version = 8;
        } else
            av_assert0(0);

        f->combined_version += f->micro_version;
    } else
        av_assert0(f->micro_version == 0);
}

av_cold int ff_ffv1_write_extradata(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;

    RangeCoder c;
    uint8_t state[CONTEXT_SIZE];
    int i, j, k;
    uint8_t state2[32][CONTEXT_SIZE];
    unsigned v;

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    f->avctx->extradata_size = 10000 + 4 +
                                    (11 * 11 * 5 * 5 * 5 + 11 * 11 * 11) * 32;
    f->avctx->extradata = av_malloc(f->avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!f->avctx->extradata)
        return AVERROR(ENOMEM);
    ff_init_range_encoder(&c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(&c, 0.05 * (1LL << 32), 256 - 8);

    put_symbol(&c, state, f->version, 0);
    if (f->version > 2)
        put_symbol(&c, state, f->micro_version, 0);

    put_symbol(&c, state, f->ac, 0);
    if (f->ac == AC_RANGE_CUSTOM_TAB)
        for (i = 1; i < 256; i++)
            put_symbol(&c, state, f->state_transition[i] - c.one_state[i], 1);

    put_symbol(&c, state, f->colorspace, 0); // YUV cs type
    put_symbol(&c, state, f->bits_per_raw_sample, 0);
    put_rac(&c, state, f->chroma_planes);
    put_symbol(&c, state, f->chroma_h_shift, 0);
    put_symbol(&c, state, f->chroma_v_shift, 0);
    put_rac(&c, state, f->transparency);
    put_symbol(&c, state, f->num_h_slices - 1, 0);
    put_symbol(&c, state, f->num_v_slices - 1, 0);

    put_symbol(&c, state, f->quant_table_count, 0);
    for (i = 0; i < f->quant_table_count; i++)
        write_quant_tables(&c, f->quant_tables[i]);

    for (i = 0; i < f->quant_table_count; i++) {
        if (contains_non_128(f->initial_states[i], f->context_count[i])) {
            put_rac(&c, state, 1);
            for (j = 0; j < f->context_count[i]; j++)
                for (k = 0; k < CONTEXT_SIZE; k++) {
                    int pred = j ? f->initial_states[i][j - 1][k] : 128;
                    put_symbol(&c, state2[k],
                               (int8_t)(f->initial_states[i][j][k] - pred), 1);
                }
        } else {
            put_rac(&c, state, 0);
        }
    }

    if (f->version > 2) {
        put_symbol(&c, state, f->ec, 0);
        put_symbol(&c, state, f->intra = (f->avctx->gop_size < 2), 0);
    }

    f->avctx->extradata_size = ff_rac_terminate(&c, 0);
    v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref, f->avctx->extradata, f->avctx->extradata_size) ^ (f->crcref ? 0x8CD88196 : 0);
    AV_WL32(f->avctx->extradata + f->avctx->extradata_size, v);
    f->avctx->extradata_size += 4;

    return 0;
}

static int sort_stt(FFV1Context *s, uint8_t stt[256])
{
    int i, i2, changed, print = 0;

    do {
        changed = 0;
        for (i = 12; i < 244; i++) {
            for (i2 = i + 1; i2 < 245 && i2 < i + 4; i2++) {

#define COST(old, new)                                      \
    s->rc_stat[old][0] * -log2((256 - (new)) / 256.0) +     \
    s->rc_stat[old][1] * -log2((new)         / 256.0)

#define COST2(old, new)                         \
    COST(old, new) + COST(256 - (old), 256 - (new))

                double size0 = COST2(i,  i) + COST2(i2, i2);
                double sizeX = COST2(i, i2) + COST2(i2, i);
                if (size0 - sizeX > size0*(1e-14) && i != 128 && i2 != 128) {
                    int j;
                    FFSWAP(int, stt[i], stt[i2]);
                    FFSWAP(int, s->rc_stat[i][0], s->rc_stat[i2][0]);
                    FFSWAP(int, s->rc_stat[i][1], s->rc_stat[i2][1]);
                    if (i != 256 - i2) {
                        FFSWAP(int, stt[256 - i], stt[256 - i2]);
                        FFSWAP(int, s->rc_stat[256 - i][0], s->rc_stat[256 - i2][0]);
                        FFSWAP(int, s->rc_stat[256 - i][1], s->rc_stat[256 - i2][1]);
                    }
                    for (j = 1; j < 256; j++) {
                        if (stt[j] == i)
                            stt[j] = i2;
                        else if (stt[j] == i2)
                            stt[j] = i;
                        if (i != 256 - i2) {
                            if (stt[256 - j] == 256 - i)
                                stt[256 - j] = 256 - i2;
                            else if (stt[256 - j] == 256 - i2)
                                stt[256 - j] = 256 - i;
                        }
                    }
                    print = changed = 1;
                }
            }
        }
    } while (changed);
    return print;
}


int ff_ffv1_encode_determine_slices(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int plane_count = 1 + 2*s->chroma_planes + s->transparency;
    int max_h_slices = AV_CEIL_RSHIFT(avctx->width , s->chroma_h_shift);
    int max_v_slices = AV_CEIL_RSHIFT(avctx->height, s->chroma_v_shift);
    s->num_v_slices = (avctx->width > 352 || avctx->height > 288 || !avctx->slices) ? 2 : 1;
    s->num_v_slices = FFMIN(s->num_v_slices, max_v_slices);
    for (; s->num_v_slices <= 32; s->num_v_slices++) {
        for (s->num_h_slices = s->num_v_slices; s->num_h_slices <= 2*s->num_v_slices; s->num_h_slices++) {
            int maxw = (avctx->width  + s->num_h_slices - 1) / s->num_h_slices;
            int maxh = (avctx->height + s->num_v_slices - 1) / s->num_v_slices;
            if (s->num_h_slices > max_h_slices || s->num_v_slices > max_v_slices)
                continue;
            if (maxw * maxh * (int64_t)(s->bits_per_raw_sample+1) * plane_count > 8<<24)
                continue;
            if (s->version < 4)
                if (  ff_need_new_slices(avctx->width , s->num_h_slices, s->chroma_h_shift)
                    ||ff_need_new_slices(avctx->height, s->num_v_slices, s->chroma_v_shift))
                    continue;
            if (avctx->slices == s->num_h_slices * s->num_v_slices && avctx->slices <= MAX_SLICES)
                return 0;
            if (maxw*maxh > 360*288)
                continue;
            if (!avctx->slices)
                return 0;
        }
    }
    av_log(avctx, AV_LOG_ERROR,
           "Unsupported number %d of slices requested, please specify a "
           "supported number with -slices (ex:4,6,9,12,16, ...)\n",
           avctx->slices);
    return AVERROR(ENOSYS);
}

av_cold int ff_ffv1_encode_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i, j, k, m, ret;

    if ((avctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2)) ||
        avctx->slices > 1)
        s->version = FFMAX(s->version, 2);

    if ((avctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2)) && s->ac == AC_GOLOMB_RICE) {
        av_log(avctx, AV_LOG_ERROR, "2 Pass mode is not possible with golomb coding\n");
        return AVERROR(EINVAL);
    }

    // Unspecified level & slices, we choose version 1.2+ to ensure multithreaded decodability
    if (avctx->slices == 0 && avctx->level < 0 && avctx->width * avctx->height > 720*576)
        s->version = FFMAX(s->version, 2);

    if (avctx->level <= 0 && s->version == 2) {
        s->version = 3;
    }
    if (avctx->level >= 0 && avctx->level <= 4) {
        if (avctx->level < s->version) {
            av_log(avctx, AV_LOG_ERROR, "Version %d needed for requested features but %d requested\n", s->version, avctx->level);
            return AVERROR(EINVAL);
        }
        s->version = avctx->level;
    } else if (s->version < 3)
        s->version = 3;

    if (s->ec < 0) {
        if (s->version >= 4) {
            s->ec = 2;
        } else if (s->version >= 3) {
            s->ec = 1;
        } else
            s->ec = 0;
    }

    // CRC requires version 3+
    if (s->ec == 1)
        s->version = FFMAX(s->version, 3);
    if (s->ec == 2) {
        s->version = FFMAX(s->version, 4);
        s->crcref = 0x7a8c4079;
    }

    if ((s->version == 2 || s->version>3) && avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_ERROR, "Version 2 or 4 needed for requested features but version 2 or 4 is experimental and not enabled\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->ac == AC_RANGE_CUSTOM_TAB) {
        for (i = 1; i < 256; i++)
            s->state_transition[i] = ver2_state[i];
    } else {
        RangeCoder c;
        ff_build_rac_states(&c, 0.05 * (1LL << 32), 256 - 8);
        for (i = 1; i < 256; i++)
            s->state_transition[i] = c.one_state[i];
    }

    for (i = 0; i < 256; i++) {
        s->quant_table_count = 2;
        if ((s->qtable == -1 && s->bits_per_raw_sample <= 8) || s->qtable == 1) {
            s->quant_tables[0][0][i]=           quant11[i];
            s->quant_tables[0][1][i]=        11*quant11[i];
            s->quant_tables[0][2][i]=     11*11*quant11[i];
            s->quant_tables[1][0][i]=           quant11[i];
            s->quant_tables[1][1][i]=        11*quant11[i];
            s->quant_tables[1][2][i]=     11*11*quant5 [i];
            s->quant_tables[1][3][i]=   5*11*11*quant5 [i];
            s->quant_tables[1][4][i]= 5*5*11*11*quant5 [i];
            s->context_count[0] = (11 * 11 * 11        + 1) / 2;
            s->context_count[1] = (11 * 11 * 5 * 5 * 5 + 1) / 2;
        } else {
            s->quant_tables[0][0][i]=           quant9_10bit[i];
            s->quant_tables[0][1][i]=         9*quant9_10bit[i];
            s->quant_tables[0][2][i]=       9*9*quant9_10bit[i];
            s->quant_tables[1][0][i]=           quant9_10bit[i];
            s->quant_tables[1][1][i]=         9*quant9_10bit[i];
            s->quant_tables[1][2][i]=       9*9*quant5_10bit[i];
            s->quant_tables[1][3][i]=     5*9*9*quant5_10bit[i];
            s->quant_tables[1][4][i]=   5*5*9*9*quant5_10bit[i];
            s->context_count[0] = (9 * 9 * 9         + 1) / 2;
            s->context_count[1] = (9 * 9 * 5 * 5 * 5 + 1) / 2;
        }
    }

    if ((ret = ff_ffv1_allocate_initial_states(s)) < 0)
        return ret;

    if (!s->transparency)
        s->plane_count = 2;
    if (!s->chroma_planes && s->version > 3)
        s->plane_count--;

    s->picture_number = 0;

    if (avctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2)) {
        for (i = 0; i < s->quant_table_count; i++) {
            s->rc_stat2[i] = av_mallocz(s->context_count[i] *
                                        sizeof(*s->rc_stat2[i]));
            if (!s->rc_stat2[i])
                return AVERROR(ENOMEM);
        }
    }
    if (avctx->stats_in) {
        char *p = avctx->stats_in;
        uint8_t (*best_state)[256] = av_malloc_array(256, 256);
        int gob_count = 0;
        char *next;
        if (!best_state)
            return AVERROR(ENOMEM);

        av_assert0(s->version >= 2);

        for (;;) {
            for (j = 0; j < 256; j++)
                for (i = 0; i < 2; i++) {
                    s->rc_stat[j][i] = strtol(p, &next, 0);
                    if (next == p) {
                        av_log(avctx, AV_LOG_ERROR,
                               "2Pass file invalid at %d %d [%s]\n", j, i, p);
                        av_freep(&best_state);
                        return AVERROR_INVALIDDATA;
                    }
                    p = next;
                }
            for (i = 0; i < s->quant_table_count; i++)
                for (j = 0; j < s->context_count[i]; j++) {
                    for (k = 0; k < 32; k++)
                        for (m = 0; m < 2; m++) {
                            s->rc_stat2[i][j][k][m] = strtol(p, &next, 0);
                            if (next == p) {
                                av_log(avctx, AV_LOG_ERROR,
                                       "2Pass file invalid at %d %d %d %d [%s]\n",
                                       i, j, k, m, p);
                                av_freep(&best_state);
                                return AVERROR_INVALIDDATA;
                            }
                            p = next;
                        }
                }
            gob_count = strtol(p, &next, 0);
            if (next == p || gob_count <= 0) {
                av_log(avctx, AV_LOG_ERROR, "2Pass file invalid\n");
                av_freep(&best_state);
                return AVERROR_INVALIDDATA;
            }
            p = next;
            while (*p == '\n' || *p == ' ')
                p++;
            if (p[0] == 0)
                break;
        }
        if (s->ac == AC_RANGE_CUSTOM_TAB)
            sort_stt(s, s->state_transition);

        find_best_state(best_state, s->state_transition);

        for (i = 0; i < s->quant_table_count; i++) {
            for (k = 0; k < 32; k++) {
                double a=0, b=0;
                int jp = 0;
                for (j = 0; j < s->context_count[i]; j++) {
                    double p = 128;
                    if (s->rc_stat2[i][j][k][0] + s->rc_stat2[i][j][k][1] > 200 && j || a+b > 200) {
                        if (a+b)
                            p = 256.0 * b / (a + b);
                        s->initial_states[i][jp][k] =
                            best_state[av_clip(round(p), 1, 255)][av_clip_uint8((a + b) / gob_count)];
                        for(jp++; jp<j; jp++)
                            s->initial_states[i][jp][k] = s->initial_states[i][jp-1][k];
                        a=b=0;
                    }
                    a += s->rc_stat2[i][j][k][0];
                    b += s->rc_stat2[i][j][k][1];
                    if (a+b) {
                        p = 256.0 * b / (a + b);
                    }
                    s->initial_states[i][j][k] =
                        best_state[av_clip(round(p), 1, 255)][av_clip_uint8((a + b) / gob_count)];
                }
            }
        }
        av_freep(&best_state);
    }

    if (s->version <= 1) {
        /* Disable slices when the version doesn't support them */
        s->num_h_slices = 1;
        s->num_v_slices = 1;
    }

    set_micro_version(s);

    return 0;
}

av_cold int ff_ffv1_encode_setup_plane_info(AVCodecContext *avctx,
                                            enum AVPixelFormat pix_fmt)
{
    FFV1Context *s = avctx->priv_data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    s->plane_count = 3;
    switch(pix_fmt) {
    case AV_PIX_FMT_GRAY9:
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUVA444P9:
    case AV_PIX_FMT_YUVA422P9:
    case AV_PIX_FMT_YUVA420P9:
        if (!avctx->bits_per_raw_sample)
            s->bits_per_raw_sample = 9;
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV440P10:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUVA444P10:
    case AV_PIX_FMT_YUVA422P10:
    case AV_PIX_FMT_YUVA420P10:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 10;
    case AV_PIX_FMT_GRAY12:
    case AV_PIX_FMT_YUV444P12:
    case AV_PIX_FMT_YUV440P12:
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV422P12:
    case AV_PIX_FMT_YUVA444P12:
    case AV_PIX_FMT_YUVA422P12:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 12;
    case AV_PIX_FMT_GRAY14:
    case AV_PIX_FMT_YUV444P14:
    case AV_PIX_FMT_YUV420P14:
    case AV_PIX_FMT_YUV422P14:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 14;
        s->packed_at_lsb = 1;
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_P016:
    case AV_PIX_FMT_P216:
    case AV_PIX_FMT_P416:
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUVA444P16:
    case AV_PIX_FMT_YUVA422P16:
    case AV_PIX_FMT_YUVA420P16:
    case AV_PIX_FMT_GRAYF16:
    case AV_PIX_FMT_YAF16:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample) {
            s->bits_per_raw_sample = 16;
        } else if (!s->bits_per_raw_sample) {
            s->bits_per_raw_sample = avctx->bits_per_raw_sample;
        }
        if (s->bits_per_raw_sample <= 8) {
            av_log(avctx, AV_LOG_ERROR, "bits_per_raw_sample invalid\n");
            return AVERROR_INVALIDDATA;
        }
        s->version = FFMAX(s->version, 1);
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YA8:
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV16:
    case AV_PIX_FMT_NV24:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA420P:
        s->chroma_planes = desc->nb_components < 3 ? 0 : 1;
        s->colorspace = 0;
        s->transparency = !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 8;
        else if (!s->bits_per_raw_sample)
            s->bits_per_raw_sample = 8;
        break;
    case AV_PIX_FMT_RGB32:
        s->colorspace = 1;
        s->transparency = 1;
        s->chroma_planes = 1;
        s->bits_per_raw_sample = 8;
        break;
    case AV_PIX_FMT_RGBA64:
        s->colorspace = 1;
        s->transparency = 1;
        s->chroma_planes = 1;
        s->bits_per_raw_sample = 16;
        s->use32bit = 1;
        s->version = FFMAX(s->version, 1);
        break;
    case AV_PIX_FMT_RGB48:
        s->colorspace = 1;
        s->chroma_planes = 1;
        s->bits_per_raw_sample = 16;
        s->use32bit = 1;
        s->version = FFMAX(s->version, 1);
        break;
    case AV_PIX_FMT_0RGB32:
        s->colorspace = 1;
        s->chroma_planes = 1;
        s->bits_per_raw_sample = 8;
        break;
    case AV_PIX_FMT_GBRP9:
        if (!avctx->bits_per_raw_sample)
            s->bits_per_raw_sample = 9;
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRAP10:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 10;
    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRAP12:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 12;
    case AV_PIX_FMT_GBRP14:
    case AV_PIX_FMT_GBRAP14:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 14;
    case AV_PIX_FMT_GBRP16:
    case AV_PIX_FMT_GBRAP16:
    case AV_PIX_FMT_GBRPF16:
    case AV_PIX_FMT_GBRAPF16:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 16;
    case AV_PIX_FMT_GBRPF32:
    case AV_PIX_FMT_GBRAPF32:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 32;
        else if (!s->bits_per_raw_sample)
            s->bits_per_raw_sample = avctx->bits_per_raw_sample;
        s->transparency = !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
        s->colorspace = 1;
        s->chroma_planes = 1;
        if (s->bits_per_raw_sample >= 16) {
            s->use32bit = 1;
        }
        s->version = FFMAX(s->version, 1);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "format %s not supported\n",
               av_get_pix_fmt_name(pix_fmt));
        return AVERROR(ENOSYS);
    }
    s->flt = !!(desc->flags & AV_PIX_FMT_FLAG_FLOAT);
    if (s->flt || s->remap_mode > 0)
        s->version = FFMAX(s->version, 4);
    av_assert0(s->bits_per_raw_sample >= 8);

    if (s->remap_mode < 0)
        s->remap_mode = s->flt ? 2 : 0;
    if (s->remap_mode == 0 && s->bits_per_raw_sample == 32) {
        av_log(avctx, AV_LOG_ERROR, "32bit requires remap\n");
        return AVERROR(EINVAL);
    }
    if (s->remap_mode == 2 &&
        !((s->bits_per_raw_sample == 16 || s->bits_per_raw_sample == 32 || s->bits_per_raw_sample == 64) && s->flt)) {
        av_log(avctx, AV_LOG_ERROR, "remap 2 is for float16/32/64 only\n");
        return AVERROR(EINVAL);
    }

    return av_pix_fmt_get_chroma_sub_sample(pix_fmt, &s->chroma_h_shift, &s->chroma_v_shift);
}

static av_cold int encode_init_internal(AVCodecContext *avctx)
{
    int ret;
    FFV1Context *s = avctx->priv_data;

    if ((ret = ff_ffv1_common_init(avctx, s)) < 0)
        return ret;

    if (s->ac == 1) // Compatbility with common command line usage
        s->ac = AC_RANGE_CUSTOM_TAB;
    else if (s->ac == AC_RANGE_DEFAULT_TAB_FORCE)
        s->ac = AC_RANGE_DEFAULT_TAB;

    ret = ff_ffv1_encode_setup_plane_info(avctx, avctx->pix_fmt);
    if (ret < 0)
        return ret;

    if (s->bits_per_raw_sample > (s->version > 3 ? 16 : 8) && !s->remap_mode) {
        if (s->ac == AC_GOLOMB_RICE) {
            av_log(avctx, AV_LOG_INFO,
                    "high bits_per_raw_sample, forcing range coder\n");
            s->ac = AC_RANGE_CUSTOM_TAB;
        }
    }


    ret = ff_ffv1_encode_init(avctx);
    if (ret < 0)
        return ret;

    if (s->version > 1) {
        if ((ret = ff_ffv1_encode_determine_slices(avctx)) < 0)
            return ret;

        if ((ret = ff_ffv1_write_extradata(avctx)) < 0)
            return ret;
    }

    if ((ret = ff_ffv1_init_slice_contexts(s)) < 0)
        return ret;
    s->slice_count = s->max_slice_count;

    for (int j = 0; j < s->slice_count; j++) {
        FFV1SliceContext *sc = &s->slices[j];

        for (int i = 0; i < s->plane_count; i++) {
            PlaneContext *const p = &s->slices[j].plane[i];

            p->quant_table_index = s->context_model;
            p->context_count     = s->context_count[p->quant_table_index];
        }
        av_assert0(s->remap_mode >= 0);
        if (s->remap_mode) {
            for (int p = 0; p < 1 + 2*s->chroma_planes + s->transparency ; p++) {
                if (s->bits_per_raw_sample == 32) {
                    sc->unit[p] = av_malloc_array(sc->slice_width, sc->slice_height * sizeof(**sc->unit));
                    if (!sc->unit[p])
                        return AVERROR(ENOMEM);
                    sc->bitmap[p] = av_malloc_array(sc->slice_width * sc->slice_height, sizeof(*sc->bitmap[p]));
                    if (!sc->bitmap[p])
                        return AVERROR(ENOMEM);
                } else {
                    sc->fltmap[p] = av_malloc_array(65536, sizeof(*sc->fltmap[p]));
                    if (!sc->fltmap[p])
                        return AVERROR(ENOMEM);
                }
            }
        }

        ff_build_rac_states(&s->slices[j].c, 0.05 * (1LL << 32), 256 - 8);

        s->slices[j].remap = s->remap_mode;
    }

    if ((ret = ff_ffv1_init_slices_state(s)) < 0)
        return ret;

#define STATS_OUT_SIZE 1024 * 1024 * 6
    if (avctx->flags & AV_CODEC_FLAG_PASS1) {
        avctx->stats_out = av_mallocz(STATS_OUT_SIZE);
        if (!avctx->stats_out)
            return AVERROR(ENOMEM);
        for (int i = 0; i < s->quant_table_count; i++)
            for (int j = 0; j < s->max_slice_count; j++) {
                FFV1SliceContext *sc = &s->slices[j];
                av_assert0(!sc->rc_stat2[i]);
                sc->rc_stat2[i] = av_mallocz(s->context_count[i] *
                                             sizeof(*sc->rc_stat2[i]));
                if (!sc->rc_stat2[i])
                    return AVERROR(ENOMEM);
            }
    }

    return 0;
}

static void encode_slice_header(FFV1Context *f, FFV1SliceContext *sc)
{
    RangeCoder *c = &sc->c;
    uint8_t state[CONTEXT_SIZE];
    int j;
    memset(state, 128, sizeof(state));

    put_symbol(c, state, sc->sx, 0);
    put_symbol(c, state, sc->sy, 0);
    put_symbol(c, state, 0, 0);
    put_symbol(c, state, 0, 0);
    for (j=0; j<f->plane_count; j++) {
        put_symbol(c, state, sc->plane[j].quant_table_index, 0);
        av_assert0(sc->plane[j].quant_table_index == f->context_model);
    }
    if (!(f->cur_enc_frame->flags & AV_FRAME_FLAG_INTERLACED))
        put_symbol(c, state, 3, 0);
    else
        put_symbol(c, state, 1 + !(f->cur_enc_frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST), 0);
    put_symbol(c, state, f->cur_enc_frame->sample_aspect_ratio.num, 0);
    put_symbol(c, state, f->cur_enc_frame->sample_aspect_ratio.den, 0);
    if (f->version > 3) {
        put_rac(c, state, sc->slice_coding_mode == 1);
        if (sc->slice_coding_mode == 1)
            ff_ffv1_clear_slice_state(f, sc);
        put_symbol(c, state, sc->slice_coding_mode, 0);
        if (sc->slice_coding_mode != 1 && f->colorspace == 1) {
            put_symbol(c, state, sc->slice_rct_by_coef, 0);
            put_symbol(c, state, sc->slice_rct_ry_coef, 0);
        }
        put_symbol(c, state, sc->remap, 0);
    }
}

static void choose_rct_params(const FFV1Context *f, FFV1SliceContext *sc,
                              const uint8_t *src[3], const int stride[3], int w, int h)
{
#define NB_Y_COEFF 15
    static const int rct_y_coeff[15][2] = {
        {0, 0}, //      4G
        {1, 1}, //  R + 2G + B
        {2, 2}, // 2R      + 2B
        {0, 2}, //      2G + 2B
        {2, 0}, // 2R + 2G
        {4, 0}, // 4R
        {0, 4}, //           4B

        {0, 3}, //      1G + 3B
        {3, 0}, // 3R + 1G
        {3, 1}, // 3R      +  B
        {1, 3}, //  R      + 3B
        {1, 2}, //  R +  G + 2B
        {2, 1}, // 2R +  G +  B
        {0, 1}, //      3G +  B
        {1, 0}, //  R + 3G
    };

    int stat[NB_Y_COEFF] = {0};
    int x, y, i, p, best;
    int16_t *sample[3];
    int lbd = f->bits_per_raw_sample <= 8;
    int packed = !src[1];
    int transparency = f->transparency;
    int packed_size = (3 + transparency)*2;

    for (y = 0; y < h; y++) {
        int lastr=0, lastg=0, lastb=0;
        for (p = 0; p < 3; p++)
            sample[p] = sc->sample_buffer + p*w;

        for (x = 0; x < w; x++) {
            int b, g, r;
            int ab, ag, ar;
            if (lbd) {
                unsigned v = *((const uint32_t*)(src[0] + x*4 + stride[0]*y));
                b =  v        & 0xFF;
                g = (v >>  8) & 0xFF;
                r = (v >> 16) & 0xFF;
            } else if (packed) {
                const uint16_t *p = ((const uint16_t*)(src[0] + x*packed_size + stride[0]*y));
                r = p[0];
                g = p[1];
                b = p[2];
            } else if (f->use32bit || transparency) {
                g = *((const uint16_t *)(src[0] + x*2 + stride[0]*y));
                b = *((const uint16_t *)(src[1] + x*2 + stride[1]*y));
                r = *((const uint16_t *)(src[2] + x*2 + stride[2]*y));
            } else {
                b = *((const uint16_t*)(src[0] + x*2 + stride[0]*y));
                g = *((const uint16_t*)(src[1] + x*2 + stride[1]*y));
                r = *((const uint16_t*)(src[2] + x*2 + stride[2]*y));
            }

            ar = r - lastr;
            ag = g - lastg;
            ab = b - lastb;
            if (x && y) {
                int bg = ag - sample[0][x];
                int bb = ab - sample[1][x];
                int br = ar - sample[2][x];

                br -= bg;
                bb -= bg;

                for (i = 0; i<NB_Y_COEFF; i++) {
                    stat[i] += FFABS(bg + ((br*rct_y_coeff[i][0] + bb*rct_y_coeff[i][1])>>2));
                }

            }
            sample[0][x] = ag;
            sample[1][x] = ab;
            sample[2][x] = ar;

            lastr = r;
            lastg = g;
            lastb = b;
        }
    }

    best = 0;
    for (i=1; i<NB_Y_COEFF; i++) {
        if (stat[i] < stat[best])
            best = i;
    }

    sc->slice_rct_by_coef = rct_y_coeff[best][1];
    sc->slice_rct_ry_coef = rct_y_coeff[best][0];
}

static void encode_histogram_remap(FFV1Context *f, FFV1SliceContext *sc)
{
    int len = 1 << f->bits_per_raw_sample;
    int flip = sc->remap == 2 ? 0x7FFF : 0;

    for (int p= 0; p < 1 + 2*f->chroma_planes + f->transparency; p++) {
        int j = 0;
        int lu = 0;
        uint8_t state[2][32];
        int run = 0;

        memset(state, 128, sizeof(state));
        put_symbol(&sc->c, state[0], 0, 0);
        memset(state, 128, sizeof(state));
        for (int i= 0; i<len; i++) {
            int ri = i ^ ((i&0x8000) ? 0 : flip);
            int u = sc->fltmap[p][ri];
            sc->fltmap[p][ri] = j;
            j+= u;

            if (lu == u) {
                run ++;
            } else {
                put_symbol_inline(&sc->c, state[lu], run, 0, NULL, NULL);
                if (run == 0)
                    lu = u;
                run = 0;
            }
        }
        if (run)
            put_symbol(&sc->c, state[lu], run, 0);
        sc->remap_count[p] = j;
    }
}

static void load_rgb_float32_frame(FFV1Context *f, FFV1SliceContext *sc,
                                   const uint8_t *src[4],
                                   int w, int h, const int stride[4])
{
    int x, y;
    int transparency = f->transparency;
    int i = 0;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int b, g, r, av_uninit(a);

            g = *((const uint32_t *)(src[0] + x*4 + stride[0]*y));
            b = *((const uint32_t *)(src[1] + x*4 + stride[1]*y));
            r = *((const uint32_t *)(src[2] + x*4 + stride[2]*y));
            if (transparency)
                a = *((const uint32_t *)(src[3] + x*4 + stride[3]*y));

            if (sc->remap == 2) {
#define FLIP(f) (((f)&0x80000000) ? (f) : (f)^0x7FFFFFFF);
                g = FLIP(g);
                b = FLIP(b);
                r = FLIP(r);
            }
            // We cannot build a histogram as we do for 16bit, we need a bit of magic here
            // Its possible to reduce the memory needed at the cost of more dereferencing
            sc->unit[0][i].val = g;
            sc->unit[0][i].ndx = x + y*w;

            sc->unit[1][i].val = b;
            sc->unit[1][i].ndx = x + y*w;

            sc->unit[2][i].val = r;
            sc->unit[2][i].ndx = x + y*w;

            if (transparency) {
                sc->unit[3][i].val = a;
                sc->unit[3][i].ndx = x + y*w;
            }
            i++;
        }
    }

    //TODO switch to radix sort
#define CMP(A,B) ((A)->val - (int64_t)(B)->val)
    AV_QSORT(sc->unit[0], i, struct Unit, CMP);
    AV_QSORT(sc->unit[1], i, struct Unit, CMP);
    AV_QSORT(sc->unit[2], i, struct Unit, CMP);
    if (transparency)
        AV_QSORT(sc->unit[3], i, struct Unit, CMP);
}

static int encode_float32_remap_segment(FFV1SliceContext *sc,
                                        int p, int mul_count, int *mul_tab, int update, int final)
{
    const int pixel_num = sc->slice_width * sc->slice_height;
    uint8_t state[2][3][32];
    int mul[4096+1];
    RangeCoder rc = sc->c;
    int lu = 0;
    int run = 0;
    int64_t last_val = -1;
    int compact_index = -1;
    int i = 0;
    int current_mul_index = -1;
    int run1final = 0;
    int run1start_i;
    int run1start_last_val;
    int run1start_mul_index;

    memcpy(mul, mul_tab, sizeof(*mul_tab)*(mul_count+1));
    memset(state, 128, sizeof(state));
    put_symbol(&rc, state[0][0], mul_count, 0);
    memset(state, 128, sizeof(state));

    for (; i < pixel_num+1; i++) {
        int current_mul = current_mul_index < 0 ? 1 : FFABS(mul[current_mul_index]);
        int64_t val;
        if (i == pixel_num) {
            if (last_val == 0xFFFFFFFF) {
                break;
            } else {
                val = last_val + ((1LL<<32) - last_val + current_mul - 1) / current_mul * current_mul;
                av_assert2(val >= (1LL<<32));
                val += lu * current_mul; //ensure a run1 ends
            }
        } else
            val = sc->unit[p][i].val;

        if (last_val != val) {
            int64_t delta = val - last_val;
            int64_t step  = FFMAX(1, (delta + current_mul/2) / current_mul);
            av_assert2(last_val < val);
            av_assert2(current_mul > 0);

            delta -= step*current_mul;
            av_assert2(delta <= current_mul/2);
            av_assert2(delta > -current_mul);

            av_assert2(step > 0);
            if (lu) {
                if (!run) {
                    run1start_i        = i - 1;
                    run1start_last_val = last_val;
                    run1start_mul_index= current_mul_index;
                }
                if (step == 1) {
                    if (run1final) {
                        if (current_mul>1)
                            put_symbol_inline(&rc, state[lu][1], delta, 1, NULL, NULL);
                    }
                    run ++;
                    av_assert2(last_val + current_mul + delta == val);
                } else {
                    if (run1final) {
                        if (run == 0)
                            lu ^= 1;
                        i--; // we did not encode val so we need to backstep
                        last_val += current_mul;
                    } else {
                        put_symbol_inline(&rc, state[lu][0], run, 0, NULL, NULL);
                        i                 = run1start_i;
                        last_val          = run1start_last_val; // we could compute this instead of storing
                        current_mul_index = run1start_mul_index;
                    }
                    run1final ^= 1;

                    run = 0;
                    continue;
                }
            } else {
                av_assert2(run == 0);
                av_assert2(run1final == 0);
                put_symbol_inline(&rc, state[lu][0], step - 1, 0, NULL, NULL);

                if (current_mul > 1)
                    put_symbol_inline(&rc, state[lu][1], delta, 1, NULL, NULL);
                if (step == 1)
                    lu ^= 1;

                av_assert2(last_val + step * current_mul + delta == val);
            }
            last_val = val;
            current_mul_index = ((last_val + 1) * mul_count) >> 32;
            if (!run || run1final) {
                av_assert2(mul[ current_mul_index ]);
                if (mul[ current_mul_index ] < 0) {
                    av_assert2(i < pixel_num);
                    mul[ current_mul_index ] *= -1;
                    put_symbol_inline(&rc, state[0][2], mul[ current_mul_index ], 0, NULL, NULL);
                }
                if (i < pixel_num)
                    compact_index ++;
            }
        }
        if (!run || run1final)
            if (final && i < pixel_num)
                sc->bitmap[p][sc->unit[p][i].ndx] = compact_index;
    }

    if (update) {
        sc->c = rc;
        sc->remap_count[p] = compact_index + 1;
    }
    return get_rac_count(&rc);
}

static void encode_float32_remap(FFV1Context *f, FFV1SliceContext *sc,
                                 const uint8_t *src[4])
{
    int pixel_num = sc->slice_width * sc->slice_height;
    const int max_log2_mul_count  = ((int[]){  1,  1,  1,  9,  9,  10})[f->remap_optimizer];
    const int log2_mul_count_step = ((int[]){  1,  1,  1,  9,  9,   1})[f->remap_optimizer];
    const int max_log2_mul        = ((int[]){  1,  8,  8,  9, 22,  22})[f->remap_optimizer];
    const int log2_mul_step       = ((int[]){  1,  8,  1,  1,  1,   1})[f->remap_optimizer];
    const int bruteforce_count    = ((int[]){  0,  0,  0,  1,  1,   1})[f->remap_optimizer];
    const int stair_mode          = ((int[]){  0,  0,  0,  1,  0,   0})[f->remap_optimizer];
    const int magic_log2          = ((int[]){  1,  1,  1,  1,  0,   0})[f->remap_optimizer];

    for (int p= 0; p < 1 + 2*f->chroma_planes + f->transparency; p++) {
        int best_log2_mul_count = 0;
        float score_sum[11] = {0};
        int mul_all[11][1025];

        for (int log2_mul_count= 0; log2_mul_count <= max_log2_mul_count; log2_mul_count += log2_mul_count_step) {
            float score_tab_all[1025][23] = {0};
            int64_t last_val = -1;
            int *mul_tab = mul_all[log2_mul_count];
            int last_mul_index = -1;
            int mul_count = 1 << log2_mul_count;

            score_sum[log2_mul_count] = 2 * log2_mul_count;
            if (magic_log2)
                score_sum[log2_mul_count] = av_float2int((float)mul_count * mul_count);
            for (int i= 0; i<pixel_num; i++) {
                int64_t val = sc->unit[p][i].val;
                int mul_index = (val + 1LL)*mul_count >> 32;
                if (val != last_val) {
                    float *score_tab = score_tab_all[(last_val + 1LL)*mul_count >> 32];
                    av_assert2(last_val < val);
                    for(int si= 0; si <= max_log2_mul; si += log2_mul_step) {
                        int64_t delta = val - last_val;
                        int mul;
                        int64_t cost;

                        if (last_val < 0) {
                            mul = 1;
                        } else if (stair_mode && mul_count == 512 && si == max_log2_mul ) {
                            if (mul_index >= 0x378/8 && mul_index <= 23 + 0x378/8) {
                                mul = (0x800080 >> (mul_index - 0x378/8));
                            } else
                                mul = 1;
                        } else {
                            mul = (0x10001LL)<<si >> 16;
                        }

                        cost = FFMAX((delta + mul/2)  / mul, 1);
                        float score = 1;
                        if (mul > 1) {
                            score *= (FFABS(delta - cost*mul)+1);
                            if (mul_count > 1)
                                score *= score;
                        }
                        score *= cost;
                        score *= score;
                        if (mul_index != last_mul_index)
                            score *= mul;
                        if (magic_log2) {
                            score_tab[si] += av_float2int(score);
                        } else
                            score_tab[si] += log2f(score);
                    }
                }
                last_val = val;
                last_mul_index = mul_index;
            }
            for(int i= 0; i<mul_count; i++) {
                int best_index = 0;
                float *score_tab = score_tab_all[i];
                for(int si= 0; si <= max_log2_mul; si += log2_mul_step) {
                    if (score_tab[si] < score_tab[ best_index ])
                        best_index = si;
                }
                if (stair_mode && mul_count == 512 && best_index == max_log2_mul ) {
                    if (i >= 0x378/8 && i <= 23 + 0x378/8) {
                        mul_tab[i] = -(0x800080 >> (i - 0x378/8));
                    } else
                        mul_tab[i] = -1;
                } else
                    mul_tab[i] = -((0x10001LL)<<best_index >> 16);
                score_sum[log2_mul_count] += score_tab[ best_index ];
            }
            mul_tab[mul_count] = 1;

            if (bruteforce_count)
                score_sum[log2_mul_count] = encode_float32_remap_segment(sc, p, mul_count, mul_all[log2_mul_count], 0, 0);

            if (score_sum[log2_mul_count] < score_sum[best_log2_mul_count])
                best_log2_mul_count = log2_mul_count;
        }

        encode_float32_remap_segment(sc, p, 1<<best_log2_mul_count, mul_all[best_log2_mul_count], 1, 1);
    }
}

static int encode_float32_rgb_frame(FFV1Context *f, FFV1SliceContext *sc,
                                    const uint8_t *src[4],
                                    int w, int h, const int stride[4], int ac)
{
    int x, y, p, i;
    const int ring_size = f->context_model ? 3 : 2;
    int32_t *sample[4][3];
    const int pass1 = !!(f->avctx->flags & AV_CODEC_FLAG_PASS1);
    int bits[4], offset;
    int transparency = f->transparency;

    ff_ffv1_compute_bits_per_plane(f, sc, bits, &offset, NULL, f->bits_per_raw_sample);

    sc->run_index = 0;

    memset(RENAME(sc->sample_buffer), 0, ring_size * MAX_PLANES *
           (w + 6) * sizeof(*RENAME(sc->sample_buffer)));

    for (y = 0; y < h; y++) {
        for (i = 0; i < ring_size; i++)
            for (p = 0; p < MAX_PLANES; p++)
                sample[p][i]= RENAME(sc->sample_buffer) + p*ring_size*(w+6) + ((h+i-y)%ring_size)*(w+6) + 3;

        for (x = 0; x < w; x++) {
            int b, g, r, av_uninit(a);
            g = sc->bitmap[0][x + w*y];
            b = sc->bitmap[1][x + w*y];
            r = sc->bitmap[2][x + w*y];
            if (transparency)
                a = sc->bitmap[3][x + w*y];

            if (sc->slice_coding_mode != 1) {
                b -= g;
                r -= g;
                g += (b * sc->slice_rct_by_coef + r * sc->slice_rct_ry_coef) >> 2;
                b += offset;
                r += offset;
            }

            sample[0][0][x] = g;
            sample[1][0][x] = b;
            sample[2][0][x] = r;
            sample[3][0][x] = a;
        }
        for (p = 0; p < 3 + transparency; p++) {
            int ret;
            sample[p][0][-1] = sample[p][1][0  ];
            sample[p][1][ w] = sample[p][1][w-1];
            ret = encode_line32(f, sc, f->avctx, w, sample[p], (p + 1) / 2,
                                bits[p], ac, pass1);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}


static int encode_slice(AVCodecContext *c, void *arg)
{
    FFV1SliceContext *sc = arg;
    FFV1Context *f   = c->priv_data;
    int width        = sc->slice_width;
    int height       = sc->slice_height;
    int x            = sc->slice_x;
    int y            = sc->slice_y;
    const AVFrame *const p = f->cur_enc_frame;
    const int ps     = av_pix_fmt_desc_get(c->pix_fmt)->comp[0].step;
    int ret;
    RangeCoder c_bak = sc->c;
    const int chroma_width  = AV_CEIL_RSHIFT(width,  f->chroma_h_shift);
    const int chroma_height = AV_CEIL_RSHIFT(height, f->chroma_v_shift);
    const uint8_t *planes[4] = {p->data[0] + ps*x + y*p->linesize[0],
                                p->data[1] ? p->data[1] + ps*x + y*p->linesize[1] : NULL,
                                p->data[2] ? p->data[2] + ps*x + y*p->linesize[2] : NULL,
                                p->data[3] ? p->data[3] + ps*x + y*p->linesize[3] : NULL};
    int ac = f->ac;

    sc->slice_coding_mode = 0;
    if (f->version > 3 && f->colorspace == 1) {
        choose_rct_params(f, sc, planes, p->linesize, width, height);
    } else {
        sc->slice_rct_by_coef = 1;
        sc->slice_rct_ry_coef = 1;
    }

retry:
    if (f->key_frame)
        ff_ffv1_clear_slice_state(f, sc);
    if (f->version > 2) {
        encode_slice_header(f, sc);
    }

    if (sc->remap) {
      //Both the 16bit and 32bit remap do exactly the same thing but with 16bits we can
      //Implement this using a "histogram" while for 32bit that would be gb sized, thus a more
      //complex implementation sorting pairs is used.
      if (f->bits_per_raw_sample != 32) {
        if (f->colorspace == 0 && c->pix_fmt != AV_PIX_FMT_YA8 && c->pix_fmt != AV_PIX_FMT_YAF16) {
            const int cx            = x >> f->chroma_h_shift;
            const int cy            = y >> f->chroma_v_shift;

            //TODO decide on the order for the encoded remaps and loads. with golomb rice it
            // easier to have all range coded ones together, otherwise it may be nicer to handle each plane as a whole?

            load_plane(f, sc, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 1);

            if (f->chroma_planes) {
                load_plane(f, sc, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1, 1);
                load_plane(f, sc, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 2, 1);
            }
            if (f->transparency)
                load_plane(f, sc, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], 3, 1);
        } else if (c->pix_fmt == AV_PIX_FMT_YA8 || c->pix_fmt == AV_PIX_FMT_YAF16) {
            load_plane(f, sc, p->data[0] +           ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 2);
            load_plane(f, sc, p->data[0] + (ps>>1) + ps*x + y*p->linesize[0], width, height, p->linesize[0], 1, 2);
        } else if (f->use32bit) {
            load_rgb_frame32(f, sc, planes, width, height, p->linesize);
        } else
            load_rgb_frame  (f, sc, planes, width, height, p->linesize);

        encode_histogram_remap(f, sc);
      } else {
            load_rgb_float32_frame(f, sc, planes, width, height, p->linesize);
            encode_float32_remap(f, sc, planes);
      }
    }

    if (ac == AC_GOLOMB_RICE) {
        sc->ac_byte_count = f->version > 2 || (!x && !y) ? ff_rac_terminate(&sc->c, f->version > 2) : 0;
        init_put_bits(&sc->pb,
                      sc->c.bytestream_start + sc->ac_byte_count,
                      sc->c.bytestream_end - sc->c.bytestream_start - sc->ac_byte_count);
    }

    if (f->colorspace == 0 && c->pix_fmt != AV_PIX_FMT_YA8 && c->pix_fmt != AV_PIX_FMT_YAF16) {
        const int cx            = x >> f->chroma_h_shift;
        const int cy            = y >> f->chroma_v_shift;

        ret = encode_plane(f, sc, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 0, 1, ac);

        if (f->chroma_planes) {
            ret |= encode_plane(f, sc, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1, 1, 1, ac);
            ret |= encode_plane(f, sc, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1, 2, 1, ac);
        }
        if (f->transparency)
            ret |= encode_plane(f, sc, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], 2, 3, 1, ac);
    } else if (c->pix_fmt == AV_PIX_FMT_YA8 || c->pix_fmt == AV_PIX_FMT_YAF16) {
        ret  = encode_plane(f, sc, p->data[0] +           ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 0, 2, ac);
        ret |= encode_plane(f, sc, p->data[0] + (ps>>1) + ps*x + y*p->linesize[0], width, height, p->linesize[0], 1, 1, 2, ac);
    } else if (f->bits_per_raw_sample == 32) {
        ret = encode_float32_rgb_frame(f, sc, planes, width, height, p->linesize, ac);
    } else if (f->use32bit) {
        ret = encode_rgb_frame32(f, sc, planes, width, height, p->linesize, ac);
    } else {
        ret = encode_rgb_frame(f, sc, planes, width, height, p->linesize, ac);
    }

    if (ac != AC_GOLOMB_RICE) {
        sc->ac_byte_count = ff_rac_terminate(&sc->c, 1);
    } else {
        flush_put_bits(&sc->pb); // FIXME: nicer padding
        sc->ac_byte_count += put_bytes_output(&sc->pb);
    }

    if (ret < 0) {
        av_assert0(sc->slice_coding_mode == 0);
        if (f->version < 4) {
            av_log(c, AV_LOG_ERROR, "Buffer too small\n");
            return ret;
        }
        av_log(c, AV_LOG_DEBUG, "Coding slice as PCM\n");
        ac = 1;
        sc->slice_coding_mode = 1;
        sc->c = c_bak;
        goto retry;
    }

    return 0;
}

size_t ff_ffv1_encode_buffer_size(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;

    size_t maxsize = avctx->width*avctx->height * (1 + f->transparency);
    if (f->chroma_planes)
        maxsize += AV_CEIL_RSHIFT(avctx->width, f->chroma_h_shift) * AV_CEIL_RSHIFT(f->height, f->chroma_v_shift) * 2;
    maxsize += f->slice_count * 800; //for slice header
    if (f->version > 3) {
        maxsize *= f->bits_per_raw_sample + 1;
        if (f->remap_mode)
            maxsize += f->slice_count * 70000 * (1 + 2*f->chroma_planes + f->transparency);
    } else {
        maxsize += f->slice_count * 2 * (avctx->width + avctx->height); //for bug with slices that code some pixels more than once
        maxsize *= 8*(2*f->bits_per_raw_sample + 5);
    }
    maxsize >>= 3;
    maxsize += FF_INPUT_BUFFER_MIN_SIZE;

    return maxsize;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    FFV1Context *f      = avctx->priv_data;
    RangeCoder *const c = &f->slices[0].c;
    uint8_t keystate    = 128;
    uint8_t *buf_p;
    int i, ret;
    int64_t maxsize;

    if(!pict) {
        if (avctx->flags & AV_CODEC_FLAG_PASS1) {
            int j, k, m;
            char *p   = avctx->stats_out;
            char *end = p + STATS_OUT_SIZE;

            memset(f->rc_stat, 0, sizeof(f->rc_stat));
            for (i = 0; i < f->quant_table_count; i++)
                memset(f->rc_stat2[i], 0, f->context_count[i] * sizeof(*f->rc_stat2[i]));

            av_assert0(f->slice_count == f->max_slice_count);
            for (j = 0; j < f->slice_count; j++) {
                const FFV1SliceContext *sc = &f->slices[j];
                for (i = 0; i < 256; i++) {
                    f->rc_stat[i][0] += sc->rc_stat[i][0];
                    f->rc_stat[i][1] += sc->rc_stat[i][1];
                }
                for (i = 0; i < f->quant_table_count; i++) {
                    for (k = 0; k < f->context_count[i]; k++)
                        for (m = 0; m < 32; m++) {
                            f->rc_stat2[i][k][m][0] += sc->rc_stat2[i][k][m][0];
                            f->rc_stat2[i][k][m][1] += sc->rc_stat2[i][k][m][1];
                        }
                }
            }

            for (j = 0; j < 256; j++) {
                snprintf(p, end - p, "%" PRIu64 " %" PRIu64 " ",
                        f->rc_stat[j][0], f->rc_stat[j][1]);
                p += strlen(p);
            }
            snprintf(p, end - p, "\n");

            for (i = 0; i < f->quant_table_count; i++) {
                for (j = 0; j < f->context_count[i]; j++)
                    for (m = 0; m < 32; m++) {
                        snprintf(p, end - p, "%" PRIu64 " %" PRIu64 " ",
                                f->rc_stat2[i][j][m][0], f->rc_stat2[i][j][m][1]);
                        p += strlen(p);
                    }
            }
            snprintf(p, end - p, "%d\n", f->gob_count);
        }
        return 0;
    }

    /* Maximum packet size */
    maxsize = ff_ffv1_encode_buffer_size(avctx);

    if (maxsize > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE - 32) {
        FFV1Context *f = avctx->priv_data;
        if (!f->maxsize_warned) {
            av_log(avctx, AV_LOG_WARNING, "Cannot allocate worst case packet size, the encoding could fail\n");
            f->maxsize_warned++;
        }
        maxsize = INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE - 32;
    }

    if ((ret = ff_alloc_packet(avctx, pkt, maxsize)) < 0)
        return ret;

    ff_init_range_encoder(c, pkt->data, pkt->size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    f->cur_enc_frame = pict;

    if (avctx->gop_size == 0 || f->picture_number % avctx->gop_size == 0) {
        put_rac(c, &keystate, 1);
        f->key_frame = 1;
        f->gob_count++;
        write_header(f);
    } else {
        put_rac(c, &keystate, 0);
        f->key_frame = 0;
    }

    if (f->ac == AC_RANGE_CUSTOM_TAB) {
        int i;
        for (i = 1; i < 256; i++) {
            c->one_state[i]        = f->state_transition[i];
            c->zero_state[256 - i] = 256 - c->one_state[i];
        }
    }

    for (i = 0; i < f->slice_count; i++) {
        FFV1SliceContext *sc = &f->slices[i];
        uint8_t *start  = pkt->data + pkt->size * (int64_t)i / f->slice_count;
        int len         = pkt->size / f->slice_count;
        if (i) {
            ff_init_range_encoder(&sc->c, start, len);
        } else {
            av_assert0(sc->c.bytestream_end >= sc->c.bytestream_start + len);
            av_assert0(sc->c.bytestream < sc->c.bytestream_start + len);
            sc->c.bytestream_end = sc->c.bytestream_start + len;
        }
    }
    avctx->execute(avctx, encode_slice, f->slices, NULL,
                   f->slice_count, sizeof(*f->slices));

    buf_p = pkt->data;
    for (i = 0; i < f->slice_count; i++) {
        FFV1SliceContext *sc = &f->slices[i];
        int bytes = sc->ac_byte_count;
        if (i > 0 || f->version > 2) {
            av_assert0(bytes < pkt->size / f->slice_count);
            memmove(buf_p, sc->c.bytestream_start, bytes);
            av_assert0(bytes < (1 << 24));
            AV_WB24(buf_p + bytes, bytes);
            bytes += 3;
        }
        if (f->ec) {
            unsigned v;
            buf_p[bytes++] = 0;
            v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref, buf_p, bytes) ^ (f->crcref ? 0x8CD88196 : 0);
            AV_WL32(buf_p + bytes, v);
            bytes += 4;
        }
        buf_p += bytes;
    }

    if (avctx->flags & AV_CODEC_FLAG_PASS1)
        avctx->stats_out[0] = '\0';

    f->picture_number++;
    pkt->size   = buf_p - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY * f->key_frame;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    FFV1Context *const s = avctx->priv_data;

    for (int j = 0; j < s->max_slice_count; j++) {
        FFV1SliceContext *sc = &s->slices[j];

        for(int p = 0; p<4; p++) {
            av_freep(&sc->unit[p]);
            av_freep(&sc->bitmap[p]);
        }
    }

    av_freep(&avctx->stats_out);
    ff_ffv1_close(s);

    return 0;
}

#define OFFSET(x) offsetof(FFV1Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "slicecrc", "Protect slices with CRCs", OFFSET(ec), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 2, VE },
    { "coder", "Coder type", OFFSET(ac), AV_OPT_TYPE_INT,
            { .i64 = 0 }, -2, 2, VE, .unit = "coder" },
        { "rice", "Golomb rice", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_GOLOMB_RICE }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "range_def", "Range with default table", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_RANGE_DEFAULT_TAB_FORCE }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "range_tab", "Range with custom table", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_RANGE_CUSTOM_TAB }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "ac", "Range with custom table (the ac option exists for compatibility and is deprecated)", 0, AV_OPT_TYPE_CONST,
            { .i64 = 1 }, INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "context", "Context model", OFFSET(context_model), AV_OPT_TYPE_INT,
            { .i64 = 0 }, 0, 1, VE },
    { "qtable", "Quantization table", OFFSET(qtable), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 2, VE , .unit = "qtable"},
        { "default", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_DEFAULT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },
        { "8bit", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_8BIT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },
        { "greater8bit", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_GT8BIT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },
    { "remap_mode", "Remap Mode", OFFSET(remap_mode), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 2, VE, .unit = "remap_mode" },
        { "auto", "Automatic", 0, AV_OPT_TYPE_CONST,
            { .i64 = -1 }, INT_MIN, INT_MAX, VE, .unit = "remap_mode" },
        { "off", "Disabled", 0, AV_OPT_TYPE_CONST,
            { .i64 =  0 }, INT_MIN, INT_MAX, VE, .unit = "remap_mode" },
        { "dualrle", "Dual RLE", 0, AV_OPT_TYPE_CONST,
            { .i64 =  1 }, INT_MIN, INT_MAX, VE, .unit = "remap_mode" },
        { "flipdualrle", "Dual RLE", 0, AV_OPT_TYPE_CONST,
            { .i64 =  2 }, INT_MIN, INT_MAX, VE, .unit = "remap_mode" },
    { "remap_optimizer", "Remap Optimizer", OFFSET(remap_optimizer), AV_OPT_TYPE_INT, { .i64 = 3 }, 0, 5, VE, .unit = "remap_optimizer" },

    { NULL }
};

static const AVClass ffv1_class = {
    .class_name = "ffv1 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_ffv1_encoder = {
    .p.name         = "ffv1",
    CODEC_LONG_NAME("FFmpeg video codec #1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FFV1,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(FFV1Context),
    .init           = encode_init_internal,
    FF_CODEC_ENCODE_CB(encode_frame),
    .close          = encode_close,
    CODEC_PIXFMTS(
        AV_PIX_FMT_YUV420P,   AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,  AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVA444P,  AV_PIX_FMT_YUV440P,   AV_PIX_FMT_YUV422P,   AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,   AV_PIX_FMT_0RGB32,    AV_PIX_FMT_RGB32,     AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUV444P9,  AV_PIX_FMT_YUV422P9,
        AV_PIX_FMT_YUV420P9,  AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA422P12,
        AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
        AV_PIX_FMT_GRAY16,    AV_PIX_FMT_GRAY8,     AV_PIX_FMT_GBRP9,     AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12,    AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRAP14,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_YA8,
        AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_RGB48,
        AV_PIX_FMT_GBRAP16, AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_GRAY9,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YAF16,
        AV_PIX_FMT_GRAYF16,
        AV_PIX_FMT_GBRPF16, AV_PIX_FMT_GBRPF32),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &ffv1_class,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_EOF_FLUSH,
};
