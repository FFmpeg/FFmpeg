/*
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
 * @brief IntraX8 (J-Frame) subdecoder, used by WMV2 and VC-1
 */

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "msmpeg4_vc1_data.h"
#include "intrax8huf.h"
#include "intrax8.h"
#include "intrax8dsp.h"
#include "mpegutils.h"

#define VLC_BUFFER_SIZE 28150

#define MAX_TABLE_DEPTH(table_bits, max_bits) \
    ((max_bits + table_bits - 1) / table_bits)

#define DC_VLC_BITS 9
#define AC_VLC_BITS 9
#define OR_VLC_BITS 7

#define DC_VLC_MTD MAX_TABLE_DEPTH(DC_VLC_BITS, MAX_DC_VLC_BITS)
#define AC_VLC_MTD MAX_TABLE_DEPTH(AC_VLC_BITS, MAX_AC_VLC_BITS)
#define OR_VLC_MTD MAX_TABLE_DEPTH(OR_VLC_BITS, MAX_OR_VLC_BITS)

static const VLCElem *j_ac_vlc[2][2][8];  // [quant < 13], [intra / inter], [select]
static const VLCElem *j_dc_vlc[2][8];     // [quant], [select]
static const VLCElem *j_orient_vlc[2][4]; // [quant], [select]

static av_cold const VLCElem *x8_init_vlc(VLCInitState *state, int nb_bits,
                                          int nb_codes, const uint8_t table[][2])
{
    return ff_vlc_init_tables_from_lengths(state, nb_bits, nb_codes, &table[0][1], 2,
                                           &table[0][0], 2, 1, 0, 0);
}

static av_cold void x8_vlc_init(void)
{
    static VLCElem vlc_buf[VLC_BUFFER_SIZE];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);
    int i;

// set ac tables
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 8; k++)
                j_ac_vlc[i][j][k] = x8_init_vlc(&state, AC_VLC_BITS, 77,
                                                x8_ac_quant_table[i][j][k]);

// set dc tables
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
            j_dc_vlc[i][j] = x8_init_vlc(&state, DC_VLC_BITS, 34,
                                         x8_dc_quant_table[i][j]);

// set orient tables
    for (i = 0; i < 2; i++)
        j_orient_vlc[0][i] = x8_init_vlc(&state, OR_VLC_BITS, 12,
                                         x8_orient_highquant_table[i]);
    for (i = 0; i < 4; i++)
        j_orient_vlc[1][i] = x8_init_vlc(&state, OR_VLC_BITS, 12,
                                         x8_orient_lowquant_table[i]);
}

static void x8_reset_vlc_tables(IntraX8Context *w)
{
    memset(w->j_dc_vlc_table, 0, sizeof(w->j_dc_vlc_table));
    memset(w->j_ac_vlc_table, 0, sizeof(w->j_ac_vlc_table));
    w->j_orient_vlc_table = NULL;
}

static inline void x8_select_ac_table(IntraX8Context *const w, int mode)
{
    int table_index;

    av_assert2(mode < 4);

    if (w->j_ac_vlc_table[mode])
        return;

    table_index       = get_bits(w->gb, 3);
    // 2 modes use same tables
    w->j_ac_vlc_table[mode] = j_ac_vlc[w->quant < 13][mode >> 1][table_index];
    av_assert2(w->j_ac_vlc_table[mode]);
}

static inline int x8_get_orient_vlc(IntraX8Context *w)
{
    if (!w->j_orient_vlc_table) {
        int table_index = get_bits(w->gb, 1 + (w->quant < 13));
        w->j_orient_vlc_table = j_orient_vlc[w->quant < 13][table_index];
    }

    return get_vlc2(w->gb, w->j_orient_vlc_table, OR_VLC_BITS, OR_VLC_MTD);
}

