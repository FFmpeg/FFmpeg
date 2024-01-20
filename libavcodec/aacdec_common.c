/*
 * Common code and tables of the AAC fixed- and floating-point decoders
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * Common code and tables of the AAC fixed- and floating-point decoders
 */

#include "aac.h"
#include "aacdectab.h"
#include "aacps.h"
#include "aactab.h"
#include "vlc.h"

#include "libavutil/attributes.h"
#include "libavutil/thread.h"

const int8_t ff_tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 5, 5, 16, 5, 0 };

const uint8_t ff_aac_channel_layout_map[16][16][3] = {
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { 0, } },
    { { 0, } },
    { { 0, } },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    {
      { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, // SCE1 = FC,
      { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, // CPE1 = FLc and FRc,
      { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, // CPE2 = FL and FR,
      { TYPE_CPE, 2, AAC_CHANNEL_BACK  }, // CPE3 = SiL and SiR,
      { TYPE_CPE, 3, AAC_CHANNEL_BACK  }, // CPE4 = BL and BR,
      { TYPE_SCE, 1, AAC_CHANNEL_BACK  }, // SCE2 = BC,
      { TYPE_LFE, 0, AAC_CHANNEL_LFE   }, // LFE1 = LFE1,
      { TYPE_LFE, 1, AAC_CHANNEL_LFE   }, // LFE2 = LFE2,
      { TYPE_SCE, 2, AAC_CHANNEL_FRONT }, // SCE3 = TpFC,
      { TYPE_CPE, 4, AAC_CHANNEL_FRONT }, // CPE5 = TpFL and TpFR,
      { TYPE_CPE, 5, AAC_CHANNEL_SIDE  }, // CPE6 = TpSiL and TpSiR,
      { TYPE_SCE, 3, AAC_CHANNEL_SIDE  }, // SCE4 = TpC,
      { TYPE_CPE, 6, AAC_CHANNEL_BACK  }, // CPE7 = TpBL and TpBR,
      { TYPE_SCE, 4, AAC_CHANNEL_BACK  }, // SCE5 = TpBC,
      { TYPE_SCE, 5, AAC_CHANNEL_FRONT }, // SCE6 = BtFC,
      { TYPE_CPE, 7, AAC_CHANNEL_FRONT }, // CPE8 = BtFL and BtFR
    },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE }, { TYPE_CPE, 2, AAC_CHANNEL_FRONT  }, },
    { { 0, } },
};

