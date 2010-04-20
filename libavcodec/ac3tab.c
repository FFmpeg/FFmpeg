/*
 * AC-3 tables
 * copyright (c) 2001 Fabrice Bellard
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
 * tables taken directly from the AC-3 spec.
 */

#include "avcodec.h"
#include "ac3tab.h"

/**
 * Possible frame sizes.
 * from ATSC A/52 Table 5.18 Frame Size Code Table.
 */
const uint16_t ff_ac3_frame_size_tab[38][3] = {
    { 64,   69,   96   },
    { 64,   70,   96   },
    { 80,   87,   120  },
    { 80,   88,   120  },
    { 96,   104,  144  },
    { 96,   105,  144  },
    { 112,  121,  168  },
    { 112,  122,  168  },
    { 128,  139,  192  },
    { 128,  140,  192  },
    { 160,  174,  240  },
    { 160,  175,  240  },
    { 192,  208,  288  },
    { 192,  209,  288  },
    { 224,  243,  336  },
    { 224,  244,  336  },
    { 256,  278,  384  },
    { 256,  279,  384  },
    { 320,  348,  480  },
    { 320,  349,  480  },
    { 384,  417,  576  },
    { 384,  418,  576  },
    { 448,  487,  672  },
    { 448,  488,  672  },
    { 512,  557,  768  },
    { 512,  558,  768  },
    { 640,  696,  960  },
    { 640,  697,  960  },
    { 768,  835,  1152 },
    { 768,  836,  1152 },
    { 896,  975,  1344 },
    { 896,  976,  1344 },
    { 1024, 1114, 1536 },
    { 1024, 1115, 1536 },
    { 1152, 1253, 1728 },
    { 1152, 1254, 1728 },
    { 1280, 1393, 1920 },
    { 1280, 1394, 1920 },
};

/**
 * Maps audio coding mode (acmod) to number of full-bandwidth channels.
 * from ATSC A/52 Table 5.8 Audio Coding Mode
 */
const uint8_t ff_ac3_channels_tab[8] = {
    2, 1, 2, 3, 3, 4, 4, 5
};

/**
 * Maps audio coding mode (acmod) to channel layout mask.
 */
const uint16_t ff_ac3_channel_layout_tab[8] = {
    CH_LAYOUT_STEREO,
    CH_LAYOUT_MONO,
    CH_LAYOUT_STEREO,
    CH_LAYOUT_SURROUND,
    CH_LAYOUT_2_1,
    CH_LAYOUT_4POINT0,
    CH_LAYOUT_2_2,
    CH_LAYOUT_5POINT0
};

#define COMMON_CHANNEL_MAP \
    { { 0, 1,          }, { 0, 1, 2,         } },\
    { { 0,             }, { 0, 1,            } },\
    { { 0, 1,          }, { 0, 1, 2,         } },\
    { { 0, 2, 1,       }, { 0, 2, 1, 3,      } },\
    { { 0, 1, 2,       }, { 0, 1, 3, 2,      } },\
    { { 0, 2, 1, 3,    }, { 0, 2, 1, 4, 3,   } },

/**
 * Table to remap channels from SMPTE order to AC-3 order.
 * [channel_mode][lfe][ch]
 */
const uint8_t ff_ac3_enc_channel_map[8][2][6] = {
    COMMON_CHANNEL_MAP
    { { 0, 1, 2, 3,    }, { 0, 1, 3, 4, 2,   } },
    { { 0, 2, 1, 3, 4, }, { 0, 2, 1, 4, 5, 3 } },
};

/**
 * Table to remap channels from from AC-3 order to SMPTE order.
 * [channel_mode][lfe][ch]
 */
const uint8_t ff_ac3_dec_channel_map[8][2][6] = {
    COMMON_CHANNEL_MAP
    { { 0, 1, 2, 3,    }, { 0, 1, 4, 2, 3,   } },
    { { 0, 2, 1, 3, 4, }, { 0, 2, 1, 5, 3, 4 } },
};

/* possible frequencies */
const uint16_t ff_ac3_sample_rate_tab[3] = { 48000, 44100, 32000 };

/* possible bitrates */
const uint16_t ff_ac3_bitrate_tab[19] = {
    32, 40, 48, 56, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512, 576, 640
};

/* AC-3 MDCT window */