#define extra_bits(eb)  (eb)        // 3 bits
#define extra_run       (0xFF << 8) // 1 bit
#define extra_level     (0x00 << 8) // 1 bit
#define run_offset(r)   ((r) << 16) // 6 bits
#define level_offset(l) ((l) << 24) // 5 bits
static const uint32_t ac_decode_table[] = {
    /* 46 */ extra_bits(3) | extra_run   | run_offset(16) | level_offset(0),
    /* 47 */ extra_bits(3) | extra_run   | run_offset(24) | level_offset(0),
    /* 48 */ extra_bits(2) | extra_run   | run_offset(4)  | level_offset(1),
    /* 49 */ extra_bits(3) | extra_run   | run_offset(8)  | level_offset(1),

    /* 50 */ extra_bits(5) | extra_run   | run_offset(32) | level_offset(0),
    /* 51 */ extra_bits(4) | extra_run   | run_offset(16) | level_offset(1),

    /* 52 */ extra_bits(2) | extra_level | run_offset(0)  | level_offset(4),
    /* 53 */ extra_bits(2) | extra_level | run_offset(0)  | level_offset(8),
    /* 54 */ extra_bits(2) | extra_level | run_offset(0)  | level_offset(12),
    /* 55 */ extra_bits(3) | extra_level | run_offset(0)  | level_offset(16),
    /* 56 */ extra_bits(3) | extra_level | run_offset(0)  | level_offset(24),

    /* 57 */ extra_bits(2) | extra_level | run_offset(1)  | level_offset(3),
    /* 58 */ extra_bits(3) | extra_level | run_offset(1)  | level_offset(7),

    /* 59 */ extra_bits(2) | extra_run   | run_offset(16) | level_offset(0),
    /* 60 */ extra_bits(2) | extra_run   | run_offset(20) | level_offset(0),
    /* 61 */ extra_bits(2) | extra_run   | run_offset(24) | level_offset(0),
    /* 62 */ extra_bits(2) | extra_run   | run_offset(28) | level_offset(0),
    /* 63 */ extra_bits(4) | extra_run   | run_offset(32) | level_offset(0),
    /* 64 */ extra_bits(4) | extra_run   | run_offset(48) | level_offset(0),

    /* 65 */ extra_bits(2) | extra_run   | run_offset(4)  | level_offset(1),
    /* 66 */ extra_bits(3) | extra_run   | run_offset(8)  | level_offset(1),
    /* 67 */ extra_bits(4) | extra_run   | run_offset(16) | level_offset(1),

    /* 68 */ extra_bits(2) | extra_level | run_offset(0)  | level_offset(4),
    /* 69 */ extra_bits(3) | extra_level | run_offset(0)  | level_offset(8),
    /* 70 */ extra_bits(4) | extra_level | run_offset(0)  | level_offset(16),

    /* 71 */ extra_bits(2) | extra_level | run_offset(1)  | level_offset(3),
    /* 72 */ extra_bits(3) | extra_level | run_offset(1)  | level_offset(7),
};
#undef extra_bits
#undef extra_run
#undef extra_level
#undef run_offset
#undef level_offset

static void x8_get_ac_rlf(IntraX8Context *const w, const int mode,
                          int *const run, int *const level, int *const final)
{
    int i, e;

//    x8_select_ac_table(w, mode);
    i = get_vlc2(w->gb, w->j_ac_vlc_table[mode], AC_VLC_BITS, AC_VLC_MTD);

    if (i < 46) { // [0-45]
        int t, l;
        if (i < 0) {
            *level =
            *final =      // prevent 'may be used uninitialized'
            *run   = 64;  // this would cause error exit in the ac loop
            return;
        }

        /*
         * i == 0-15  r = 0-15 l = 0; r = i & %01111
         * i == 16-19 r = 0-3  l = 1; r = i & %00011
         * i == 20-21 r = 0-1  l = 2; r = i & %00001
         * i == 22    r = 0    l = 3; r = i & %00000
         */

        *final =
        t      = i > 22;
        i     -= 23 * t;

        /* l = lut_l[i / 2] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3 }[i >> 1];
         *     11 10'01 01'00 00'00 00'00 00'00 00 => 0xE50000 */
        l = (0xE50000 >> (i & 0x1E)) & 3; // 0x1E or ~1 or (i >> 1 << 1)

        /* t = lut_mask[l] = { 0x0f, 0x03, 0x01, 0x00 }[l];
         *     as i < 256 the higher bits do not matter */
        t = 0x01030F >> (l << 3);

        *run   = i & t;
        *level = l;
    } else if (i < 73) { // [46-72]
        uint32_t sm;
        uint32_t mask;

        i -= 46;
        sm = ac_decode_table[i];

        e    = get_bits(w->gb, sm & 0xF);
        sm >>= 8;                               // 3 bits
        mask = sm & 0xff;
        sm >>= 8;                               // 1 bit

        *run   = (sm &  0xff) + (e &  mask);    // 6 bits
        *level = (sm >>    8) + (e & ~mask);    // 5 bits
        *final = i > (58 - 46);
    } else if (i < 75) { // [73-74]
        static const uint8_t crazy_mix_runlevel[32] = {
            0x22, 0x32, 0x33, 0x53, 0x23, 0x42, 0x43, 0x63,
            0x24, 0x52, 0x34, 0x73, 0x25, 0x62, 0x44, 0x83,
            0x26, 0x72, 0x35, 0x54, 0x27, 0x82, 0x45, 0x64,
            0x28, 0x92, 0x36, 0x74, 0x29, 0xa2, 0x46, 0x84,
        };

        *final = !(i & 1);
        e      = get_bits(w->gb, 5); // get the extra bits
        *run   = crazy_mix_runlevel[e] >> 4;
        *level = crazy_mix_runlevel[e] & 0x0F;
    } else {
        *level = get_bits(w->gb, 7 - 3 * (i & 1));
        *run   = get_bits(w->gb, 6);
        *final = get_bits1(w->gb);
    }
    return;
}