const int16_t ff_aac_channel_map[3][4][6] = {
    {
      { AV_CHAN_FRONT_CENTER,        AV_CHAN_FRONT_LEFT_OF_CENTER, AV_CHAN_FRONT_RIGHT_OF_CENTER, AV_CHAN_FRONT_LEFT,        AV_CHAN_FRONT_RIGHT,        AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_SIDE_LEFT,            AV_CHAN_SIDE_RIGHT,            AV_CHAN_BACK_LEFT,         AV_CHAN_BACK_RIGHT,         AV_CHAN_BACK_CENTER },
      { AV_CHAN_LOW_FREQUENCY,       AV_CHAN_LOW_FREQUENCY_2,      AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
    {
      { AV_CHAN_TOP_FRONT_CENTER,    AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_FRONT_LEFT,    AV_CHAN_TOP_FRONT_RIGHT,    AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_TOP_SIDE_LEFT,        AV_CHAN_TOP_SIDE_RIGHT,        AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_TOP_CENTER},
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_BACK_LEFT,     AV_CHAN_TOP_BACK_RIGHT,     AV_CHAN_TOP_BACK_CENTER},
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE},
    },
    {
      { AV_CHAN_BOTTOM_FRONT_CENTER, AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_BOTTOM_FRONT_LEFT, AV_CHAN_BOTTOM_FRONT_RIGHT, AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
};

const AVChannelLayout ff_aac_ch_layout[] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1,
    AV_CHANNEL_LAYOUT_22POINT2,
    AV_CHANNEL_LAYOUT_7POINT1_TOP_BACK,
    { 0 },
};

VLCElem ff_vlc_scalefactors[352];
const VLCElem *ff_vlc_spectral[11];

/// Huffman tables for SBR

static const uint8_t sbr_huffman_tab[][2] = {
    /* t_huffman_env_1_5dB - 121 entries */
    {  60,   2 }, {  59,   2 }, {  61,   3 }, {  58,   3 }, {  62,   4 },
    {  57,   4 }, {  63,   5 }, {  56,   5 }, {  64,   6 }, {  55,   6 },
    {  65,   7 }, {  54,   7 }, {  66,   8 }, {  53,   8 }, {  67,   9 },
    {  52,   9 }, {  51,  10 }, {  68,  10 }, {  50,  11 }, {  69,  12 },
    {  49,  12 }, {  70,  13 }, {  48,  13 }, {  47,  13 }, {  71,  14 },
    {  46,  14 }, {  72,  14 }, {  45,  14 }, {  44,  15 }, {  73,  15 },
    {  41,  16 }, {  42,  16 }, {  43,  16 }, {  74,  16 }, {  36,  16 },
    {  40,  16 }, {  76,  16 }, {  34,  17 }, {  39,  17 }, {  75,  17 },
    {  37,  17 }, {  35,  18 }, {  38,  18 }, {   0,  18 }, {   1,  18 },
    {   2,  18 }, {   3,  18 }, {   4,  18 }, {   5,  18 }, {   6,  19 },
    {   7,  19 }, {   8,  19 }, {   9,  19 }, {  10,  19 }, {  11,  19 },
    {  12,  19 }, {  13,  19 }, {  14,  19 }, {  15,  19 }, {  16,  19 },
    {  17,  19 }, {  18,  19 }, {  19,  19 }, {  20,  19 }, {  21,  19 },
    {  22,  19 }, {  23,  19 }, {  24,  19 }, {  25,  19 }, {  26,  19 },
    {  27,  19 }, {  28,  19 }, {  29,  19 }, {  30,  19 }, {  31,  19 },
    {  32,  19 }, {  33,  19 }, {  77,  19 }, {  78,  19 }, {  79,  19 },
    {  80,  19 }, {  81,  19 }, {  82,  19 }, {  83,  19 }, {  84,  19 },
    {  85,  19 }, {  86,  19 }, {  87,  19 }, {  88,  19 }, {  89,  19 },
    {  90,  19 }, {  91,  19 }, {  92,  19 }, {  93,  19 }, {  94,  19 },
    {  95,  19 }, {  96,  19 }, {  97,  19 }, {  98,  19 }, {  99,  19 },
    { 100,  19 }, { 101,  19 }, { 102,  19 }, { 103,  19 }, { 104,  19 },
    { 105,  19 }, { 106,  19 }, { 107,  19 }, { 108,  19 }, { 109,  19 },
    { 110,  19 }, { 111,  19 }, { 112,  19 }, { 113,  19 }, { 114,  19 },
    { 115,  19 }, { 116,  19 }, { 117,  19 }, { 118,  19 }, { 119,  19 },
    { 120,  19 },
    /* f_huffman_env_1_5dB - 121 entries */
    {  60,   2 }, {  59,   2 }, {  61,   3 }, {  58,   3 }, {  57,   4 },
    {  62,   4 }, {  56,   5 }, {  63,   5 }, {  55,   6 }, {  64,   6 },
    {  54,   7 }, {  65,   8 }, {  53,   8 }, {  66,   8 }, {  52,   9 },
    {  67,   9 }, {  51,   9 }, {  68,  10 }, {  50,  10 }, {  69,  11 },
    {  49,  11 }, {  70,  11 }, {  71,  11 }, {  48,  12 }, {  72,  12 },
    {  47,  12 }, {  73,  12 }, {  74,  13 }, {  46,  13 }, {  45,  13 },
    {  75,  13 }, {  76,  14 }, {  77,  14 }, {  44,  14 }, {  43,  15 },
    {  42,  15 }, {  41,  16 }, {  78,  16 }, {  79,  16 }, {  40,  16 },
    {  39,  16 }, {  80,  17 }, {  81,  17 }, {  36,  17 }, {  37,  17 },
    {  38,  17 }, {  34,  17 }, {  32,  18 }, {  82,  18 }, {  83,  18 },
    {  85,  18 }, {  19,  18 }, {  35,  18 }, {  86,  18 }, {  87,  18 },
    {  30,  18 }, {  33,  18 }, {  84,  18 }, {  88,  18 }, { 104,  18 },
    {   9,  19 }, {  14,  19 }, {  16,  19 }, {  17,  19 }, {  23,  19 },
    {  27,  19 }, {  29,  19 }, {  31,  19 }, {  90,  19 }, {  97,  19 },
    { 102,  19 }, { 107,  19 }, { 108,  19 }, {   0,  19 }, {   1,  19 },
    {   2,  20 }, {   3,  20 }, {   4,  20 }, {   5,  20 }, {   6,  20 },
    {   7,  20 }, {   8,  20 }, {  10,  20 }, {  11,  20 }, {  12,  20 },
    {  13,  20 }, {  15,  20 }, {  18,  20 }, {  20,  20 }, {  21,  20 },
    {  22,  20 }, {  24,  20 }, {  25,  20 }, {  26,  20 }, {  28,  20 },
    {  89,  20 }, {  91,  20 }, {  92,  20 }, {  93,  20 }, {  94,  20 },
    {  95,  20 }, {  96,  20 }, {  98,  20 }, {  99,  20 }, { 100,  20 },
    { 101,  20 }, { 103,  20 }, { 105,  20 }, { 106,  20 }, { 109,  20 },
    { 110,  20 }, { 111,  20 }, { 112,  20 }, { 113,  20 }, { 114,  20 },
    { 115,  20 }, { 116,  20 }, { 117,  20 }, { 118,  20 }, { 119,  20 },
    { 120,  20 },
    /* t_huffman_env_bal_1_5dB - 49 entries */
    {  24,   1 }, {  25,   2 }, {  23,   3 }, {  26,   4 }, {  22,   5 },
    {  27,   6 }, {  21,   7 }, {  28,   8 }, {  20,   9 }, {  19,  11 },
    {  29,  11 }, {  18,  12 }, {  30,  12 }, {  31,  15 }, {  17,  16 },
    {  32,  16 }, {   0,  16 }, {   1,  16 }, {   2,  16 }, {   3,  16 },
    {   4,  16 }, {   5,  16 }, {   6,  16 }, {   7,  16 }, {   8,  16 },
    {   9,  16 }, {  10,  16 }, {  11,  16 }, {  12,  16 }, {  13,  16 },
    {  14,  16 }, {  15,  16 }, {  16,  16 }, {  33,  16 }, {  34,  16 },
    {  35,  16 }, {  36,  16 }, {  37,  16 }, {  38,  16 }, {  39,  17 },
    {  40,  17 }, {  41,  17 }, {  42,  17 }, {  43,  17 }, {  44,  17 },
    {  45,  17 }, {  46,  17 }, {  47,  17 }, {  48,  17 },
    /* f_huffman_env_bal_1_5dB - 49 entries */
    {  24,   1 }, {  23,   2 }, {  25,   3 }, {  22,   4 }, {  26,   5 },
    {  27,   6 }, {  21,   7 }, {  20,   8 }, {  28,   9 }, {  19,  11 },
    {  29,  11 }, {  18,  11 }, {  30,  12 }, {  17,  14 }, {  31,  15 },
    {  32,  16 }, {  15,  16 }, {  16,  17 }, {   0,  18 }, {   1,  18 },
    {   2,  18 }, {   3,  18 }, {   4,  18 }, {   5,  18 }, {   6,  18 },
    {   7,  18 }, {   8,  18 }, {   9,  18 }, {  10,  18 }, {  11,  18 },
    {  12,  18 }, {  13,  18 }, {  14,  18 }, {  33,  18 }, {  34,  18 },
    {  35,  18 }, {  36,  18 }, {  37,  18 }, {  38,  18 }, {  39,  18 },
    {  40,  18 }, {  41,  18 }, {  42,  18 }, {  43,  18 }, {  44,  18 },
    {  45,  18 }, {  46,  18 }, {  47,  19 }, {  48,  19 },
    /* t_huffman_env_3_0dB - 63 entries */
    {  31,   1 }, {  30,   2 }, {  32,   3 }, {  29,   4 }, {  33,   5 },
    {  28,   6 }, {  34,   7 }, {  27,   8 }, {  35,   9 }, {  26,  11 },
    {  36,  11 }, {  25,  12 }, {  24,  13 }, {  37,  13 }, {  23,  14 },
    {  38,  14 }, {  22,  14 }, {  21,  14 }, {  39,  14 }, {  40,  15 },
    {  41,  16 }, {  18,  16 }, {  20,  16 }, {  19,  16 }, {  17,  17 },
    {  42,  17 }, {  43,  18 }, {   0,  18 }, {   1,  18 }, {   2,  19 },
    {   3,  19 }, {   4,  19 }, {   5,  19 }, {   6,  19 }, {   7,  19 },
    {   8,  19 }, {   9,  19 }, {  10,  19 }, {  11,  19 }, {  12,  19 },
    {  13,  19 }, {  14,  19 }, {  15,  19 }, {  16,  19 }, {  44,  19 },
    {  45,  19 }, {  46,  19 }, {  47,  19 }, {  48,  19 }, {  49,  19 },
    {  50,  19 }, {  51,  19 }, {  52,  19 }, {  53,  19 }, {  54,  19 },
    {  55,  19 }, {  56,  19 }, {  57,  19 }, {  58,  19 }, {  59,  19 },
    {  60,  19 }, {  61,  19 }, {  62,  19 },
    /* f_huffman_env_3_0dB - 63 entries */
    {  31,   1 }, {  30,   2 }, {  32,   3 }, {  29,   4 }, {  33,   5 },
    {  28,   6 }, {  34,   8 }, {  27,   8 }, {  35,   9 }, {  26,   9 },
    {  36,  10 }, {  25,  10 }, {  37,  11 }, {  24,  11 }, {  38,  12 },
    {  23,  12 }, {  39,  13 }, {  40,  14 }, {  22,  14 }, {  21,  15 },
    {  41,  15 }, {  42,  15 }, {  20,  16 }, {  19,  16 }, {  43,  16 },
    {  44,  16 }, {  18,  17 }, {  16,  17 }, {  45,  17 }, {  46,  17 },
    {  17,  18 }, {  49,  18 }, {  13,  18 }, {   7,  18 }, {  12,  18 },
    {  47,  18 }, {  48,  18 }, {   9,  19 }, {  10,  19 }, {  15,  19 },
    {  51,  19 }, {  52,  19 }, {  53,  19 }, {  56,  19 }, {   8,  19 },
    {  11,  19 }, {  55,  19 }, {   0,  20 }, {   1,  20 }, {   2,  20 },
    {   3,  20 }, {   4,  20 }, {   5,  20 }, {   6,  20 }, {  14,  20 },
    {  50,  20 }, {  54,  20 }, {  57,  20 }, {  58,  20 }, {  59,  20 },
    {  60,  20 }, {  61,  20 }, {  62,  20 },
    /* t_huffman_env_bal_3_0dB - 25 entries */
    {  12,   1 }, {  13,   2 }, {  11,   3 }, {  10,   4 }, {  14,   5 },
    {  15,   6 }, {   9,   7 }, {   8,   8 }, {  16,   9 }, {   7,  12 },
    {   0,  13 }, {   1,  13 }, {   2,  13 }, {   3,  13 }, {   4,  13 },
    {   5,  13 }, {   6,  13 }, {  17,  13 }, {  18,  13 }, {  19,  13 },
    {  20,  13 }, {  21,  13 }, {  22,  13 }, {  23,  14 }, {  24,  14 },
    /* f_huffman_env_bal_3_0dB - 25 entries */
    {  12,   1 }, {  11,   2 }, {  13,   3 }, {  10,   4 }, {  14,   5 },
    {  15,   6 }, {   9,   7 }, {   8,   8 }, {  16,   9 }, {   7,  11 },
    {  17,  12 }, {  18,  13 }, {   0,  13 }, {   1,  13 }, {   2,  13 },
    {   3,  13 }, {   4,  13 }, {   5,  14 }, {   6,  14 }, {  19,  14 },
    {  20,  14 }, {  21,  14 }, {  22,  14 }, {  23,  14 }, {  24,  14 },
    /* t_huffman_noise_3_0dB - 63 entries */
    {  31,   1 }, {  32,   2 }, {  30,   3 }, {  29,   4 }, {  33,   5 },
    {  28,   6 }, {  34,   8 }, {  27,   8 }, {  35,  10 }, {  26,  11 },
    {  36,  13 }, {  42,  13 }, {   0,  13 }, {   1,  13 }, {   2,  13 },
    {   3,  13 }, {   4,  13 }, {   5,  13 }, {   6,  13 }, {   7,  13 },
    {   8,  13 }, {   9,  13 }, {  10,  13 }, {  11,  13 }, {  12,  13 },
    {  13,  13 }, {  14,  13 }, {  15,  13 }, {  16,  13 }, {  17,  13 },
    {  18,  13 }, {  19,  13 }, {  20,  13 }, {  21,  13 }, {  22,  13 },
    {  23,  13 }, {  24,  13 }, {  25,  13 }, {  37,  13 }, {  38,  13 },
    {  39,  13 }, {  40,  13 }, {  41,  13 }, {  43,  13 }, {  44,  13 },
    {  45,  13 }, {  46,  13 }, {  47,  13 }, {  48,  13 }, {  49,  13 },
    {  50,  13 }, {  51,  13 }, {  52,  13 }, {  53,  13 }, {  54,  13 },
    {  55,  13 }, {  56,  13 }, {  57,  13 }, {  58,  13 }, {  59,  13 },
    {  60,  13 }, {  61,  14 }, {  62,  14 },
    /* t_huffman_noise_bal_3_0dB - 25 entries */
    {  12,   1 }, {  11,   2 }, {  13,   3 }, {  10,   5 }, {  14,   6 },
    {   0,   8 }, {   1,   8 }, {   2,   8 }, {   3,   8 }, {   4,   8 },
    {   5,   8 }, {   6,   8 }, {   7,   8 }, {   8,   8 }, {   9,   8 },
    {  15,   8 }, {  16,   8 }, {  17,   8 }, {  18,   8 }, {  19,   8 },
    {  20,   8 }, {  21,   8 }, {  22,   8 }, {  23,   8 }, {  24,   8 },
};

static const uint8_t sbr_huffman_nb_codes[] = {
    121, 121, 49, 49, 63, 63, 25, 25, 63, 25
};

static const int8_t sbr_vlc_offsets[10] = {
    -60, -60, -24, -24, -31, -31, -12, -12, -31, -12
};

const VLCElem *ff_aac_sbr_vlc[10];

static av_cold void aacdec_common_init(void)
{
    static VLCElem vlc_buf[(304 + 270 + 550 + 300 + 328 +
                            294 + 306 + 268 + 510 + 366 + 462) +
                           (1098 + 1092 + 768 + 1026 + 1058 +
                            1052 +  544 + 544 +  592 + 512)];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);
    const uint8_t (*tab)[2] = sbr_huffman_tab;

    for (unsigned i = 0; i < 11; i++) {
#define TAB_WRAP_SIZE(name) name[i], sizeof(name[i][0]), sizeof(name[i][0])
        ff_vlc_spectral[i] =
            ff_vlc_init_tables_sparse(&state, 8, ff_aac_spectral_sizes[i],
                                      TAB_WRAP_SIZE(ff_aac_spectral_bits),
                                      TAB_WRAP_SIZE(ff_aac_spectral_codes),
                                      TAB_WRAP_SIZE(ff_aac_codebook_vector_idx),
                                      0);
    }

    VLC_INIT_STATIC_TABLE(ff_vlc_scalefactors, 7,
                          FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
                          ff_aac_scalefactor_bits,
                          sizeof(ff_aac_scalefactor_bits[0]),
                          sizeof(ff_aac_scalefactor_bits[0]),
                          ff_aac_scalefactor_code,
                          sizeof(ff_aac_scalefactor_code[0]),
                          sizeof(ff_aac_scalefactor_code[0]), 0);

    // SBR VLC table initialization
    for (int i = 0; i < FF_ARRAY_ELEMS(ff_aac_sbr_vlc); i++) {
        ff_aac_sbr_vlc[i] =
            ff_vlc_init_tables_from_lengths(&state, 9, sbr_huffman_nb_codes[i],
                                            &tab[0][1], 2,
                                            &tab[0][0], 2, 1,
                                            sbr_vlc_offsets[i], 0);
        tab += sbr_huffman_nb_codes[i];
    }

    ff_ps_init_common();
}

av_cold void ff_aacdec_common_init_once(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, aacdec_common_init);
}