/* MDCT window */
const int16_t ff_ac3_window[256] = {
    4,    7,   12,   16,   21,   28,   34,   42,
   51,   61,   72,   84,   97,  111,  127,  145,
  164,  184,  207,  231,  257,  285,  315,  347,
  382,  419,  458,  500,  544,  591,  641,  694,
  750,  810,  872,  937, 1007, 1079, 1155, 1235,
 1318, 1406, 1497, 1593, 1692, 1796, 1903, 2016,
 2132, 2253, 2379, 2509, 2644, 2783, 2927, 3076,
 3230, 3389, 3552, 3721, 3894, 4072, 4255, 4444,
 4637, 4835, 5038, 5246, 5459, 5677, 5899, 6127,
 6359, 6596, 6837, 7083, 7334, 7589, 7848, 8112,
 8380, 8652, 8927, 9207, 9491, 9778,10069,10363,
10660,10960,11264,11570,11879,12190,12504,12820,
13138,13458,13780,14103,14427,14753,15079,15407,
15735,16063,16392,16720,17049,17377,17705,18032,
18358,18683,19007,19330,19651,19970,20287,20602,
20914,21225,21532,21837,22139,22438,22733,23025,
23314,23599,23880,24157,24430,24699,24964,25225,
25481,25732,25979,26221,26459,26691,26919,27142,
27359,27572,27780,27983,28180,28373,28560,28742,
28919,29091,29258,29420,29577,29729,29876,30018,
30155,30288,30415,30538,30657,30771,30880,30985,
31086,31182,31274,31363,31447,31528,31605,31678,
31747,31814,31877,31936,31993,32046,32097,32145,
32190,32232,32272,32310,32345,32378,32409,32438,
32465,32490,32513,32535,32556,32574,32592,32608,
32623,32636,32649,32661,32671,32681,32690,32698,
32705,32712,32718,32724,32729,32733,32737,32741,
32744,32747,32750,32752,32754,32756,32757,32759,
32760,32761,32762,32763,32764,32764,32765,32765,
32766,32766,32766,32766,32767,32767,32767,32767,
32767,32767,32767,32767,32767,32767,32767,32767,
32767,32767,32767,32767,32767,32767,32767,32767,
};