/* static const uint8_t dc_extra_sbits[] = {
 *     0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
 * }; */
static const uint8_t dc_index_offset[] = {
    0, 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
};

static int x8_get_dc_rlf(IntraX8Context *const w, const int mode,
                         int *const level, int *const final)
{
    int i, e, c;

    av_assert2(mode < 3);
    if (!w->j_dc_vlc_table[mode]) {
        int table_index = get_bits(w->gb, 3);
        // 4 modes, same table
        w->j_dc_vlc_table[mode] = j_dc_vlc[w->quant < 13][table_index];
    }

    i = get_vlc2(w->gb, w->j_dc_vlc_table[mode], DC_VLC_BITS, DC_VLC_MTD);

    /* (i >= 17) { i -= 17; final =1; } */
    c      = i > 16;
    *final = c;
    i      -= 17 * c;

    if (i <= 0) {
        *level = 0;
        return -i;
    }
    c  = (i + 1) >> 1; // hackish way to calculate dc_extra_sbits[]
    c -= c > 1;

    e = get_bits(w->gb, c); // get the extra bits
    i = dc_index_offset[i] + (e >> 1);

    e      = -(e & 1);     // 0, 0xffffff
    *level =  (i ^ e) - e; // (i ^ 0) - 0, (i ^ 0xff) - (-1)
    return 0;
}

// end of huffman

static int x8_setup_spatial_predictor(IntraX8Context *const w, const int chroma)
{
    int range;
    int sum;
    int quant;

    w->dsp.setup_spatial_compensation(w->dest[chroma], w->scratchpad,
                                      w->frame->linesize[chroma > 0],
                                      &range, &sum, w->edges);
    if (chroma) {
        w->orient = w->chroma_orient;
        quant     = w->quant_dc_chroma;
    } else {
        quant = w->quant;
    }

    w->flat_dc = 0;
    if (range < quant || range < 3) {
        w->orient = 0;

        // yep you read right, a +-1 idct error may break decoding!
        if (range < 3) {
            w->flat_dc      = 1;
            sum            += 9;
            // ((1 << 17) + 9) / (8 + 8 + 1 + 2) = 6899
            w->predicted_dc = sum * 6899 >> 17;
        }
    }
    if (chroma)
        return 0;

    av_assert2(w->orient < 3);
    if (range < 2 * w->quant) {
        if ((w->edges & 3) == 0) {
            if (w->orient == 1)
                w->orient = 11;
            if (w->orient == 2)
                w->orient = 10;
        } else {
            w->orient = 0;
        }
        w->raw_orient = 0;
    } else {
        static const uint8_t prediction_table[3][12] = {
            { 0, 8, 4, 10, 11, 2, 6, 9, 1, 3, 5, 7 },
            { 4, 0, 8, 11, 10, 3, 5, 2, 6, 9, 1, 7 },
            { 8, 0, 4, 10, 11, 1, 7, 2, 6, 9, 3, 5 },
        };
        w->raw_orient = x8_get_orient_vlc(w);
        if (w->raw_orient < 0)
            return -1;
        av_assert2(w->raw_orient < 12);
        av_assert2(w->orient < 3);
        w->orient=prediction_table[w->orient][w->raw_orient];
    }
    return 0;
}

