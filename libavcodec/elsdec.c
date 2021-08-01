/*
 * ELS (Entropy Logarithmic-Scale) decoder
 *
 * Copyright (c) 2013 Maxim Poliakovski
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
 * Entropy Logarithmic-Scale binary arithmetic decoder
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

#include "elsdec.h"

/* ELS coder constants and structures. */
#define ELS_JOTS_PER_BYTE   36
#define ELS_MAX             (1 << 24)
#define RUNG_SPACE          (64 * sizeof(ElsRungNode))

/* ELS coder tables. */
static const struct Ladder {
    int8_t  AMps;
    int8_t  ALps;
    uint8_t next0;
    uint8_t next1;
} Ladder[174] = {
    { -6,   -5,   2,   1 },
    { -2,  -12,   3,   6 },
    { -2,  -12,   4,   6 },
    { -1,  -16,   7,   5 },
    { -1,  -16,   8,  10 },
    { -5,   -6,  11,   9 },
    { -6,   -5,  10,   5 },
    { -1,  -18,  13,  11 },
    { -1,  -18,  12,  14 },
    { -6,   -5,  15,  18 },
    { -5,   -6,  14,   9 },
    { -3,   -8,  17,  15 },
    { -1,  -20,  20,  16 },
    { -1,  -20,  23,  17 },
    { -3,   -8,  16,  18 },
    { -5,   -6,  19,  26 },
    { -3,   -9,  22,  24 },
    { -3,   -9,  21,  19 },
    { -5,   -6,  24,  26 },
    { -4,   -7,  27,  25 },
    { -1,  -22,  34,  28 },
    { -2,  -11,  29,  27 },
    { -2,  -11,  28,  30 },
    { -1,  -22,  39,  29 },
    { -4,   -7,  30,  32 },
    { -6,   -5,  33,  31 },
    { -6,   -5,  32,  25 },
    { -3,   -8,  35,  33 },
    { -2,  -12,  36,  38 },
    { -2,  -12,  37,  35 },
    { -3,   -8,  38,  40 },
    { -6,   -5,  41,  48 },
    { -6,   -5,  40,  31 },
    { -5,   -6,  43,  41 },
    { -1,  -24,  94,  42 },
    { -3,   -8,  45,  43 },
    { -2,  -12,  42,  44 },
    { -2,  -12,  47,  45 },
    { -3,   -8,  44,  46 },
    { -1,  -24, 125,  47 },
    { -5,   -6,  46,  48 },
    { -6,   -5,  49,  49 },
    { -2,  -13, 152, 164 },
    { -4,   -7,  51,  49 },
    { -3,   -9, 164, 168 },
    { -3,   -9,  55,  51 },
    { -4,   -7, 168, 170 },
    { -2,  -13,  67,  55 },
    { -6,   -5, 170,  49 },
    { -6,   -5,  51, 170 },
    { -1,  -72,  50,  74 },
    { -4,   -7,  53,  49 },
    { -1,  -61,  50,  74 },
    { -3,   -8,  55,  49 },
    { -1,  -51,  52,  76 },
    { -3,   -9,  57,  51 },
    { -1,  -46,  54,  76 },
    { -2,  -10,  59,  53 },
    { -1,  -43,  56,  78 },
    { -2,  -11,  61,  53 },
    { -1,  -41,  58,  80 },
    { -2,  -12,  63,  55 },
    { -1,  -39,  60,  82 },
    { -2,  -12,  65,  55 },
    { -1,  -37,  62,  84 },
    { -2,  -13,  67,  57 },
    { -1,  -36,  64,  86 },
    { -1,  -14,  69,  59 },
    { -1,  -35,  66,  88 },
    { -1,  -14,  71,  59 },
    { -1,  -34,  68,  90 },
    { -1,  -15,  73,  61 },
    { -1,  -33,  70,  92 },
    { -1,  -15,  75,  61 },
    { -1,  -32,  72,  94 },
    { -1,  -15,  77,  63 },
    { -1,  -31,  74,  96 },
    { -1,  -16,  79,  65 },
    { -1,  -31,  76,  98 },
    { -1,  -16,  81,  67 },
    { -1,  -30,  78, 100 },
    { -1,  -17,  83,  67 },
    { -1,  -29,  80, 102 },
    { -1,  -17,  85,  69 },
    { -1,  -29,  82, 104 },
    { -1,  -18,  87,  71 },
    { -1,  -28,  84, 104 },
    { -1,  -18,  89,  73 },
    { -1,  -28,  86, 108 },
    { -1,  -18,  91,  73 },
    { -1,  -27,  88, 108 },
    { -1,  -19,  93,  75 },
    { -1,  -27,  90, 112 },
    { -1,  -19,  95,  77 },
    { -1,  -26,  92, 112 },
    { -1,  -20,  97,  79 },
    { -1,  -26,  94, 114 },
    { -1,  -20,  99,  81 },
    { -1,  -25,  96, 116 },
    { -1,  -20, 101,  83 },
    { -1,  -25,  98, 118 },
    { -1,  -21, 103,  83 },
    { -1,  -24, 100, 120 },
    { -1,  -21, 105,  85 },
    { -1,  -24, 102, 122 },
    { -1,  -22, 107,  87 },
    { -1,  -23, 104, 124 },
    { -1,  -22, 109,  89 },
    { -1,  -23, 106, 126 },
    { -1,  -22, 111,  91 },
    { -1,  -22, 108, 128 },
    { -1,  -23, 113,  93 },
    { -1,  -22, 110, 130 },
    { -1,  -23, 115,  95 },
    { -1,  -22, 112, 132 },
    { -1,  -24, 117,  97 },
    { -1,  -21, 114, 134 },
    { -1,  -24, 119,  99 },
    { -1,  -21, 116, 136 },
    { -1,  -25, 121, 101 },
    { -1,  -20, 118, 136 },
    { -1,  -25, 123, 103 },
    { -1,  -20, 120, 138 },
    { -1,  -26, 125, 105 },
    { -1,  -20, 122, 140 },
    { -1,  -26, 127, 107 },
    { -1,  -19, 124, 142 },
    { -1,  -27, 129, 107 },
    { -1,  -19, 126, 144 },
    { -1,  -27, 131, 111 },
    { -1,  -18, 128, 146 },
    { -1,  -28, 133, 111 },
    { -1,  -18, 130, 146 },
    { -1,  -28, 135, 115 },
    { -1,  -18, 132, 148 },
    { -1,  -29, 137, 115 },
    { -1,  -17, 134, 150 },
    { -1,  -29, 139, 117 },
    { -1,  -17, 136, 152 },
    { -1,  -30, 141, 119 },
    { -1,  -16, 138, 152 },
    { -1,  -31, 143, 121 },
    { -1,  -16, 140, 154 },
    { -1,  -31, 145, 123 },
    { -1,  -15, 142, 156 },
    { -1,  -32, 147, 125 },
    { -1,  -15, 144, 158 },
    { -1,  -33, 149, 127 },
    { -1,  -15, 146, 158 },
    { -1,  -34, 151, 129 },
    { -1,  -14, 148, 160 },
    { -1,  -35, 153, 131 },
    { -1,  -14, 150, 160 },
    { -1,  -36, 155, 133 },
    { -2,  -13, 152, 162 },
    { -1,  -37, 157, 135 },
    { -2,  -12, 154, 164 },
    { -1,  -39, 159, 137 },
    { -2,  -12, 156, 164 },
    { -1,  -41, 161, 139 },
    { -2,  -11, 158, 166 },
    { -1,  -43, 163, 141 },
    { -2,  -10, 160, 166 },
    { -1,  -46, 165, 143 },
    { -3,   -9, 162, 168 },
    { -1,  -51, 167, 143 },
    { -3,   -8, 164, 170 },
    { -1,  -61, 169, 145 },
    { -4,   -7, 166, 170 },
    { -1,  -72, 169, 145 },
    { -6,   -5, 168,  49 },
    {  0, -108, 171, 171 },
    {  0, -108, 172, 172 },
    { -6,   -5, 173, 173 },
};

