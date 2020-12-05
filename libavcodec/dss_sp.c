/*
 * Digital Speech Standard - Standard Play mode (DSS SP) audio decoder.
 * Copyright (C) 2014 Oleksij Rempel <linux@rempel-privat.de>
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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

#define SUBFRAMES 4
#define PULSE_MAX 8

#define DSS_SP_FRAME_SIZE        42
#define DSS_SP_SAMPLE_COUNT     (66 * SUBFRAMES)
#define DSS_SP_FORMULA(a, b, c) ((int)((((a) * (1 << 15)) + (b) * (unsigned)(c)) + 0x4000) >> 15)

typedef struct DssSpSubframe {
    int16_t gain;
    int32_t combined_pulse_pos;
    int16_t pulse_pos[7];
    int16_t pulse_val[7];
} DssSpSubframe;

typedef struct DssSpFrame {
    int16_t filter_idx[14];
    int16_t sf_adaptive_gain[SUBFRAMES];
    int16_t pitch_lag[SUBFRAMES];
    struct DssSpSubframe sf[SUBFRAMES];
} DssSpFrame;

typedef struct DssSpContext {
    AVCodecContext *avctx;
    int32_t excitation[288 + 6];
    int32_t history[187];
    DssSpFrame fparam;
    int32_t working_buffer[SUBFRAMES][72];
    int32_t audio_buf[15];
    int32_t err_buf1[15];
    int32_t lpc_filter[14];
    int32_t filter[15];
    int32_t vector_buf[72];
    int noise_state;
    int32_t err_buf2[15];

    int pulse_dec_mode;

    DECLARE_ALIGNED(16, uint8_t, bits)[DSS_SP_FRAME_SIZE +
                                       AV_INPUT_BUFFER_PADDING_SIZE];
} DssSpContext;

/*
 * Used for the coding/decoding of the pulse positions for the MP-MLQ codebook.
 */