static void x8_update_predictions(IntraX8Context *const w, const int orient,
                                  const int est_run)
{
    w->prediction_table[w->mb_x * 2 + (w->mb_y & 1)] = (est_run << 2) + 1 * (orient == 4) + 2 * (orient == 8);
/*
 * y = 2n + 0 -> // 0 2 4
 * y = 2n + 1 -> // 1 3 5
 */
}

static void x8_get_prediction_chroma(IntraX8Context *const w)
{
    w->edges  = 1 * !(w->mb_x >> 1);
    w->edges |= 2 * !(w->mb_y >> 1);
    w->edges |= 4 * (w->mb_x >= (2 * w->mb_width - 1)); // mb_x for chroma would always be odd

    w->raw_orient = 0;
    // lut_co[8] = {inv,4,8,8, inv,4,8,8} <- => {1,1,0,0;1,1,0,0} => 0xCC
    if (w->edges & 3) {
        w->chroma_orient = 4 << ((0xCC >> w->edges) & 1);
        return;
    }
    // block[x - 1][y | 1 - 1)]
    w->chroma_orient = (w->prediction_table[2 * w->mb_x - 2] & 0x03) << 2;
}

static void x8_get_prediction(IntraX8Context *const w)
{
    int a, b, c, i;

    w->edges  = 1 * !w->mb_x;
    w->edges |= 2 * !w->mb_y;
    w->edges |= 4 * (w->mb_x >= (2 * w->mb_width - 1));

    switch (w->edges & 3) {
    case 0:
        break;
    case 1:
        // take the one from the above block[0][y - 1]
        w->est_run = w->prediction_table[!(w->mb_y & 1)] >> 2;
        w->orient  = 1;
        return;
    case 2:
        // take the one from the previous block[x - 1][0]
        w->est_run = w->prediction_table[2 * w->mb_x - 2] >> 2;
        w->orient  = 2;
        return;
    case 3:
        w->est_run = 16;
        w->orient  = 0;
        return;
    }
    // no edge cases
    b = w->prediction_table[2 * w->mb_x     + !(w->mb_y & 1)]; // block[x    ][y - 1]
    a = w->prediction_table[2 * w->mb_x - 2 +  (w->mb_y & 1)]; // block[x - 1][y    ]
    c = w->prediction_table[2 * w->mb_x - 2 + !(w->mb_y & 1)]; // block[x - 1][y - 1]

    w->est_run = FFMIN(b, a);
    /* This condition has nothing to do with w->edges, even if it looks
     * similar it would trigger if e.g. x = 3; y = 2;
     * I guess somebody wrote something wrong and it became standard. */
    if ((w->mb_x & w->mb_y) != 0)
        w->est_run = FFMIN(c, w->est_run);
    w->est_run >>= 2;

    a &= 3;
    b &= 3;
    c &= 3;

    i = (0xFFEAF4C4 >> (2 * b + 8 * a)) & 3;
    if (i != 3)
        w->orient = i;
    else
        w->orient = (0xFFEAD8 >> (2 * c + 8 * (w->quant > 12))) & 3;
/*
 * lut1[b][a] = {
 * ->{ 0, 1, 0, pad },
 *   { 0, 1, X, pad },
 *   { 2, 2, 2, pad }
 * }
 * pad 2  2  2;
 * pad X  1  0;
 * pad 0  1  0 <-
 * -> 11 10 '10 10 '11 11'01 00 '11 00'01 00 => 0xEAF4C4
 *
 * lut2[q>12][c] = {
 * ->{ 0, 2, 1, pad},
 *   { 2, 2, 2, pad}
 * }
 * pad 2  2  2;
 * pad 1  2  0 <-
 * -> 11 10'10 10 '11 01'10 00 => 0xEAD8
 */
}