static const uint32_t els_exp_tab[ELS_JOTS_PER_BYTE * 4 + 1] = {
           0,        0,       0,       0,       0,       0,         0,        0,
           0,        0,       0,       0,       0,       0,         0,        0,
           0,        0,       0,       0,       0,       0,         0,        0,
           0,        0,       0,       0,       0,       0,         0,        0,
           0,        0,       0,       0,       1,       1,         1,        1,
           1,        2,       2,       2,       3,       4,         4,        5,
           6,        7,       8,      10,      11,      13,        16,       18,
          21,       25,      29,      34,      40,      47,        54,       64,
          74,       87,     101,     118,     138,      161,      188,      219,
         256,      298,     348,     406,     474,      552,      645,      752,
         877,     1024,    1194,    1393,    1625,     1896,     2211,     2580,
        3010,     3511,    4096,    4778,    5573,     6501,     7584,     8847,
       10321,    12040,   14045,   16384,   19112,    22295,    26007,    30339,
       35391,    41285,   48160,   56180,   65536,    76288,    89088,   103936,
      121344,   141312,  165120,  192512,  224512,   262144,   305664,   356608,
      416000,   485376,  566016,  660480,  770560,   898816,  1048576,  1223168,
     1426688,  1664256, 1941504, 2264832, 2642176,  3082240,  3595520,  4194304,
     4892672,  5707520, 6657792, 7766784, 9060096, 10568960, 12328960, 14382080,
    16777216,
};

void ff_els_decoder_init(ElsDecCtx *ctx, const uint8_t *in, size_t data_size)
{
    int nbytes;

    /* consume up to 3 bytes from the input data */
    if (data_size >= 3) {
        ctx->x = AV_RB24(in);
        nbytes = 3;
    } else if (data_size == 2) {
        ctx->x = AV_RB16(in);
        nbytes = 2;
    } else {
        ctx->x = *in;
        nbytes = 1;
    }

    ctx->in_buf    = in + nbytes;
    ctx->data_size = data_size - nbytes;
    ctx->err       = 0;
    ctx->j         = ELS_JOTS_PER_BYTE;
    ctx->t         = ELS_MAX;
    ctx->diff      = FFMIN(ELS_MAX - ctx->x,
                           ELS_MAX - els_exp_tab[ELS_JOTS_PER_BYTE * 4 - 1]);
}

