/*
 * G.722 ADPCM audio encoder/decoder
 *
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
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
 * G.722 ADPCM audio codec
 *
 * This G.722 decoder is a bit-exact implementation of the ITU G.722
 * specification for all three specified bitrates - 64000bps, 56000bps
 * and 48000bps. It passes the ITU tests.
 *
 * @note For the 56000bps and 48000bps bitrates, the lowest 1 or 2 bits
 *       respectively of each byte are ignored.
 */

#include "mathops.h"
#include "g722.h"

static const int8_t sign_lookup[2] = { -1, 1 };

static const int16_t inv_log2_table[32] = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383,
    2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371,
    3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008
};
static const int16_t high_log_factor_step[2] = { 798, -214 };
const int16_t ff_g722_high_inv_quant[4] = { -926, -202, 926, 202 };
/**
 * low_log_factor_step[index] == wl[rl42[index]]
 */
static const int16_t low_log_factor_step[16] = {
     -60, 3042, 1198, 538, 334, 172,  58, -30,
    3042, 1198,  538, 334, 172,  58, -30, -60
};
const int16_t ff_g722_low_inv_quant4[16] = {
       0, -2557, -1612, -1121,  -786,  -530,  -323,  -150,
    2557,  1612,  1121,   786,   530,   323,   150,     0
};
const int16_t ff_g722_low_inv_quant6[64] = {
     -17,   -17,   -17,   -17, -3101, -2738, -2376, -2088,
   -1873, -1689, -1535, -1399, -1279, -1170, -1072,  -982,
    -899,  -822,  -750,  -682,  -618,  -558,  -501,  -447,
    -396,  -347,  -300,  -254,  -211,  -170,  -130,   -91,
    3101,  2738,  2376,  2088,  1873,  1689,  1535,  1399,
    1279,  1170,  1072,   982,   899,   822,   750,   682,
     618,   558,   501,   447,   396,   347,   300,   254,
     211,   170,   130,    91,    54,    17,   -54,   -17
};

static inline void s_zero(int cur_diff, struct G722Band *band)
{
    int s_zero = 0;

    #define ACCUM(k, x, d) do { \
            int tmp = x; \
            band->zero_mem[k] = ((band->zero_mem[k] * 255) >> 8) + \
               d*((band->diff_mem[k]^cur_diff) < 0 ? -128 : 128); \
            band->diff_mem[k] = tmp; \
            s_zero += (tmp * band->zero_mem[k]) >> 15; \
        } while (0)
    if (cur_diff) {
        ACCUM(5, band->diff_mem[4], 1);
        ACCUM(4, band->diff_mem[3], 1);
        ACCUM(3, band->diff_mem[2], 1);
        ACCUM(2, band->diff_mem[1], 1);
        ACCUM(1, band->diff_mem[0], 1);
        ACCUM(0, cur_diff << 1, 1);
    } else {
        ACCUM(5, band->diff_mem[4], 0);
        ACCUM(4, band->diff_mem[3], 0);
        ACCUM(3, band->diff_mem[2], 0);
        ACCUM(2, band->diff_mem[1], 0);
        ACCUM(1, band->diff_mem[0], 0);
        ACCUM(0, cur_diff << 1, 0);
    }
    #undef ACCUM
    band->s_zero = s_zero;
}

/**
 * adaptive predictor
 *
 * @param cur_diff the dequantized and scaled delta calculated from the
 *                 current codeword
 */
static void do_adaptive_prediction(struct G722Band *band, const int cur_diff)
{
    int sg[2], limit, cur_qtzd_reconst;

    const int cur_part_reconst = band->s_zero + cur_diff < 0;

    sg[0] = sign_lookup[cur_part_reconst != band->part_reconst_mem[0]];
    sg[1] = sign_lookup[cur_part_reconst == band->part_reconst_mem[1]];
    band->part_reconst_mem[1] = band->part_reconst_mem[0];
    band->part_reconst_mem[0] = cur_part_reconst;

    band->pole_mem[1] = av_clip((sg[0] * av_clip(band->pole_mem[0], -8191, 8191) >> 5) +
                                (sg[1] << 7) + (band->pole_mem[1] * 127 >> 7), -12288, 12288);

    limit = 15360 - band->pole_mem[1];
    band->pole_mem[0] = av_clip(-192 * sg[0] + (band->pole_mem[0] * 255 >> 8), -limit, limit);

    s_zero(cur_diff, band);

    cur_qtzd_reconst = av_clip_int16((band->s_predictor + cur_diff) << 1);
    band->s_predictor = av_clip_int16(band->s_zero +
                                      (band->pole_mem[0] * cur_qtzd_reconst >> 15) +
                                      (band->pole_mem[1] * band->prev_qtzd_reconst >> 15));
    band->prev_qtzd_reconst = cur_qtzd_reconst;
}

static inline int linear_scale_factor(const int log_factor)
{
    const int wd1 = inv_log2_table[(log_factor >> 6) & 31];
    const int shift = log_factor >> 11;
    return shift < 0 ? wd1 >> -shift : wd1 << shift;
}

void ff_g722_update_low_predictor(struct G722Band *band, const int ilow)
{
    do_adaptive_prediction(band,
                           band->scale_factor * ff_g722_low_inv_quant4[ilow] >> 10);

    // quantizer adaptation
    band->log_factor   = av_clip((band->log_factor * 127 >> 7) +
                                 low_log_factor_step[ilow], 0, 18432);
    band->scale_factor = linear_scale_factor(band->log_factor - (8 << 11));
}

void ff_g722_update_high_predictor(struct G722Band *band, const int dhigh,
                                  const int ihigh)
{
    do_adaptive_prediction(band, dhigh);

    // quantizer adaptation
    band->log_factor   = av_clip((band->log_factor * 127 >> 7) +
                                 high_log_factor_step[ihigh&1], 0, 22528);
    band->scale_factor = linear_scale_factor(band->log_factor - (10 << 11));
}