static const uint32_t dss_sp_combinatorial_table[PULSE_MAX][72] = {
    {       0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0,
            0,         0,         0,          0,          0,          0 },
    {       0,         1,         2,          3,          4,          5,
            6,         7,         8,          9,         10,         11,
           12,        13,        14,         15,         16,         17,
           18,        19,        20,         21,         22,         23,
           24,        25,        26,         27,         28,         29,
           30,        31,        32,         33,         34,         35,
           36,        37,        38,         39,         40,         41,
           42,        43,        44,         45,         46,         47,
           48,        49,        50,         51,         52,         53,
           54,        55,        56,         57,         58,         59,
           60,        61,        62,         63,         64,         65,
           66,        67,        68,         69,         70,         71 },
    {       0,         0,         1,          3,          6,         10,
           15,        21,        28,         36,         45,         55,
           66,        78,        91,        105,        120,        136,
          153,       171,       190,        210,        231,        253,
          276,       300,       325,        351,        378,        406,
          435,       465,       496,        528,        561,        595,
          630,       666,       703,        741,        780,        820,
          861,       903,       946,        990,       1035,       1081,
         1128,      1176,      1225,       1275,       1326,       1378,
         1431,      1485,      1540,       1596,       1653,       1711,
         1770,      1830,      1891,       1953,       2016,       2080,
         2145,      2211,      2278,       2346,       2415,       2485 },
    {       0,         0,         0,          1,          4,         10,
           20,        35,        56,         84,        120,        165,
          220,       286,       364,        455,        560,        680,
          816,       969,      1140,       1330,       1540,       1771,
         2024,      2300,      2600,       2925,       3276,       3654,
         4060,      4495,      4960,       5456,       5984,       6545,
         7140,      7770,      8436,       9139,       9880,      10660,
        11480,     12341,     13244,      14190,      15180,      16215,
        17296,     18424,     19600,      20825,      22100,      23426,
        24804,     26235,     27720,      29260,      30856,      32509,
        34220,     35990,     37820,      39711,      41664,      43680,
        45760,     47905,     50116,      52394,      54740,      57155 },
    {       0,         0,         0,          0,          1,          5,
           15,        35,        70,        126,        210,        330,
          495,       715,      1001,       1365,       1820,       2380,
         3060,      3876,      4845,       5985,       7315,       8855,
        10626,     12650,     14950,      17550,      20475,      23751,
        27405,     31465,     35960,      40920,      46376,      52360,
        58905,     66045,     73815,      82251,      91390,     101270,
       111930,    123410,    135751,     148995,     163185,     178365,
       194580,    211876,    230300,     249900,     270725,     292825,
       316251,    341055,    367290,     395010,     424270,     455126,
       487635,    521855,    557845,     595665,     635376,     677040,
       720720,    766480,    814385,     864501,     916895,     971635 },
    {       0,         0,         0,          0,          0,          1,
            6,        21,        56,        126,        252,        462,
          792,      1287,      2002,       3003,       4368,       6188,
         8568,     11628,     15504,      20349,      26334,      33649,
        42504,     53130,     65780,      80730,      98280,     118755,
       142506,    169911,    201376,     237336,     278256,     324632,
       376992,    435897,    501942,     575757,     658008,     749398,
       850668,    962598,   1086008,    1221759,    1370754,    1533939,
      1712304,   1906884,   2118760,    2349060,    2598960,    2869685,
      3162510,   3478761,   3819816,    4187106,    4582116,    5006386,
      5461512,   5949147,   6471002,    7028847,    7624512,    8259888,
      8936928,   9657648,  10424128,   11238513,   12103014,   13019909 },
    {       0,         0,         0,          0,          0,          0,
            1,         7,        28,         84,        210,        462,
          924,      1716,      3003,       5005,       8008,      12376,
        18564,     27132,     38760,      54264,      74613,     100947,
       134596,    177100,    230230,     296010,     376740,     475020,
       593775,    736281,    906192,    1107568,    1344904,    1623160,
      1947792,   2324784,   2760681,    3262623,    3838380,    4496388,
      5245786,   6096454,   7059052,    8145060,    9366819,   10737573,
     12271512,  13983816,  15890700,   18009460,   20358520,   22957480,
     25827165,  28989675,  32468436,   36288252,   40475358,   45057474,
     50063860,  55525372,  61474519,   67945521,   74974368,   82598880,
     90858768,  99795696, 109453344,  119877472,  131115985,  143218999 },
    {       0,         0,         0,          0,          0,          0,
            0,         1,         8,         36,        120,        330,
          792,      1716,      3432,       6435,      11440,      19448,
        31824,     50388,     77520,     116280,     170544,     245157,
       346104,    480700,    657800,     888030,    1184040,    1560780,
      2035800,   2629575,   3365856,    4272048,    5379616,    6724520,
      8347680,  10295472,  12620256,   15380937,   18643560,   22481940,
     26978328,  32224114,  38320568,   45379620,   53524680,   62891499,
     73629072,  85900584,  99884400,  115775100,  133784560,  154143080,
    177100560, 202927725, 231917400,  264385836,  300674088,  341149446,
    386206920, 436270780, 491796152,  553270671,  621216192,  696190560,
    778789440, 869648208, 969443904, 1078897248, 1198774720, 1329890705 },
};

