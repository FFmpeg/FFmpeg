/*
 * Copyright (c) 2015 Kieran Kunhya
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

#include <stdint.h>

#include "libavutil/attributes.h"

#include "cfhd.h"

/* some special codewords, not sure what they all mean */
#define TABLE_9_BAND_END1 0x1C7859Eh
#define TABLE_9_BAND_END_LEN1 25
#define TABLE_9_BAND_END2 0x38F0B3Fh
#define TABLE_9_BAND_END_LEN2 26
#define TABLE_9_BAND_END3 0x38F0B3Eh
#define TABLE_9_BAND_END_LEN3 26

#define NB_VLC_TABLE_9   (71 + 3)
#define NB_VLC_TABLE_18 (263 + 1)

static const uint32_t table_9_vlc_bits[NB_VLC_TABLE_9] = {
            0,       0x2,       0xc,      0x1a,
         0x1d,      0x1e,      0x39,      0x3e,
         0x37,      0x7e,      0x6c,      0xe2,
         0xfe,      0xdb,      0xe0,     0x1c3,
        0x1c6,     0x1ff,     0x1fe,     0x1b5,
        0x369,     0x385,     0x71d,     0x6d0,
        0x708,     0x71f,     0xe3d,     0xe39,
        0xe13,     0xe12,    0x1c71,    0x1b45,
       0x1b47,    0x3689,    0x38f2,    0x38e1,
       0x38e0,    0x38f1,    0x3688,    0x6d1b,
       0x71e0,    0x6d19,    0x71e7,    0xe3cd,
       0xda35,    0xda30,    0xe3c3,   0x1b469,
      0x1b462,   0x1c798,   0x1b463,   0x1c799,
      0x38f08,   0x38f09,   0x38f0a,   0x6d1a0,
      0x6d1a3,   0x6d1a1,   0xda345,   0xda344,
      0xe3c2d,   0xe3c2f,   0xe3c2e,  0x38f0b2,
     0x71e160,  0x71e162,  0x71e166,  0x71e161,
     0xe3c2ce,  0xe3c2c6,  0xe3c2c7, 0x1C7859E,
    0x38F0B3F, 0x38F0B3E,
};

static const uint8_t table_9_vlc_len[NB_VLC_TABLE_9] = {
     1,    2,    4,    5,    5,    5,    6,    6,
     6,    7,    7,    8,    8,    8,    8,    9,
     9,    9,    9,    9,   10,   10,   11,   11,
    11,   11,   12,   12,   12,   12,   13,   13,
    13,   14,   14,   14,   14,   14,   14,   15,
    15,   15,   15,   16,   16,   16,   16,   17,
    17,   17,   17,   17,   18,   18,   18,   19,
    19,   19,   20,   20,   20,   20,   20,   22,
    23,   23,   23,   23,   24,   24,   24,   25,
    26,   26,
};

static const uint16_t table_9_vlc_run[NB_VLC_TABLE_9] = {
    1,    1,    1,    1,   12,    1,   32,  160,
    1,    1,    1,  320,    1,    1,   80,  120,
    1,    1,  100,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1
};

static const uint8_t table_9_vlc_level[NB_VLC_TABLE_9] = {
     0,    1,    2,    3,    0,    4,    0,    0,
     5,    7,    6,    0,    9,    8,    0,    0,
    11,   12,    0,   10,   13,   14,   17,   15,
    16,   18,   22,   21,   20,   19,   25,   23,
    24,   27,   31,   29,   28,   30,   26,   33,
    34,   32,   35,   39,   37,   36,   38,   42,
    40,   43,   41,   44,   45,   46,   47,   48,
    50,   49,   52,   51,   53,   55,   54,   56,
    57,   59,   60,   58,   61,   62,   63,   64,
    64,   64,
};