static void x8_ac_compensation(IntraX8Context *const w, const int direction,
                               const int dc_level)
{
    int t;
#define B(x,y)  w->block[0][w->idct_permutation[(x) + (y) * 8]]
#define T(x)  ((x) * dc_level + 0x8000) >> 16;
    switch (direction) {
    case 0:
        t        = T(3811); // h
        B(1, 0) -= t;
        B(0, 1) -= t;

        t        = T(487); // e
        B(2, 0) -= t;
        B(0, 2) -= t;

        t        = T(506); // f
        B(3, 0) -= t;
        B(0, 3) -= t;

        t        = T(135); // c
        B(4, 0) -= t;
        B(0, 4) -= t;
        B(2, 1) += t;
        B(1, 2) += t;
        B(3, 1) += t;
        B(1, 3) += t;

        t        = T(173); // d
        B(5, 0) -= t;
        B(0, 5) -= t;

        t        = T(61); // b
        B(6, 0) -= t;
        B(0, 6) -= t;
        B(5, 1) += t;
        B(1, 5) += t;

        t        = T(42); // a
        B(7, 0) -= t;
        B(0, 7) -= t;
        B(4, 1) += t;
        B(1, 4) += t;
        B(4, 4) += t;

        t        = T(1084); // g
        B(1, 1) += t;

        w->block_last_index[0] = FFMAX(w->block_last_index[0], 7 * 8);
        break;
    case 1:
        B(0, 1) -= T(6269);
        B(0, 3) -= T(708);
        B(0, 5) -= T(172);
        B(0, 7) -= T(73);

        w->block_last_index[0] = FFMAX(w->block_last_index[0], 7 * 8);
        break;
    case 2:
        B(1, 0) -= T(6269);
        B(3, 0) -= T(708);
        B(5, 0) -= T(172);
        B(7, 0) -= T(73);

        w->block_last_index[0] = FFMAX(w->block_last_index[0], 7);
        break;
    }
#undef B
#undef T
}

static void dsp_x8_put_solidcolor(const uint8_t pix, uint8_t *dst,
                                  const ptrdiff_t linesize)
{
    int k;
    for (k = 0; k < 8; k++) {
        memset(dst, pix, 8);
        dst += linesize;
    }
}

static const int16_t quant_table[64] = {
    256, 256, 256, 256, 256, 256, 259, 262,
    265, 269, 272, 275, 278, 282, 285, 288,
    292, 295, 299, 303, 306, 310, 314, 317,
    321, 325, 329, 333, 337, 341, 345, 349,
    353, 358, 362, 366, 371, 375, 379, 384,
    389, 393, 398, 403, 408, 413, 417, 422,
    428, 433, 438, 443, 448, 454, 459, 465,
    470, 476, 482, 488, 493, 499, 505, 511,
};