static const int16_t dss_sp_filter_cb[14][32] = {
    { -32653, -32587, -32515, -32438, -32341, -32216, -32062, -31881,
      -31665, -31398, -31080, -30724, -30299, -29813, -29248, -28572,
      -27674, -26439, -24666, -22466, -19433, -16133, -12218,  -7783,
       -2834,   1819,   6544,  11260,  16050,  20220,  24774,  28120 },

    { -27503, -24509, -20644, -17496, -14187, -11277,  -8420,  -5595,
       -3013,   -624,   1711,   3880,   5844,   7774,   9739,  11592,
       13364,  14903,  16426,  17900,  19250,  20586,  21803,  23006,
       24142,  25249,  26275,  27300,  28359,  29249,  30118,  31183 },

    { -27827, -24208, -20943, -17781, -14843, -11848,  -9066,  -6297,
       -3660,   -910,   1918,   5025,   8223,  11649,  15086,  18423,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -17128, -11975,  -8270,  -5123,  -2296,    183,   2503,   4707,
        6798,   8945,  11045,  13239,  15528,  18248,  21115,  24785,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -21557, -17280, -14286, -11644,  -9268,  -7087,  -4939,  -2831,
        -691,   1407,   3536,   5721,   8125,  10677,  13721,  17731,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -15030, -10377,  -7034,  -4327,  -1900,    364,   2458,   4450,
        6422,   8374,  10374,  12486,  14714,  16997,  19626,  22954,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -16155, -12362,  -9698,  -7460,  -5258,  -3359,  -1547,    219,
        1916,   3599,   5299,   6994,   8963,  11226,  13716,  16982,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -14742,  -9848,  -6921,  -4648,  -2769,  -1065,    499,   2083,
        3633,   5219,   6857,   8580,  10410,  12672,  15561,  20101,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -11099,  -7014,  -3855,  -1025,   1680,   4544,   7807,  11932,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    {  -9060,  -4570,  -1381,   1419,   4034,   6728,   9865,  14149,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -12450,  -7985,  -4596,  -1734,    961,   3629,   6865,  11142,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -11831,  -7404,  -4010,  -1096,   1606,   4291,   7386,  11482,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -13404,  -9250,  -5995,  -3312,   -890,   1594,   4464,   8198,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },

    { -11239,  -7220,  -4040,  -1406,    971,   3321,   6006,   9697,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0,
           0,      0,      0,      0,      0,      0,      0,      0 },
};

static const uint16_t  dss_sp_fixed_cb_gain[64] = {
       0,    4,    8,   13,   17,   22,   26,   31,
      35,   40,   44,   48,   53,   58,   63,   69,
      76,   83,   91,   99,  109,  119,  130,  142,
     155,  170,  185,  203,  222,  242,  265,  290,
     317,  346,  378,  414,  452,  494,  540,  591,
     646,  706,  771,  843,  922, 1007, 1101, 1204,
    1316, 1438, 1572, 1719, 1879, 2053, 2244, 2453,
    2682, 2931, 3204, 3502, 3828, 4184, 4574, 5000,
};

static const int16_t  dss_sp_pulse_val[8] = {
    -31182, -22273, -13364, -4455, 4455, 13364, 22273, 31182
};

static const uint16_t binary_decreasing_array[] = {
    32767, 16384, 8192, 4096, 2048, 1024, 512, 256,
    128, 64, 32, 16, 8, 4, 2,
};

static const uint16_t dss_sp_unc_decreasing_array[] = {
    32767, 26214, 20972, 16777, 13422, 10737, 8590, 6872,
    5498, 4398, 3518, 2815, 2252, 1801, 1441,
};

static const uint16_t dss_sp_adaptive_gain[] = {
     102,  231,  360,  488,  617,  746,  875, 1004,
    1133, 1261, 1390, 1519, 1648, 1777, 1905, 2034,
    2163, 2292, 2421, 2550, 2678, 2807, 2936, 3065,
    3194, 3323, 3451, 3580, 3709, 3838, 3967, 4096,
};

static const int32_t dss_sp_sinc[67] = {
      262,   293,   323,   348,   356,   336,   269,   139,
      -67,  -358,  -733, -1178, -1668, -2162, -2607, -2940,
    -3090, -2986, -2562, -1760,  -541,  1110,  3187,  5651,
     8435, 11446, 14568, 17670, 20611, 23251, 25460, 27125,
    28160, 28512, 28160,
    27125, 25460, 23251, 20611, 17670, 14568, 11446,  8435,
     5651,  3187,  1110,  -541, -1760, -2562, -2986, -3090,
    -2940, -2607, -2162, -1668, -1178,  -733,  -358,   -67,
      139,   269,   336,   356,   348,   323,   293,   262,
};

static av_cold int dss_sp_decode_init(AVCodecContext *avctx)
{
    DssSpContext *p = avctx->priv_data;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;
    avctx->channels       = 1;
    avctx->sample_rate    = 11025;

    memset(p->history, 0, sizeof(p->history));
    p->pulse_dec_mode = 1;
    p->avctx          = avctx;

    return 0;
}

static void dss_sp_unpack_coeffs(DssSpContext *p, const uint8_t *src)
{
    GetBitContext gb;
    DssSpFrame *fparam = &p->fparam;
    int i;
    int subframe_idx;
    uint32_t combined_pitch;
    uint32_t tmp;
    uint32_t pitch_lag;

    for (i = 0; i < DSS_SP_FRAME_SIZE; i += 2) {
        p->bits[i]     = src[i + 1];
        p->bits[i + 1] = src[i];
    }

    init_get_bits(&gb, p->bits, DSS_SP_FRAME_SIZE * 8);

    for (i = 0; i < 2; i++)
        fparam->filter_idx[i] = get_bits(&gb, 5);
    for (; i < 8; i++)
        fparam->filter_idx[i] = get_bits(&gb, 4);
    for (; i < 14; i++)
        fparam->filter_idx[i] = get_bits(&gb, 3);

    for (subframe_idx = 0; subframe_idx < 4; subframe_idx++) {
        fparam->sf_adaptive_gain[subframe_idx] = get_bits(&gb, 5);

        fparam->sf[subframe_idx].combined_pulse_pos = get_bits_long(&gb, 31);

        fparam->sf[subframe_idx].gain = get_bits(&gb, 6);

        for (i = 0; i < 7; i++)
            fparam->sf[subframe_idx].pulse_val[i] = get_bits(&gb, 3);
    }

    for (subframe_idx = 0; subframe_idx < 4; subframe_idx++) {
        unsigned int C72_binomials[PULSE_MAX] = {
            72, 2556, 59640, 1028790, 13991544, 156238908, 1473109704,
            3379081753
        };
        unsigned int combined_pulse_pos =
            fparam->sf[subframe_idx].combined_pulse_pos;
        int index = 6;

        if (combined_pulse_pos < C72_binomials[PULSE_MAX - 1]) {
            if (p->pulse_dec_mode) {
                int pulse, pulse_idx;
                pulse              = PULSE_MAX - 1;
                pulse_idx          = 71;
                combined_pulse_pos =
                    fparam->sf[subframe_idx].combined_pulse_pos;

                /* this part seems to be close to g723.1 gen_fcb_excitation()
                 * RATE_6300 */

                /* TODO: what is 7? size of subframe? */
                for (i = 0; i < 7; i++) {
                    for (;
                         combined_pulse_pos <
                         dss_sp_combinatorial_table[pulse][pulse_idx];
                         --pulse_idx)
                        ;
                    combined_pulse_pos -=
                        dss_sp_combinatorial_table[pulse][pulse_idx];
                    pulse--;
                    fparam->sf[subframe_idx].pulse_pos[i] = pulse_idx;
                }
            }
        } else {
            p->pulse_dec_mode = 0;

            /* why do we need this? */
            fparam->sf[subframe_idx].pulse_pos[6] = 0;

            for (i = 71; i >= 0; i--) {
                if (C72_binomials[index] <= combined_pulse_pos) {
                    combined_pulse_pos -= C72_binomials[index];

                    fparam->sf[subframe_idx].pulse_pos[6 - index] = i;

                    if (!index)
                        break;
                    --index;
                }
                --C72_binomials[0];
                if (index) {
                    int a;
                    for (a = 0; a < index; a++)
                        C72_binomials[a + 1] -= C72_binomials[a];
                }
            }
        }
    }

    combined_pitch = get_bits(&gb, 24);

    fparam->pitch_lag[0] = (combined_pitch % 151) + 36;

    combined_pitch /= 151;

    for (i = 1; i < SUBFRAMES - 1; i++) {
        fparam->pitch_lag[i] = combined_pitch % 48;
        combined_pitch      /= 48;
    }
    if (combined_pitch > 47) {
        av_log (p->avctx, AV_LOG_WARNING, "combined_pitch was too large\n");
        combined_pitch = 0;
    }
    fparam->pitch_lag[i] = combined_pitch;

    pitch_lag = fparam->pitch_lag[0];
    for (i = 1; i < SUBFRAMES; i++) {
        if (pitch_lag > 162) {
            fparam->pitch_lag[i] += 162 - 23;
        } else {
            tmp = pitch_lag - 23;
            if (tmp < 36)
                tmp = 36;
            fparam->pitch_lag[i] += tmp;
        }
        pitch_lag = fparam->pitch_lag[i];
    }
}

static void dss_sp_unpack_filter(DssSpContext *p)
{
    int i;

    for (i = 0; i < 14; i++)
        p->lpc_filter[i] = dss_sp_filter_cb[i][p->fparam.filter_idx[i]];
}

static void dss_sp_convert_coeffs(int32_t *lpc_filter, int32_t *coeffs)
{
    int a, a_plus, i;

    coeffs[0] = 0x2000;
    for (a = 0; a < 14; a++) {
        a_plus         = a + 1;
        coeffs[a_plus] = lpc_filter[a] >> 2;
        if (a_plus / 2 >= 1) {
            for (i = 1; i <= a_plus / 2; i++) {
                int coeff_1, coeff_2, tmp;

                coeff_1 = coeffs[i];
                coeff_2 = coeffs[a_plus - i];

                tmp = DSS_SP_FORMULA(coeff_1, lpc_filter[a], coeff_2);
                coeffs[i] = av_clip_int16(tmp);

                tmp = DSS_SP_FORMULA(coeff_2, lpc_filter[a], coeff_1);
                coeffs[a_plus - i] = av_clip_int16(tmp);
            }
        }
    }
}

static void dss_sp_add_pulses(int32_t *vector_buf,
                              const struct DssSpSubframe *sf)
{
    int i;

    for (i = 0; i < 7; i++)
        vector_buf[sf->pulse_pos[i]] += (dss_sp_fixed_cb_gain[sf->gain] *
                                         dss_sp_pulse_val[sf->pulse_val[i]] +
                                         0x4000) >> 15;
}

static void dss_sp_gen_exc(int32_t *vector, int32_t *prev_exc,
                           int pitch_lag, int gain)
{
    int i;

    /* do we actually need this check? we can use just [a3 - i % a3]
     * for both cases */
    if (pitch_lag < 72)
        for (i = 0; i < 72; i++)
            vector[i] = prev_exc[pitch_lag - i % pitch_lag];
    else
        for (i = 0; i < 72; i++)
            vector[i] = prev_exc[pitch_lag - i];

    for (i = 0; i < 72; i++) {
        int tmp = gain * vector[i] >> 11;
        vector[i] = av_clip_int16(tmp);
    }
}

static void dss_sp_scale_vector(int32_t *vec, int bits, int size)
{
    int i;

    if (bits < 0)
        for (i = 0; i < size; i++)
            vec[i] = vec[i] >> -bits;
    else
        for (i = 0; i < size; i++)
            vec[i] = vec[i] * (1 << bits);
}

static void dss_sp_update_buf(int32_t *hist, int32_t *vector)
{
    int i;

    for (i = 114; i > 0; i--)
        vector[i + 72] = vector[i];

    for (i = 0; i < 72; i++)
        vector[72 - i] = hist[i];
}

static void dss_sp_shift_sq_sub(const int32_t *filter_buf,
                                int32_t *error_buf, int32_t *dst)
{
    int a;

    for (a = 0; a < 72; a++) {
        int i, tmp;

        tmp = dst[a] * filter_buf[0];

        for (i = 14; i > 0; i--)
            tmp -= error_buf[i] * (unsigned)filter_buf[i];

        for (i = 14; i > 0; i--)
            error_buf[i] = error_buf[i - 1];

        tmp = (int)(tmp + 4096U) >> 13;

        error_buf[1] = tmp;

        dst[a] = av_clip_int16(tmp);
    }
}

static void dss_sp_shift_sq_add(const int32_t *filter_buf, int32_t *audio_buf,
                                int32_t *dst)
{
    int a;

    for (a = 0; a < 72; a++) {
        int i, tmp = 0;

        audio_buf[0] = dst[a];

        for (i = 14; i >= 0; i--)
            tmp += audio_buf[i] * filter_buf[i];

        for (i = 14; i > 0; i--)
            audio_buf[i] = audio_buf[i - 1];

        tmp = (tmp + 4096) >> 13;

        dst[a] = av_clip_int16(tmp);
    }
}

static void dss_sp_vec_mult(const int32_t *src, int32_t *dst,
                            const int16_t *mult)
{
    int i;

    dst[0] = src[0];

    for (i = 1; i < 15; i++)
        dst[i] = (src[i] * mult[i] + 0x4000) >> 15;
}

static int dss_sp_get_normalize_bits(int32_t *vector_buf, int16_t size)
{
    unsigned int val;
    int max_val;
    int i;

    val = 1;
    for (i = 0; i < size; i++)
        val |= FFABS(vector_buf[i]);

    for (max_val = 0; val <= 0x4000; ++max_val)
        val *= 2;
    return max_val;
}

static int dss_sp_vector_sum(DssSpContext *p, int size)
{
    int i, sum = 0;
    for (i = 0; i < size; i++)
        sum += FFABS(p->vector_buf[i]);
    return sum;
}

static void dss_sp_sf_synthesis(DssSpContext *p, int32_t lpc_filter,
                                int32_t *dst, int size)
{
    int32_t tmp_buf[15];
    int32_t noise[72];
    int bias, vsum_2 = 0, vsum_1 = 0, v36, normalize_bits;
    int i, tmp;

    if (size > 0) {
        vsum_1 = dss_sp_vector_sum(p, size);

        if (vsum_1 > 0xFFFFF)
            vsum_1 = 0xFFFFF;
    }

    normalize_bits = dss_sp_get_normalize_bits(p->vector_buf, size);

    dss_sp_scale_vector(p->vector_buf, normalize_bits - 3, size);
    dss_sp_scale_vector(p->audio_buf, normalize_bits, 15);
    dss_sp_scale_vector(p->err_buf1, normalize_bits, 15);

    v36 = p->err_buf1[1];

    dss_sp_vec_mult(p->filter, tmp_buf, binary_decreasing_array);
    dss_sp_shift_sq_add(tmp_buf, p->audio_buf, p->vector_buf);

    dss_sp_vec_mult(p->filter, tmp_buf, dss_sp_unc_decreasing_array);
    dss_sp_shift_sq_sub(tmp_buf, p->err_buf1, p->vector_buf);

    /* lpc_filter can be negative */
    lpc_filter = lpc_filter >> 1;
    if (lpc_filter >= 0)
        lpc_filter = 0;

    if (size > 1) {
        for (i = size - 1; i > 0; i--) {
            tmp = DSS_SP_FORMULA(p->vector_buf[i], lpc_filter,
                                 p->vector_buf[i - 1]);
            p->vector_buf[i] = av_clip_int16(tmp);
        }
    }

    tmp              = DSS_SP_FORMULA(p->vector_buf[0], lpc_filter, v36);
    p->vector_buf[0] = av_clip_int16(tmp);

    dss_sp_scale_vector(p->vector_buf, -normalize_bits, size);
    dss_sp_scale_vector(p->audio_buf, -normalize_bits, 15);
    dss_sp_scale_vector(p->err_buf1, -normalize_bits, 15);

    if (size > 0)
        vsum_2 = dss_sp_vector_sum(p, size);

    if (vsum_2 >= 0x40)
        tmp = (vsum_1 << 11) / vsum_2;
    else
        tmp = 1;

    bias     = 409 * tmp >> 15 << 15;
    tmp      = (bias + 32358 * p->noise_state) >> 15;
    noise[0] = av_clip_int16(tmp);

    for (i = 1; i < size; i++) {
        tmp      = (bias + 32358 * noise[i - 1]) >> 15;
        noise[i] = av_clip_int16(tmp);
    }

    p->noise_state = noise[size - 1];
    for (i = 0; i < size; i++) {
        tmp    = (p->vector_buf[i] * noise[i]) >> 11;
        dst[i] = av_clip_int16(tmp);
    }
}

static void dss_sp_update_state(DssSpContext *p, int32_t *dst)
{
    int i, offset = 6, counter = 0, a = 0;

    for (i = 0; i < 6; i++)
        p->excitation[i] = p->excitation[288 + i];

    for (i = 0; i < 72 * SUBFRAMES; i++)
        p->excitation[6 + i] = dst[i];

    do {
        int tmp = 0;

        for (i = 0; i < 6; i++)
            tmp += p->excitation[offset--] * dss_sp_sinc[a + i * 11];

        offset += 7;

        tmp >>= 15;
        dst[counter] = av_clip_int16(tmp);

        counter++;

        a = (a + 1) % 11;
        if (!a)
            offset++;
    } while (offset < FF_ARRAY_ELEMS(p->excitation));
}

static void dss_sp_32to16bit(int16_t *dst, int32_t *src, int size)
{
    int i;

    for (i = 0; i < size; i++)
        dst[i] = av_clip_int16(src[i]);
}

static int dss_sp_decode_one_frame(DssSpContext *p,
                                   int16_t *abuf_dst, const uint8_t *abuf_src)
{
    int i, j;

    dss_sp_unpack_coeffs(p, abuf_src);

    dss_sp_unpack_filter(p);

    dss_sp_convert_coeffs(p->lpc_filter, p->filter);

    for (j = 0; j < SUBFRAMES; j++) {
        dss_sp_gen_exc(p->vector_buf, p->history,
                       p->fparam.pitch_lag[j],
                       dss_sp_adaptive_gain[p->fparam.sf_adaptive_gain[j]]);

        dss_sp_add_pulses(p->vector_buf, &p->fparam.sf[j]);

        dss_sp_update_buf(p->vector_buf, p->history);

        for (i = 0; i < 72; i++)
            p->vector_buf[i] = p->history[72 - i];

        dss_sp_shift_sq_sub(p->filter,
                            p->err_buf2, p->vector_buf);

        dss_sp_sf_synthesis(p, p->lpc_filter[0],
                            &p->working_buffer[j][0], 72);
    }

    dss_sp_update_state(p, &p->working_buffer[0][0]);

    dss_sp_32to16bit(abuf_dst,
                     &p->working_buffer[0][0], 264);
    return 0;
}

static int dss_sp_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DssSpContext *p    = avctx->priv_data;
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    int16_t *out;
    int ret;

    if (buf_size < DSS_SP_FRAME_SIZE) {
        if (buf_size)
            av_log(avctx, AV_LOG_WARNING,
                   "Expected %d bytes, got %d - skipping packet.\n",
                   DSS_SP_FRAME_SIZE, buf_size);
        *got_frame_ptr = 0;
        return AVERROR_INVALIDDATA;
    }

    frame->nb_samples = DSS_SP_SAMPLE_COUNT;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    out = (int16_t *)frame->data[0];

    dss_sp_decode_one_frame(p, out, buf);

    *got_frame_ptr = 1;

    return DSS_SP_FRAME_SIZE;
}

AVCodec ff_dss_sp_decoder = {
    .name           = "dss_sp",
    .long_name      = NULL_IF_CONFIG_SMALL("Digital Speech Standard - Standard Play mode (DSS SP)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DSS_SP,
    .priv_data_size = sizeof(DssSpContext),
    .init           = dss_sp_decode_init,
    .decode         = dss_sp_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