static const uint32_t table_18_vlc_bits[NB_VLC_TABLE_18] = {
            0,       0x2,       0x7,      0x19,
         0x30,      0x36,      0x6f,      0x63,
         0x69,      0x6b,      0xd1,      0xd4,
         0xdc,     0x189,     0x18a,     0x1a0,
        0x1ab,     0x377,     0x310,     0x316,
        0x343,     0x354,     0x375,     0x623,
        0x684,     0x685,     0x6ab,     0x6ec,
        0xddb,     0xc5c,     0xc5e,     0xc44,
        0xd55,     0xdd1,     0xdd3,    0x1bb5,
       0x188b,    0x18bb,    0x18bf,    0x1aa8,
       0x1ba0,    0x1ba5,    0x1ba4,    0x3115,
       0x3175,    0x317d,    0x3553,    0x3768,
       0x6e87,    0x6ed3,    0x62e8,    0x62f8,
       0x6228,    0x6aa4,    0x6e85,    0xc453,
       0xc5d3,    0xc5f3,    0xdda4,    0xdd08,
       0xdd0c,   0x1bb4b,   0x1bb4a,   0x18ba5,
      0x18be5,   0x1aa95,   0x1aa97,   0x188a4,
      0x1ba13,   0x31748,   0x317c8,   0x35528,
      0x3552c,   0x37424,   0x37434,   0x37436,
      0x62294,   0x62e92,   0x62f92,   0x6aa52,
      0x6aa5a,   0x6e86a,   0x6e86e,   0x6e84a,
      0xc452a,   0xc5d27,   0xc5f26,   0xd54a6,
      0xd54b6,   0xdd096,   0xdd0d6,   0xdd0de,
     0x188a56,  0x18ba4d,  0x18be4e,  0x18be4f,
     0x1aa96e,  0x1ba12e,  0x1ba12f,  0x1ba1af,
     0x1ba1bf,  0x37435d,  0x37437d,  0x317498,
     0x35529c,  0x35529d,  0x3552de,  0x3552df,
     0x62e933,  0x62295d,  0x6aa53d,  0x6aa53f,
     0x6aa53e,  0x6e86b9,  0x6e86f8,  0xd54a79,
     0xc5d265,  0xc452b8,  0xdd0d71,  0xd54a78,
     0xdd0d70,  0xdd0df2,  0xdd0df3, 0x188a5f6,
    0x188a5f5, 0x188a5f4, 0x188a5f3, 0x188a5f2,
    0x188a5f1, 0x188a5f0, 0x188a5ef, 0x188a5ee,
    0x188a5ed, 0x188a5aa, 0x188a5e3, 0x188a5df,
    0x188a589, 0x188a5dd, 0x188a578, 0x188a5e0,
    0x188a588, 0x188a5d6, 0x188a5db, 0x188a5e1,
    0x188a587, 0x188a59a, 0x188a5c4, 0x188a5ec,
    0x188a586, 0x188a573, 0x188a59c, 0x188a5c8,
    0x188a5fb, 0x188a5a1, 0x188a5eb, 0x188a5a8,
    0x188a584, 0x188a5d2, 0x188a599, 0x188a598,
    0x188a583, 0x18ba4c9, 0x188a5d0, 0x188a594,
    0x188a582, 0x188a5cb, 0x188a5d8, 0x188a5e7,
    0x188a581, 0x188a5ea, 0x188a5a9, 0x188a5a6,
    0x188a580, 0x188a5a0, 0x188a59d, 0x188a5c3,
    0x188a57f, 0x188a5c0, 0x188a5de, 0x188a5d4,
    0x188a57e, 0x188a5c2, 0x188a592, 0x188a5cd,
    0x188a57d, 0x188a5a3, 0x188a5e8, 0x188a5a2,
    0x188a57c, 0x188a58e, 0x188a5b3, 0x188a5b2,
    0x188a5b1, 0x188a5b0, 0x188a5af, 0x188a5ae,
    0x188a5ad, 0x188a5ac, 0x188a5ab, 0x188a5da,
    0x188a5e4, 0x188a5e5, 0x188a5d9, 0x188a5b5,
    0x188a5bc, 0x188a5bd, 0x188a5e9, 0x188a5cc,
    0x188a585, 0x188a5d3, 0x188a5e2, 0x188a595,
    0x188a596, 0x188a5b8, 0x188a590, 0x188a5c9,
    0x188a5a4, 0x188a5e6, 0x188a5a5, 0x188a5ce,
    0x188a5bf, 0x188a572, 0x188a59b, 0x188a5be,
    0x188a5c7, 0x188a5ca, 0x188a5d5, 0x188a57b,
    0x188a58d, 0x188a58c, 0x188a58b, 0x188a58a,
    0x18ba4c8, 0x188a5c5, 0x188a5fa, 0x188a5bb,
    0x188a5c1, 0x188a5cf, 0x188a5b9, 0x188a5b6,
    0x188a597, 0x188a5fe, 0x188a5d7, 0x188a5ba,
    0x188a591, 0x188a5c6, 0x188a5dc, 0x188a57a,
    0x188a59f, 0x188a5f9, 0x188a5b4, 0x188a5a7,
    0x188a58f, 0x188a5fd, 0x188a5b7, 0x188a593,
    0x188a59e, 0x188a5f8, 0x188a5ff, 0x188a5fc,
    0x188a579, 0x188a5f7, 0x3114ba2, 0x3114ba3,
};

