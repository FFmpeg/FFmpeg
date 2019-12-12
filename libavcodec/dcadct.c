/*
 * Copyright (C) 2016 foo86
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

#include "libavutil/common.h"

#include "dcadct.h"
#include "dcamath.h"

static void sum_a(const int *input, int *output, int len)
{
    int i;

    for (i = 0; i < len; i++)
        output[i] = input[2 * i] + input[2 * i + 1];
}

static void sum_b(const int *input, int *output, int len)
{
    int i;

    output[0] = input[0];
    for (i = 1; i < len; i++)
        output[i] = input[2 * i] + input[2 * i - 1];
}

static void sum_c(const int *input, int *output, int len)
{
    int i;

    for (i = 0; i < len; i++)
        output[i] = input[2 * i];
}

static void sum_d(const int *input, int *output, int len)
{
    int i;

    output[0] = input[1];
    for (i = 1; i < len; i++)
        output[i] = input[2 * i - 1] + input[2 * i + 1];
}

static void dct_a(const int *input, int *output)
{
    static const int cos_mod[8][8] = {
         { 8348215,  8027397,  7398092,  6484482,  5321677,  3954362,  2435084,   822227 },
         { 8027397,  5321677,   822227, -3954362, -7398092, -8348215, -6484482, -2435084 },
         { 7398092,   822227, -6484482, -8027397, -2435084,  5321677,  8348215,  3954362 },
         { 6484482, -3954362, -8027397,   822227,  8348215,  2435084, -7398092, -5321677 },
         { 5321677, -7398092, -2435084,  8348215,  -822227, -8027397,  3954362,  6484482 },
         { 3954362, -8348215,  5321677,  2435084, -8027397,  6484482,   822227, -7398092 },
         { 2435084, -6484482,  8348215, -7398092,  3954362,   822227, -5321677,  8027397 },
         {  822227, -2435084,  3954362, -5321677,  6484482, -7398092,  8027397, -8348215 }
    };

    int i, j;

    for (i = 0; i < 8; i++) {
        int64_t res = 0;
        for (j = 0; j < 8; j++)
            res += (int64_t)cos_mod[i][j] * input[j];
        output[i] = norm23(res);
    }
}

static void dct_b(const int *input, int *output)
{
    static const int cos_mod[8][7] = {
        {  8227423,  7750063,  6974873,  5931642,  4660461,  3210181,  1636536 },
        {  6974873,  3210181, -1636536, -5931642, -8227423, -7750063, -4660461 },
        {  4660461, -3210181, -8227423, -5931642,  1636536,  7750063,  6974873 },
        {  1636536, -7750063, -4660461,  5931642,  6974873, -3210181, -8227423 },
        { -1636536, -7750063,  4660461,  5931642, -6974873, -3210181,  8227423 },
        { -4660461, -3210181,  8227423, -5931642, -1636536,  7750063, -6974873 },
        { -6974873,  3210181,  1636536, -5931642,  8227423, -7750063,  4660461 },
        { -8227423,  7750063, -6974873,  5931642, -4660461,  3210181, -1636536 }
    };

    int i, j;

    for (i = 0; i < 8; i++) {
        int64_t res = input[0] * (INT64_C(1) << 23);
        for (j = 0; j < 7; j++)
            res += (int64_t)cos_mod[i][j] * input[1 + j];
        output[i] = norm23(res);
    }
}

static void mod_a(const int *input, int *output)
{
    static const int cos_mod[16] = {
          4199362,   4240198,   4323885,   4454708,
          4639772,   4890013,   5221943,   5660703,
         -6245623,  -7040975,  -8158494,  -9809974,
        -12450076, -17261920, -28585092, -85479984
    };

    int i, k;

    for (i = 0; i < 8; i++)
        output[i] = mul23(cos_mod[i], input[i] + input[8 + i]);

    for (i = 8, k = 7; i < 16; i++, k--)
        output[i] = mul23(cos_mod[i], input[k] - input[8 + k]);
}

static void mod_b(int *input, int *output)
{
    static const int cos_mod[8] = {
        4214598,  4383036,  4755871,  5425934,
        6611520,  8897610, 14448934, 42791536
    };

    int i, k;

    for (i = 0; i < 8; i++)
        input[8 + i] = mul23(cos_mod[i], input[8 + i]);

    for (i = 0; i < 8; i++)
        output[i] = input[i] + input[8 + i];

    for (i = 8, k = 7; i < 16; i++, k--)
        output[i] = input[k] - input[8 + k];
}

static void mod_c(const int *input, int *output)
{
    static const int cos_mod[32] = {
         1048892,  1051425,   1056522,   1064244,
         1074689,  1087987,   1104313,   1123884,
         1146975,  1173922,   1205139,   1241133,
         1282529,  1330095,   1384791,   1447815,
        -1520688, -1605358,  -1704360,  -1821051,
        -1959964, -2127368,  -2332183,  -2587535,
        -2913561, -3342802,  -3931480,  -4785806,
        -6133390, -8566050, -14253820, -42727120
    };

    int i, k;

    for (i = 0; i < 16; i++)
        output[i] = mul23(cos_mod[i], input[i] + input[16 + i]);

    for (i = 16, k = 15; i < 32; i++, k--)
        output[i] = mul23(cos_mod[i], input[k] - input[16 + k]);
}

static void clp_v(int *input, int len)
{
    int i;

    for (i = 0; i < len; i++)
        input[i] = clip23(input[i]);
}

static void imdct_half_32(int32_t *output, const int32_t *input)
{
    int buf_a[32], buf_b[32];
    int i, k, mag, shift, round;

    mag = 0;
    for (i = 0; i < 32; i++)
        mag += abs(input[i]);

    shift = mag > 0x400000 ? 2 : 0;
    round = shift > 0 ? 1 << (shift - 1) : 0;

    for (i = 0; i < 32; i++)
        buf_a[i] = (input[i] + round) >> shift;

    sum_a(buf_a, buf_b +  0, 16);
    sum_b(buf_a, buf_b + 16, 16);
    clp_v(buf_b, 32);

    sum_a(buf_b +  0, buf_a +  0, 8);
    sum_b(buf_b +  0, buf_a +  8, 8);
    sum_c(buf_b + 16, buf_a + 16, 8);
    sum_d(buf_b + 16, buf_a + 24, 8);
    clp_v(buf_a, 32);

    dct_a(buf_a +  0, buf_b +  0);
    dct_b(buf_a +  8, buf_b +  8);
    dct_b(buf_a + 16, buf_b + 16);
    dct_b(buf_a + 24, buf_b + 24);
    clp_v(buf_b, 32);

    mod_a(buf_b +  0, buf_a +  0);
    mod_b(buf_b + 16, buf_a + 16);
    clp_v(buf_a, 32);

    mod_c(buf_a, buf_b);

    for (i = 0; i < 32; i++)
        buf_b[i] = clip23(buf_b[i] * (1 << shift));

    for (i = 0, k = 31; i < 16; i++, k--) {
        output[     i] = clip23(buf_b[i] - buf_b[k]);
        output[16 + i] = clip23(buf_b[i] + buf_b[k]);
    }
}

static void mod64_a(const int *input, int *output)
{
    static const int cos_mod[32] = {
          4195568,   4205700,   4226086,    4256977,
          4298755,   4351949,   4417251,    4495537,
          4587901,   4695690,   4820557,    4964534,
          5130115,   5320382,   5539164,    5791261,
         -6082752,  -6421430,  -6817439,   -7284203,
         -7839855,  -8509474,  -9328732,  -10350140,
        -11654242, -13371208, -15725922,  -19143224,
        -24533560, -34264200, -57015280, -170908480
    };

    int i, k;

    for (i = 0; i < 16; i++)
        output[i] = mul23(cos_mod[i], input[i] + input[16 + i]);

    for (i = 16, k = 15; i < 32; i++, k--)
        output[i] = mul23(cos_mod[i], input[k] - input[16 + k]);
}

static void mod64_b(int *input, int *output)
{
    static const int cos_mod[16] = {
         4199362,  4240198,  4323885,  4454708,
         4639772,  4890013,  5221943,  5660703,
         6245623,  7040975,  8158494,  9809974,
        12450076, 17261920, 28585092, 85479984
    };

    int i, k;

    for (i = 0; i < 16; i++)
        input[16 + i] = mul23(cos_mod[i], input[16 + i]);

    for (i = 0; i < 16; i++)
        output[i] = input[i] + input[16 + i];

    for (i = 16, k = 15; i < 32; i++, k--)
        output[i] = input[k] - input[16 + k];
}

static void mod64_c(const int *input, int *output)
{
    static const int cos_mod[64] = {
          741511,    741958,    742853,    744199,
          746001,    748262,    750992,    754197,
          757888,    762077,    766777,    772003,
          777772,    784105,    791021,    798546,
          806707,    815532,    825054,    835311,
          846342,    858193,    870912,    884554,
          899181,    914860,    931667,    949686,
          969011,    989747,   1012012,   1035941,
        -1061684,  -1089412,  -1119320,  -1151629,
        -1186595,  -1224511,  -1265719,  -1310613,
        -1359657,  -1413400,  -1472490,  -1537703,
        -1609974,  -1690442,  -1780506,  -1881904,
        -1996824,  -2128058,  -2279225,  -2455101,
        -2662128,  -2909200,  -3208956,  -3579983,
        -4050785,  -4667404,  -5509372,  -6726913,
        -8641940, -12091426, -20144284, -60420720
    };

    int i, k;

    for (i = 0; i < 32; i++)
        output[i] = mul23(cos_mod[i], input[i] + input[32 + i]);

    for (i = 32, k = 31; i < 64; i++, k--)
        output[i] = mul23(cos_mod[i], input[k] - input[32 + k]);
}

static void imdct_half_64(int32_t *output, const int32_t *input)
{
    int buf_a[64], buf_b[64];
    int i, k, mag, shift, round;

    mag = 0;
    for (i = 0; i < 64; i++)
        mag += abs(input[i]);

    shift = mag > 0x400000 ? 2 : 0;
    round = shift > 0 ? 1 << (shift - 1) : 0;

    for (i = 0; i < 64; i++)
        buf_a[i] = (input[i] + round) >> shift;

    sum_a(buf_a, buf_b +  0, 32);
    sum_b(buf_a, buf_b + 32, 32);
    clp_v(buf_b, 64);

    sum_a(buf_b +  0, buf_a +  0, 16);
    sum_b(buf_b +  0, buf_a + 16, 16);
    sum_c(buf_b + 32, buf_a + 32, 16);
    sum_d(buf_b + 32, buf_a + 48, 16);
    clp_v(buf_a, 64);

    sum_a(buf_a +  0, buf_b +  0, 8);
    sum_b(buf_a +  0, buf_b +  8, 8);
    sum_c(buf_a + 16, buf_b + 16, 8);
    sum_d(buf_a + 16, buf_b + 24, 8);
    sum_c(buf_a + 32, buf_b + 32, 8);
    sum_d(buf_a + 32, buf_b + 40, 8);
    sum_c(buf_a + 48, buf_b + 48, 8);
    sum_d(buf_a + 48, buf_b + 56, 8);
    clp_v(buf_b, 64);

    dct_a(buf_b +  0, buf_a +  0);
    dct_b(buf_b +  8, buf_a +  8);
    dct_b(buf_b + 16, buf_a + 16);
    dct_b(buf_b + 24, buf_a + 24);
    dct_b(buf_b + 32, buf_a + 32);
    dct_b(buf_b + 40, buf_a + 40);
    dct_b(buf_b + 48, buf_a + 48);
    dct_b(buf_b + 56, buf_a + 56);
    clp_v(buf_a, 64);

    mod_a(buf_a +  0, buf_b +  0);
    mod_b(buf_a + 16, buf_b + 16);
    mod_b(buf_a + 32, buf_b + 32);
    mod_b(buf_a + 48, buf_b + 48);
    clp_v(buf_b, 64);

    mod64_a(buf_b +  0, buf_a +  0);
    mod64_b(buf_b + 32, buf_a + 32);
    clp_v(buf_a, 64);

    mod64_c(buf_a, buf_b);

    for (i = 0; i < 64; i++)
        buf_b[i] = clip23(buf_b[i] * (1 << shift));

    for (i = 0, k = 63; i < 32; i++, k--) {
        output[     i] = clip23(buf_b[i] - buf_b[k]);
        output[32 + i] = clip23(buf_b[i] + buf_b[k]);
    }
}

av_cold void ff_dcadct_init(DCADCTContext *c)
{
    c->imdct_half[0] = imdct_half_32;
    c->imdct_half[1] = imdct_half_64;
}