const uint8_t ff_ac3_log_add_tab[260]= {
0x40,0x3f,0x3e,0x3d,0x3c,0x3b,0x3a,0x39,0x38,0x37,
0x36,0x35,0x34,0x34,0x33,0x32,0x31,0x30,0x2f,0x2f,
0x2e,0x2d,0x2c,0x2c,0x2b,0x2a,0x29,0x29,0x28,0x27,
0x26,0x26,0x25,0x24,0x24,0x23,0x23,0x22,0x21,0x21,
0x20,0x20,0x1f,0x1e,0x1e,0x1d,0x1d,0x1c,0x1c,0x1b,
0x1b,0x1a,0x1a,0x19,0x19,0x18,0x18,0x17,0x17,0x16,
0x16,0x15,0x15,0x15,0x14,0x14,0x13,0x13,0x13,0x12,
0x12,0x12,0x11,0x11,0x11,0x10,0x10,0x10,0x0f,0x0f,
0x0f,0x0e,0x0e,0x0e,0x0d,0x0d,0x0d,0x0d,0x0c,0x0c,
0x0c,0x0c,0x0b,0x0b,0x0b,0x0b,0x0a,0x0a,0x0a,0x0a,
0x0a,0x09,0x09,0x09,0x09,0x09,0x08,0x08,0x08,0x08,
0x08,0x08,0x07,0x07,0x07,0x07,0x07,0x07,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x05,0x05,0x05,0x05,
0x05,0x05,0x05,0x05,0x04,0x04,0x04,0x04,0x04,0x04,
0x04,0x04,0x04,0x04,0x04,0x03,0x03,0x03,0x03,0x03,
0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x02,
0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

const uint16_t ff_ac3_hearing_threshold_tab[50][3]= {
{ 0x04d0,0x04f0,0x0580 },
{ 0x04d0,0x04f0,0x0580 },
{ 0x0440,0x0460,0x04b0 },
{ 0x0400,0x0410,0x0450 },
{ 0x03e0,0x03e0,0x0420 },
{ 0x03c0,0x03d0,0x03f0 },
{ 0x03b0,0x03c0,0x03e0 },
{ 0x03b0,0x03b0,0x03d0 },
{ 0x03a0,0x03b0,0x03c0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03a0 },
{ 0x0390,0x03a0,0x03a0 },
{ 0x0390,0x0390,0x03a0 },
{ 0x0390,0x0390,0x03a0 },
{ 0x0380,0x0390,0x03a0 },
{ 0x0380,0x0380,0x03a0 },
{ 0x0370,0x0380,0x03a0 },
{ 0x0370,0x0380,0x03a0 },
{ 0x0360,0x0370,0x0390 },
{ 0x0360,0x0370,0x0390 },
{ 0x0350,0x0360,0x0390 },
{ 0x0350,0x0360,0x0390 },
{ 0x0340,0x0350,0x0380 },
{ 0x0340,0x0350,0x0380 },
{ 0x0330,0x0340,0x0380 },
{ 0x0320,0x0340,0x0370 },
{ 0x0310,0x0320,0x0360 },
{ 0x0300,0x0310,0x0350 },
{ 0x02f0,0x0300,0x0340 },
{ 0x02f0,0x02f0,0x0330 },
{ 0x02f0,0x02f0,0x0320 },
{ 0x02f0,0x02f0,0x0310 },
{ 0x0300,0x02f0,0x0300 },
{ 0x0310,0x0300,0x02f0 },
{ 0x0340,0x0320,0x02f0 },
{ 0x0390,0x0350,0x02f0 },
{ 0x03e0,0x0390,0x0300 },
{ 0x0420,0x03e0,0x0310 },
{ 0x0460,0x0420,0x0330 },
{ 0x0490,0x0450,0x0350 },
{ 0x04a0,0x04a0,0x03c0 },
{ 0x0460,0x0490,0x0410 },
{ 0x0440,0x0460,0x0470 },
{ 0x0440,0x0440,0x04a0 },
{ 0x0520,0x0480,0x0460 },
{ 0x0800,0x0630,0x0440 },
{ 0x0840,0x0840,0x0450 },
{ 0x0840,0x0840,0x04e0 },
};

const uint8_t ff_ac3_bap_tab[64]= {
    0, 1, 1, 1, 1, 1, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 6, 6, 6, 7,
    7, 7, 7, 8, 8, 8, 8, 9, 9, 9,
    9, 10, 10, 10, 10, 11, 11, 11, 11, 12,
    12, 12, 12, 13, 13, 13, 13, 14, 14, 14,
    14, 14, 14, 14, 14, 15, 15, 15, 15, 15,
    15, 15, 15, 15,
};

const uint8_t ff_ac3_slow_decay_tab[4]={
    0x0f, 0x11, 0x13, 0x15,
};

const uint8_t ff_ac3_fast_decay_tab[4]={
    0x3f, 0x53, 0x67, 0x7b,
};

const uint16_t ff_ac3_slow_gain_tab[4]= {
    0x540, 0x4d8, 0x478, 0x410,
};

const uint16_t ff_ac3_db_per_bit_tab[4]= {
    0x000, 0x700, 0x900, 0xb00,
};

const int16_t ff_ac3_floor_tab[8]= {
    0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, 0xf800,
};

const uint16_t ff_ac3_fast_gain_tab[8]= {
    0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400,
};

const uint8_t ff_ac3_critical_band_size_tab[50]={
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3,
    3, 6, 6, 6, 6, 6, 6, 12, 12, 12, 12, 24, 24, 24, 24, 24
};
/**
 * Default channel map for a dependent substream defined by acmod
 */
const uint16_t ff_eac3_default_chmap[8] = {
    AC3_CHMAP_L |               AC3_CHMAP_R, // FIXME Ch1+Ch2
                  AC3_CHMAP_C,
    AC3_CHMAP_L |               AC3_CHMAP_R,
    AC3_CHMAP_L | AC3_CHMAP_C | AC3_CHMAP_R,
    AC3_CHMAP_L |               AC3_CHMAP_R |                   AC3_CHMAP_C_SUR,
    AC3_CHMAP_L | AC3_CHMAP_C | AC3_CHMAP_R |                   AC3_CHMAP_C_SUR,
    AC3_CHMAP_L |               AC3_CHMAP_R | AC3_CHMAP_L_SUR |                  AC3_CHMAP_R_SUR,
    AC3_CHMAP_L | AC3_CHMAP_C | AC3_CHMAP_R | AC3_CHMAP_L_SUR |                  AC3_CHMAP_R_SUR
};