static const uint8_t table_18_vlc_len[NB_VLC_TABLE_18] = {
     1,  2,  3,  5,  6,  6,  7,  7,
     7,  7,  8,  8,  8,  9,  9,  9,
     9, 10, 10, 10, 10, 10, 10, 11,
    11, 11, 11, 11, 12, 12, 12, 12,
    12, 12, 12, 13, 13, 13, 13, 13,
    13, 13, 13, 14, 14, 14, 14, 14,
    15, 15, 15, 15, 15, 15, 15, 16,
    16, 16, 16, 16, 16, 17, 17, 17,
    17, 17, 17, 17, 17, 18, 18, 18,
    18, 18, 18, 18, 19, 19, 19, 19,
    19, 19, 19, 19, 20, 20, 20, 20,
    20, 20, 20, 20, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 22, 22, 22,
    22, 22, 22, 22, 23, 23, 23, 23,
    23, 23, 23, 24, 24, 24, 24, 24,
    24, 24, 24, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 25, 25,
    25, 25, 25, 25, 25, 25, 26, 26,
};

static const uint16_t table_18_vlc_run[NB_VLC_TABLE_18] = {
     1,    1,    1,    1,    1,    1,    1,    1,
    12,    1,   20,    1,    1,    1,   32,    1,
     1,    1,    1,    1,   60,    1,    1,    1,
     1,  100,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,  180,    1,
     1,  320,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    2,
};

static const uint8_t table_18_vlc_level[NB_VLC_TABLE_18] = {
      0,    1,    2,    3,    4,    5,    8,    6,
      0,    7,    0,    9,   10,   11,    0,   12,
     13,   18,   14,   15,    0,   16,   17,   19,
     20,    0,   21,   22,   29,   24,   25,   23,
     26,   27,   28,   35,   30,   31,    0,   32,
     33,    0,   34,   36,   37,   38,   39,   40,
     46,   47,   42,   43,   41,   44,   45,   48,
     49,   50,   53,   51,   52,   61,   60,   55,
     56,   57,   58,   54,   59,   62,   63,   64,
     65,   66,   67,   68,   69,   70,   71,   72,
     73,   75,   76,   74,   77,   78,   79,   80,
     81,   82,   83,   84,   85,   86,   87,   88,
     89,   90,   91,   92,   93,   99,  100,   94,
     95,   96,   97,   98,  102,  101,  103,  105,
    104,  106,  107,  111,  109,  108,  113,  110,
    112,  114,  115,  225,  189,  188,  203,  202,
    197,  207,  169,  223,  159,  235,  152,  192,
    179,  201,  172,  149,  178,  120,  219,  150,
    127,  211,  125,  158,  247,  238,  163,  228,
    183,  217,  168,  122,  128,  249,  187,  186,
    136,  181,  255,  230,  135,  233,  222,  145,
    134,  167,  248,  209,  243,  216,  164,  140,
    157,  239,  191,  251,  156,  139,  242,  133,
    162,  213,  165,  212,  227,  198,  236,  234,
    117,  215,  124,  123,  254,  253,  148,  218,
    146,  147,  224,  143,  184,  185,  166,  132,
    129,  250,  151,  119,  193,  176,  245,  229,
    206,  144,  208,  137,  241,  237,  190,  240,
    131,  232,  252,  171,  205,  204,  118,  214,
    180,  126,  182,  175,  141,  138,  177,  153,
    194,  160,  121,  174,  246,  130,  200,  170,
    221,  196,  142,  210,  199,  155,  154,  244,
    220,  195,  161,  231,  173,  226,  116,  255,
};

