/*
 * Copyright (C) 2007 Marco Gerards <marco@gnu.org>
 * Copyright (C) 2009 David Conrad
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
 * Arithmetic decoder for Dirac
 * @author Marco Gerards <marco@gnu.org>
 */

#include "dirac_arith.h"


static const uint16_t dirac_prob[256] = {
    0,    2,    5,    8,    11,   15,   20,   24,
    29,   35,   41,   47,   53,   60,   67,   74,
    82,   89,   97,   106,  114,  123,  132,  141,
    150,  160,  170,  180,  190,  201,  211,  222,
    233,  244,  256,  267,  279,  291,  303,  315,
    327,  340,  353,  366,  379,  392,  405,  419,
    433,  447,  461,  475,  489,  504,  518,  533,
    548,  563,  578,  593,  609,  624,  640,  656,
    672,  688,  705,  721,  738,  754,  771,  788,
    805,  822,  840,  857,  875,  892,  910,  928,
    946,  964,  983,  1001, 1020, 1038, 1057, 1076,
    1095, 1114, 1133, 1153, 1172, 1192, 1211, 1231,
    1251, 1271, 1291, 1311, 1332, 1352, 1373, 1393,
    1414, 1435, 1456, 1477, 1498, 1520, 1541, 1562,
    1584, 1606, 1628, 1649, 1671, 1694, 1716, 1738,
    1760, 1783, 1806, 1828, 1851, 1874, 1897, 1920,
    1935, 1942, 1949, 1955, 1961, 1968, 1974, 1980,
    1985, 1991, 1996, 2001, 2006, 2011, 2016, 2021,
    2025, 2029, 2033, 2037, 2040, 2044, 2047, 2050,
    2053, 2056, 2058, 2061, 2063, 2065, 2066, 2068,
    2069, 2070, 2071, 2072, 2072, 2072, 2072, 2072,
    2072, 2071, 2070, 2069, 2068, 2066, 2065, 2063,
    2060, 2058, 2055, 2052, 2049, 2045, 2042, 2038,
    2033, 2029, 2024, 2019, 2013, 2008, 2002, 1996,
    1989, 1982, 1975, 1968, 1960, 1952, 1943, 1934,
    1925, 1916, 1906, 1896, 1885, 1874, 1863, 1851,
    1839, 1827, 1814, 1800, 1786, 1772, 1757, 1742,
    1727, 1710, 1694, 1676, 1659, 1640, 1622, 1602,
    1582, 1561, 1540, 1518, 1495, 1471, 1447, 1422,
    1396, 1369, 1341, 1312, 1282, 1251, 1219, 1186,
    1151, 1114, 1077, 1037, 995,  952,  906,  857,
    805,  750,  690,  625,  553,  471,  376,  255
};

const uint8_t ff_dirac_next_ctx[DIRAC_CTX_COUNT] = {
    [CTX_ZPZN_F1]   = CTX_ZP_F2,
    [CTX_ZPNN_F1]   = CTX_ZP_F2,
    [CTX_ZP_F2]     = CTX_ZP_F3,
    [CTX_ZP_F3]     = CTX_ZP_F4,
    [CTX_ZP_F4]     = CTX_ZP_F5,
    [CTX_ZP_F5]     = CTX_ZP_F6,
    [CTX_ZP_F6]     = CTX_ZP_F6,
    [CTX_NPZN_F1]   = CTX_NP_F2,
    [CTX_NPNN_F1]   = CTX_NP_F2,
    [CTX_NP_F2]     = CTX_NP_F3,
    [CTX_NP_F3]     = CTX_NP_F4,
    [CTX_NP_F4]     = CTX_NP_F5,
    [CTX_NP_F5]     = CTX_NP_F6,
    [CTX_NP_F6]     = CTX_NP_F6,
    [CTX_DELTA_Q_F] = CTX_DELTA_Q_F,
};

int16_t ff_dirac_prob_branchless[256][2];

av_cold void ff_dirac_init_arith_tables(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        ff_dirac_prob_branchless[i][0] =  dirac_prob[255-i];
        ff_dirac_prob_branchless[i][1] = -dirac_prob[i];
    }
}

void ff_dirac_init_arith_decoder(DiracArith *c, GetBitContext *gb, int length)
{
    int i;
    align_get_bits(gb);

    length = FFMIN(length, get_bits_left(gb)/8);

    c->bytestream     = gb->buffer + get_bits_count(gb)/8;
    c->bytestream_end = c->bytestream + length;
    skip_bits_long(gb, length*8);

    c->low = 0;
    for (i = 0; i < 4; i++) {
        c->low <<= 8;
        if (c->bytestream < c->bytestream_end)
            c->low |= *c->bytestream++;
        else
            c->low |= 0xff;
    }

    c->counter = -16;
    c->range   = 0xffff;
    c->error   = 0;
    c->overread= 0;

    for (i = 0; i < DIRAC_CTX_COUNT; i++)
        c->contexts[i] = 0x8000;
}