static int x8_decode_intra_mb(IntraX8Context *const w, const int chroma)
{
    uint8_t *scantable;
    int final, run, level;
    int ac_mode, dc_mode, est_run, dc_level;
    int pos, n;
    int zeros_only;
    int use_quant_matrix;
    int sign;

    av_assert2(w->orient < 12);
    w->bdsp.clear_block(w->block[0]);

    if (chroma)
        dc_mode = 2;
    else
        dc_mode = !!w->est_run; // 0, 1

    if (x8_get_dc_rlf(w, dc_mode, &dc_level, &final))
        return -1;
    n          = 0;
    zeros_only = 0;
    if (!final) { // decode ac
        use_quant_matrix = w->use_quant_matrix;
        if (chroma) {
            ac_mode = 1;
            est_run = 64; // not used
        } else {
            if (w->raw_orient < 3)
                use_quant_matrix = 0;

            if (w->raw_orient > 4) {
                ac_mode = 0;
                est_run = 64;
            } else {
                if (w->est_run > 1) {
                    ac_mode = 2;
                    est_run = w->est_run;
                } else {
                    ac_mode = 3;
                    est_run = 64;
                }
            }
        }
        x8_select_ac_table(w, ac_mode);
        /* scantable_selector[12] = { 0, 2, 0, 1, 1, 1, 0, 2, 2, 0, 1, 2 }; <-
         * -> 10'01' 00'10' 10'00' 01'01' 01'00' 10'00 => 0x928548 */
        scantable = w->permutated_scantable[(0x928548 >> (2 * w->orient)) & 3];
        pos       = 0;
        do {
            n++;
            if (n >= est_run) {
                ac_mode = 3;
                x8_select_ac_table(w, 3);
            }

            x8_get_ac_rlf(w, ac_mode, &run, &level, &final);

            pos += run + 1;
            if (pos > 63) {
                // this also handles vlc error in x8_get_ac_rlf
                return -1;
            }
            level  = (level + 1) * w->dquant;
            level += w->qsum;

            sign  = -get_bits1(w->gb);
            level = (level ^ sign) - sign;

            if (use_quant_matrix)
                level = (level * quant_table[pos]) >> 8;

            w->block[0][scantable[pos]] = level;
        } while (!final);

        w->block_last_index[0] = pos;
    } else { // DC only
        w->block_last_index[0] = 0;
        if (w->flat_dc && ((unsigned) (dc_level + 1)) < 3) { // [-1; 1]
            int32_t divide_quant = !chroma ? w->divide_quant_dc_luma
                                           : w->divide_quant_dc_chroma;
            int32_t dc_quant     = !chroma ? w->quant
                                           : w->quant_dc_chroma;

            // original intent dc_level += predicted_dc/quant;
            // but it got lost somewhere in the rounding
            dc_level += (w->predicted_dc * divide_quant + (1 << 12)) >> 13;

            dsp_x8_put_solidcolor(av_clip_uint8((dc_level * dc_quant + 4) >> 3),
                                  w->dest[chroma],
                                  w->frame->linesize[!!chroma]);

            goto block_placed;
        }
        zeros_only = dc_level == 0;
    }
    if (!chroma)
        w->block[0][0] = dc_level * w->quant;
    else
        w->block[0][0] = dc_level * w->quant_dc_chroma;

    // there is !zero_only check in the original, but dc_level check is enough
    if ((unsigned int) (dc_level + 1) >= 3 && (w->edges & 3) != 3) {
        int direction;
        /* ac_comp_direction[orient] = { 0, 3, 3, 1, 1, 0, 0, 0, 2, 2, 2, 1 }; <-
         * -> 01'10' 10'10' 00'00' 00'01' 01'11' 11'00 => 0x6A017C */
        direction = (0x6A017C >> (w->orient * 2)) & 3;
        if (direction != 3) {
            // modify block_last[]
            x8_ac_compensation(w, direction, w->block[0][0]);
        }
    }

    if (w->flat_dc) {
        dsp_x8_put_solidcolor(w->predicted_dc, w->dest[chroma],
                              w->frame->linesize[!!chroma]);
    } else {
        w->dsp.spatial_compensation[w->orient](w->scratchpad,
                                               w->dest[chroma],
                                               w->frame->linesize[!!chroma]);
    }
    if (!zeros_only)
        w->wdsp.idct_add(w->dest[chroma],
                         w->frame->linesize[!!chroma],
                         w->block[0]);

block_placed:
    if (!chroma)
        x8_update_predictions(w, w->orient, n);

    if (w->loopfilter) {
        uint8_t *ptr = w->dest[chroma];
        ptrdiff_t linesize = w->frame->linesize[!!chroma];

        if (!((w->edges & 2) || (zeros_only && (w->orient | 4) == 4)))
            w->dsp.h_loop_filter(ptr, linesize, w->quant);

        if (!((w->edges & 1) || (zeros_only && (w->orient | 8) == 8)))
            w->dsp.v_loop_filter(ptr, linesize, w->quant);
    }
    return 0;
}

// FIXME maybe merge with ff_*
static void x8_init_block_index(IntraX8Context *w, AVFrame *frame)
{
    // not parent codec linesize as this would be wrong for field pics
    // not that IntraX8 has interlacing support ;)
    const ptrdiff_t linesize   = frame->linesize[0];
    const ptrdiff_t uvlinesize = frame->linesize[1];

    w->dest[0] = frame->data[0];
    w->dest[1] = frame->data[1];
    w->dest[2] = frame->data[2];

    w->dest[0] +=  w->mb_y       * linesize   << 3;
    // chroma blocks are on add rows
    w->dest[1] += (w->mb_y & ~1) * uvlinesize << 2;
    w->dest[2] += (w->mb_y & ~1) * uvlinesize << 2;
}