av_cold int ff_cfhd_init_vlcs(CFHDContext *s)
{
    int i, j, ret = 0;
    uint32_t new_cfhd_vlc_bits[NB_VLC_TABLE_18 * 2];
    uint8_t  new_cfhd_vlc_len[NB_VLC_TABLE_18 * 2];
    uint16_t new_cfhd_vlc_run[NB_VLC_TABLE_18 * 2];
    int16_t  new_cfhd_vlc_level[NB_VLC_TABLE_18 * 2];

    /** Similar to dv.c, generate signed VLC tables **/

    /* Table 9 */
    for (i = 0, j = 0; i < NB_VLC_TABLE_9; i++, j++) {
        new_cfhd_vlc_bits[j]  = table_9_vlc_bits[i];
        new_cfhd_vlc_len[j]   = table_9_vlc_len[i];
        new_cfhd_vlc_run[j]   = table_9_vlc_run[i];
        new_cfhd_vlc_level[j] = table_9_vlc_level[i];

        /* Don't include the zero level nor escape bits */
        if (table_9_vlc_level[i] &&
            new_cfhd_vlc_bits[j] != table_9_vlc_bits[NB_VLC_TABLE_9-1]) {
            new_cfhd_vlc_bits[j] <<= 1;
            new_cfhd_vlc_len[j]++;
            j++;
            new_cfhd_vlc_bits[j]  = (table_9_vlc_bits[i] << 1) | 1;
            new_cfhd_vlc_len[j]   =  table_9_vlc_len[i] + 1;
            new_cfhd_vlc_run[j]   =  table_9_vlc_run[i];
            new_cfhd_vlc_level[j] = -table_9_vlc_level[i];
        }
    }

    ret = init_vlc(&s->vlc_9, VLC_BITS, j, new_cfhd_vlc_len,
                   1, 1, new_cfhd_vlc_bits, 4, 4, 0);
    if (ret < 0)
        return ret;
    for (i = 0; i < s->vlc_9.table_size; i++) {
        int code = s->vlc_9.table[i][0];
        int len  = s->vlc_9.table[i][1];
        int level, run;

        if (len < 0) { // more bits needed
            run   = 0;
            level = code;
        } else {
            run   = new_cfhd_vlc_run[code];
            level = new_cfhd_vlc_level[code];
        }
        s->table_9_rl_vlc[i].len   = len;
        s->table_9_rl_vlc[i].level = level;
        s->table_9_rl_vlc[i].run   = run;
    }

    /* Table 18 */
    for (i = 0, j = 0; i < NB_VLC_TABLE_18; i++, j++) {
        new_cfhd_vlc_bits[j]  = table_18_vlc_bits[i];
        new_cfhd_vlc_len[j]   = table_18_vlc_len[i];
        new_cfhd_vlc_run[j]   = table_18_vlc_run[i];
        new_cfhd_vlc_level[j] = table_18_vlc_level[i];

        /* Don't include the zero level nor escape bits */
        if (table_18_vlc_level[i] &&
            new_cfhd_vlc_bits[j] != table_18_vlc_bits[NB_VLC_TABLE_18-1]) {
            new_cfhd_vlc_bits[j] <<= 1;
            new_cfhd_vlc_len[j]++;
            j++;
            new_cfhd_vlc_bits[j]  = (table_18_vlc_bits[i] << 1) | 1;
            new_cfhd_vlc_len[j]   =  table_18_vlc_len[i] + 1;
            new_cfhd_vlc_run[j]   =  table_18_vlc_run[i];
            new_cfhd_vlc_level[j] = -table_18_vlc_level[i];
        }
    }

    ret = init_vlc(&s->vlc_18, VLC_BITS, j, new_cfhd_vlc_len,
                   1, 1, new_cfhd_vlc_bits, 4, 4, 0);
    if (ret < 0)
        return ret;
    av_assert0(s->vlc_18.table_size == 4572);

    for (i = 0; i < s->vlc_18.table_size; i++) {
        int code = s->vlc_18.table[i][0];
        int len  = s->vlc_18.table[i][1];
        int level, run;

        if (len < 0) { // more bits needed
            run   = 0;
            level = code;
        } else {
            run   = new_cfhd_vlc_run[code];
            level = new_cfhd_vlc_level[code];
        }
        s->table_18_rl_vlc[i].len   = len;
        s->table_18_rl_vlc[i].level = level;
        s->table_18_rl_vlc[i].run   = run;
    }

    return ret;
}