void ff_els_decoder_uninit(ElsUnsignedRung *rung)
{
    av_freep(&rung->rem_rung_list);
}

static int els_import_byte(ElsDecCtx *ctx)
{
    if (!ctx->data_size) {
        ctx->err = AVERROR_EOF;
        return AVERROR_EOF;
    }
    ctx->x   = (ctx->x << 8) | *ctx->in_buf++;
    ctx->data_size--;
    ctx->j  += ELS_JOTS_PER_BYTE;
    ctx->t <<= 8;

    return 0;
}

int ff_els_decode_bit(ElsDecCtx *ctx, uint8_t *rung)
{
    int z, bit, ret;
    const uint32_t *pAllowable = &els_exp_tab[ELS_JOTS_PER_BYTE * 3];

    if (ctx->err)
        return 0;

    z          = pAllowable[ctx->j + Ladder[*rung].ALps];
    ctx->t    -= z;
    ctx->diff -= z;
    if (ctx->diff > 0)
        return *rung & 1;   /* shortcut for x < t > pAllowable[j - 1] */

    if (ctx->t > ctx->x) {  /* decode most probable symbol (MPS) */
        ctx->j += Ladder[*rung].AMps;
        while (ctx->t > pAllowable[ctx->j])
            ctx->j++;

        if (ctx->j <= 0) { /* MPS: import one byte from bytestream. */
            ret = els_import_byte(ctx);
            if (ret < 0)
                return ret;
        }

        z     = ctx->t;
        bit   = *rung & 1;
        *rung = Ladder[*rung].next0;
    } else { /* decode less probable symbol (LPS) */
        ctx->x -= ctx->t;
        ctx->t  = z;

        ctx->j += Ladder[*rung].ALps;
        if (ctx->j <= 0) {
            /* LPS: import one byte from bytestream. */
            z <<= 8;
            ret = els_import_byte(ctx);
            if (ret < 0)
                return ret;
            if (ctx->j <= 0) {
                /* LPS: import second byte from bytestream. */
                z <<= 8;
                ret = els_import_byte(ctx);
                if (ret < 0)
                    return ret;
                while (pAllowable[ctx->j - 1] >= z)
                    ctx->j--;
            }
        }

        bit   = !(*rung & 1);
        *rung = Ladder[*rung].next1;
    }

    ctx->diff = FFMIN(z - ctx->x, z - pAllowable[ctx->j - 1]);

    return bit;
}

unsigned ff_els_decode_unsigned(ElsDecCtx *ctx, ElsUnsignedRung *ur)
{
    int i, n, r, bit;
    ElsRungNode *rung_node;

    if (ctx->err)
        return 0;

    /* decode unary prefix */
    for (n = 0; n < ELS_EXPGOLOMB_LEN + 1; n++)
        if (ff_els_decode_bit(ctx, &ur->prefix_rung[n]))
            break;

    /* handle the error/overflow case */
    if (ctx->err || n >= ELS_EXPGOLOMB_LEN) {
        ctx->err = AVERROR_INVALIDDATA;
        return 0;
    }

    /* handle the zero case */
    if (!n)
        return 0;

    /* initialize probability tree */
    if (!ur->rem_rung_list) {
        ur->rem_rung_list = av_realloc(NULL, RUNG_SPACE);
        if (!ur->rem_rung_list) {
            ctx->err = AVERROR(ENOMEM);
            return 0;
        }
        memset(ur->rem_rung_list, 0, RUNG_SPACE);
        ur->rung_list_size = RUNG_SPACE;
        ur->avail_index    = ELS_EXPGOLOMB_LEN;
    }

    /* decode the remainder */
    for (i = 0, r = 0, bit = 0; i < n; i++) {
        if (!i)
            rung_node = &ur->rem_rung_list[n];
        else {
            if (!rung_node->next_index) {
                if (ur->rung_list_size <= (ur->avail_index + 2) * sizeof(ElsRungNode)) {
                    // remember rung_node position
                    ptrdiff_t pos     = rung_node - ur->rem_rung_list;
                    ctx->err = av_reallocp(&ur->rem_rung_list,
                                                   ur->rung_list_size +
                                                   RUNG_SPACE);
                    if (ctx->err < 0) {
                        return 0;
                    }
                    memset((uint8_t *) ur->rem_rung_list + ur->rung_list_size, 0,
                           RUNG_SPACE);
                    ur->rung_list_size += RUNG_SPACE;
                    // restore rung_node position in the new list
                    rung_node = &ur->rem_rung_list[pos];
                }
                rung_node->next_index = ur->avail_index;
                ur->avail_index      += 2;
            }
            rung_node = &ur->rem_rung_list[rung_node->next_index + bit];
        }

        bit = ff_els_decode_bit(ctx, &rung_node->rung);
        if (ctx->err)
            return bit;

        r = (r << 1) + bit;
    }

    return (1 << n) - 1 + r; /* make value from exp golomb code */
}