av_cold int ff_intrax8_common_init(AVCodecContext *avctx,
                                   IntraX8Context *w,
                                   int16_t (*block)[64],
                                   int block_last_index[12],
                                   int mb_width, int mb_height)
{
    static AVOnce init_static_once = AV_ONCE_INIT;

    w->avctx = avctx;
    w->mb_width  = mb_width;
    w->mb_height = mb_height;
    w->block = block;
    w->block_last_index = block_last_index;

    // two rows, 2 blocks per cannon mb
    w->prediction_table = av_mallocz(w->mb_width * 2 * 2);
    if (!w->prediction_table)
        return AVERROR(ENOMEM);

    ff_wmv2dsp_init(&w->wdsp);

    ff_init_scantable_permutation(w->idct_permutation,
                                  w->wdsp.idct_perm);

    ff_permute_scantable(w->permutated_scantable[0], ff_wmv1_scantable[0],
                         w->idct_permutation);
    ff_permute_scantable(w->permutated_scantable[1], ff_wmv1_scantable[2],
                         w->idct_permutation);
    ff_permute_scantable(w->permutated_scantable[2], ff_wmv1_scantable[3],
                         w->idct_permutation);

    ff_intrax8dsp_init(&w->dsp);
    ff_blockdsp_init(&w->bdsp);

    ff_thread_once(&init_static_once, x8_vlc_init);

    return 0;
}

av_cold void ff_intrax8_common_end(IntraX8Context *w)
{
    av_freep(&w->prediction_table);
}

int ff_intrax8_decode_picture(IntraX8Context *w, MPVPicture *pict,
                              GetBitContext *gb, int *mb_x, int *mb_y,
                              int dquant, int quant_offset,
                              int loopfilter, int lowdelay)
{
    int mb_xy;

    w->gb     = gb;
    w->dquant = dquant;
    w->quant  = dquant >> 1;
    w->qsum   = quant_offset;
    w->frame  = pict->f;
    w->loopfilter = loopfilter;
    w->use_quant_matrix = get_bits1(w->gb);

    w->mb_x = *mb_x;
    w->mb_y = *mb_y;

    w->divide_quant_dc_luma = ((1 << 16) + (w->quant >> 1)) / w->quant;
    if (w->quant < 5) {
        w->quant_dc_chroma        = w->quant;
        w->divide_quant_dc_chroma = w->divide_quant_dc_luma;
    } else {
        w->quant_dc_chroma        = w->quant + ((w->quant + 3) >> 3);
        w->divide_quant_dc_chroma = ((1 << 16) + (w->quant_dc_chroma >> 1)) / w->quant_dc_chroma;
    }
    x8_reset_vlc_tables(w);

    for (w->mb_y = 0; w->mb_y < w->mb_height * 2; w->mb_y++) {
        x8_init_block_index(w, w->frame);
        mb_xy = (w->mb_y >> 1) * (w->mb_width + 1);
        if (get_bits_left(gb) < 1)
            goto error;
        for (w->mb_x = 0; w->mb_x < w->mb_width * 2; w->mb_x++) {
            x8_get_prediction(w);
            if (x8_setup_spatial_predictor(w, 0))
                goto error;
            if (x8_decode_intra_mb(w, 0))
                goto error;

            if (w->mb_x & w->mb_y & 1) {
                x8_get_prediction_chroma(w);

                /* when setting up chroma, no vlc is read,
                 * so no error condition can be reached */
                x8_setup_spatial_predictor(w, 1);
                if (x8_decode_intra_mb(w, 1))
                    goto error;

                x8_setup_spatial_predictor(w, 2);
                if (x8_decode_intra_mb(w, 2))
                    goto error;

                w->dest[1] += 8;
                w->dest[2] += 8;

                pict->qscale_table[mb_xy] = w->quant;
                mb_xy++;
            }
            w->dest[0] += 8;
        }
        if (w->mb_y & 1)
            ff_draw_horiz_band(w->avctx, w->frame, w->frame,
                               (w->mb_y - 1) * 8, 16,
                               PICT_FRAME, 0, lowdelay);
    }

error:
    *mb_x = w->mb_x;
    *mb_y = w->mb_y;

    return 0;
}
