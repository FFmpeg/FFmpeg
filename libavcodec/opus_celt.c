/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * Opus CELT decoder
 */

#include <stdint.h>

#include "libavutil/float_dsp.h"
#include "libavutil/libm.h"

#include "imdct15.h"
#include "opus.h"

enum CeltSpread {
    CELT_SPREAD_NONE,
    CELT_SPREAD_LIGHT,
    CELT_SPREAD_NORMAL,
    CELT_SPREAD_AGGRESSIVE
};

typedef struct CeltFrame {
    float energy[CELT_MAX_BANDS];
    float prev_energy[2][CELT_MAX_BANDS];

    uint8_t collapse_masks[CELT_MAX_BANDS];

    /* buffer for mdct output + postfilter */
    DECLARE_ALIGNED(32, float, buf)[2048];

    /* postfilter parameters */
    int pf_period_new;
    float pf_gains_new[3];
    int pf_period;
    float pf_gains[3];
    int pf_period_old;
    float pf_gains_old[3];

    float deemph_coeff;
} CeltFrame;

struct CeltContext {
    // constant values that do not change during context lifetime
    AVCodecContext    *avctx;
    IMDCT15Context    *imdct[4];
    AVFloatDSPContext  *dsp;
    int output_channels;

    // values that have inter-frame effect and must be reset on flush
    CeltFrame frame[2];
    uint32_t seed;
    int flushed;

    // values that only affect a single frame
    int coded_channels;
    int framebits;
    int duration;

    /* number of iMDCT blocks in the frame */
    int blocks;
    /* size of each block */
    int blocksize;

    int startband;
    int endband;
    int codedbands;

    int anticollapse_bit;

    int intensitystereo;
    int dualstereo;
    enum CeltSpread spread;

    int remaining;
    int remaining2;
    int fine_bits    [CELT_MAX_BANDS];
    int fine_priority[CELT_MAX_BANDS];
    int pulses       [CELT_MAX_BANDS];
    int tf_change    [CELT_MAX_BANDS];

    DECLARE_ALIGNED(32, float, coeffs)[2][CELT_MAX_FRAME_SIZE];
    DECLARE_ALIGNED(32, float, scratch)[22 * 8]; // MAX(celt_freq_range) * 1<<CELT_MAX_LOG_BLOCKS
};

static const uint16_t celt_model_tapset[] = { 4, 2, 3, 4 };

static const uint16_t celt_model_spread[] = { 32, 7, 9, 30, 32 };

static const uint16_t celt_model_alloc_trim[] = {
    128,   2,   4,   9,  19,  41,  87, 109, 119, 124, 126, 128
};

static const uint16_t celt_model_energy_small[] = { 4, 2, 3, 4 };

static const uint8_t celt_freq_bands[] = { /* in steps of 200Hz */
    0,  1,  2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100
};

static const uint8_t celt_freq_range[] = {
    1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  6,  6,  8, 12, 18, 22
};

static const uint8_t celt_log_freq_range[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8, 16, 16, 16, 21, 21, 24, 29, 34, 36
};

static const int8_t celt_tf_select[4][2][2][2] = {
    { { { 0, -1 }, { 0, -1 } }, { { 0, -1 }, { 0, -1 } } },
    { { { 0, -1 }, { 0, -2 } }, { { 1,  0 }, { 1, -1 } } },
    { { { 0, -2 }, { 0, -3 } }, { { 2,  0 }, { 1, -1 } } },
    { { { 0, -2 }, { 0, -3 } }, { { 3,  0 }, { 1, -1 } } }
};

static const float celt_mean_energy[] = {
    6.437500f, 6.250000f, 5.750000f, 5.312500f, 5.062500f,
    4.812500f, 4.500000f, 4.375000f, 4.875000f, 4.687500f,
    4.562500f, 4.437500f, 4.875000f, 4.625000f, 4.312500f,
    4.500000f, 4.375000f, 4.625000f, 4.750000f, 4.437500f,
    3.750000f, 3.750000f, 3.750000f, 3.750000f, 3.750000f
};

static const float celt_alpha_coef[] = {
    29440.0f/32768.0f,    26112.0f/32768.0f,    21248.0f/32768.0f,    16384.0f/32768.0f
};

static const float celt_beta_coef[] = { /* TODO: precompute 1 minus this if the code ends up neater */
    30147.0f/32768.0f,    22282.0f/32768.0f,    12124.0f/32768.0f,     6554.0f/32768.0f
};

static const uint8_t celt_coarse_energy_dist[4][2][42] = {
    {
        {       // 120-sample inter
             72, 127,  65, 129,  66, 128,  65, 128,  64, 128,  62, 128,  64, 128,
             64, 128,  92,  78,  92,  79,  92,  78,  90,  79, 116,  41, 115,  40,
            114,  40, 132,  26, 132,  26, 145,  17, 161,  12, 176,  10, 177,  11
        }, {    // 120-sample intra
             24, 179,  48, 138,  54, 135,  54, 132,  53, 134,  56, 133,  55, 132,
             55, 132,  61, 114,  70,  96,  74,  88,  75,  88,  87,  74,  89,  66,
             91,  67, 100,  59, 108,  50, 120,  40, 122,  37,  97,  43,  78,  50
        }
    }, {
        {       // 240-sample inter
             83,  78,  84,  81,  88,  75,  86,  74,  87,  71,  90,  73,  93,  74,
             93,  74, 109,  40, 114,  36, 117,  34, 117,  34, 143,  17, 145,  18,
            146,  19, 162,  12, 165,  10, 178,   7, 189,   6, 190,   8, 177,   9
        }, {    // 240-sample intra
             23, 178,  54, 115,  63, 102,  66,  98,  69,  99,  74,  89,  71,  91,
             73,  91,  78,  89,  86,  80,  92,  66,  93,  64, 102,  59, 103,  60,
            104,  60, 117,  52, 123,  44, 138,  35, 133,  31,  97,  38,  77,  45
        }
    }, {
        {       // 480-sample inter
             61,  90,  93,  60, 105,  42, 107,  41, 110,  45, 116,  38, 113,  38,
            112,  38, 124,  26, 132,  27, 136,  19, 140,  20, 155,  14, 159,  16,
            158,  18, 170,  13, 177,  10, 187,   8, 192,   6, 175,   9, 159,  10
        }, {    // 480-sample intra
             21, 178,  59, 110,  71,  86,  75,  85,  84,  83,  91,  66,  88,  73,
             87,  72,  92,  75,  98,  72, 105,  58, 107,  54, 115,  52, 114,  55,
            112,  56, 129,  51, 132,  40, 150,  33, 140,  29,  98,  35,  77,  42
        }
    }, {
        {       // 960-sample inter
             42, 121,  96,  66, 108,  43, 111,  40, 117,  44, 123,  32, 120,  36,
            119,  33, 127,  33, 134,  34, 139,  21, 147,  23, 152,  20, 158,  25,
            154,  26, 166,  21, 173,  16, 184,  13, 184,  10, 150,  13, 139,  15
        }, {    // 960-sample intra
             22, 178,  63, 114,  74,  82,  84,  83,  92,  82, 103,  62,  96,  72,
             96,  67, 101,  73, 107,  72, 113,  55, 118,  52, 125,  52, 118,  52,
            117,  55, 135,  49, 137,  39, 157,  32, 145,  29,  97,  33,  77,  40
        }
    }
};

static const uint8_t celt_static_alloc[11][21] = {  /* 1/32 bit/sample */
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  90,  80,  75,  69,  63,  56,  49,  40,  34,  29,  20,  18,  10,   0,   0,   0,   0,   0,   0,   0,   0 },
    { 110, 100,  90,  84,  78,  71,  65,  58,  51,  45,  39,  32,  26,  20,  12,   0,   0,   0,   0,   0,   0 },
    { 118, 110, 103,  93,  86,  80,  75,  70,  65,  59,  53,  47,  40,  31,  23,  15,   4,   0,   0,   0,   0 },
    { 126, 119, 112, 104,  95,  89,  83,  78,  72,  66,  60,  54,  47,  39,  32,  25,  17,  12,   1,   0,   0 },
    { 134, 127, 120, 114, 103,  97,  91,  85,  78,  72,  66,  60,  54,  47,  41,  35,  29,  23,  16,  10,   1 },
    { 144, 137, 130, 124, 113, 107, 101,  95,  88,  82,  76,  70,  64,  57,  51,  45,  39,  33,  26,  15,   1 },
    { 152, 145, 138, 132, 123, 117, 111, 105,  98,  92,  86,  80,  74,  67,  61,  55,  49,  43,  36,  20,   1 },
    { 162, 155, 148, 142, 133, 127, 121, 115, 108, 102,  96,  90,  84,  77,  71,  65,  59,  53,  46,  30,   1 },
    { 172, 165, 158, 152, 143, 137, 131, 125, 118, 112, 106, 100,  94,  87,  81,  75,  69,  63,  56,  45,  20 },
    { 200, 200, 200, 200, 200, 200, 200, 200, 198, 193, 188, 183, 178, 173, 168, 163, 158, 153, 148, 129, 104 }
};

static const uint8_t celt_static_caps[4][2][21] = {
    {       // 120-sample
        {224, 224, 224, 224, 224, 224, 224, 224, 160, 160,
         160, 160, 185, 185, 185, 178, 178, 168, 134,  61,  37},
        {224, 224, 224, 224, 224, 224, 224, 224, 240, 240,
         240, 240, 207, 207, 207, 198, 198, 183, 144,  66,  40},
    }, {    // 240-sample
        {160, 160, 160, 160, 160, 160, 160, 160, 185, 185,
         185, 185, 193, 193, 193, 183, 183, 172, 138,  64,  38},
        {240, 240, 240, 240, 240, 240, 240, 240, 207, 207,
         207, 207, 204, 204, 204, 193, 193, 180, 143,  66,  40},
    }, {    // 480-sample
        {185, 185, 185, 185, 185, 185, 185, 185, 193, 193,
         193, 193, 193, 193, 193, 183, 183, 172, 138,  65,  39},
        {207, 207, 207, 207, 207, 207, 207, 207, 204, 204,
         204, 204, 201, 201, 201, 188, 188, 176, 141,  66,  40},
    }, {    // 960-sample
        {193, 193, 193, 193, 193, 193, 193, 193, 193, 193,
         193, 193, 194, 194, 194, 184, 184, 173, 139,  65,  39},
        {204, 204, 204, 204, 204, 204, 204, 204, 201, 201,
         201, 201, 198, 198, 198, 187, 187, 175, 140,  66,  40}
    }
};

static const uint8_t celt_cache_bits[392] = {
    40, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 40, 15, 23, 28,
    31, 34, 36, 38, 39, 41, 42, 43, 44, 45, 46, 47, 47, 49, 50,
    51, 52, 53, 54, 55, 55, 57, 58, 59, 60, 61, 62, 63, 63, 65,
    66, 67, 68, 69, 70, 71, 71, 40, 20, 33, 41, 48, 53, 57, 61,
    64, 66, 69, 71, 73, 75, 76, 78, 80, 82, 85, 87, 89, 91, 92,
    94, 96, 98, 101, 103, 105, 107, 108, 110, 112, 114, 117, 119, 121, 123,
    124, 126, 128, 40, 23, 39, 51, 60, 67, 73, 79, 83, 87, 91, 94,
    97, 100, 102, 105, 107, 111, 115, 118, 121, 124, 126, 129, 131, 135, 139,
    142, 145, 148, 150, 153, 155, 159, 163, 166, 169, 172, 174, 177, 179, 35,
    28, 49, 65, 78, 89, 99, 107, 114, 120, 126, 132, 136, 141, 145, 149,
    153, 159, 165, 171, 176, 180, 185, 189, 192, 199, 205, 211, 216, 220, 225,
    229, 232, 239, 245, 251, 21, 33, 58, 79, 97, 112, 125, 137, 148, 157,
    166, 174, 182, 189, 195, 201, 207, 217, 227, 235, 243, 251, 17, 35, 63,
    86, 106, 123, 139, 152, 165, 177, 187, 197, 206, 214, 222, 230, 237, 250,
    25, 31, 55, 75, 91, 105, 117, 128, 138, 146, 154, 161, 168, 174, 180,
    185, 190, 200, 208, 215, 222, 229, 235, 240, 245, 255, 16, 36, 65, 89,
    110, 128, 144, 159, 173, 185, 196, 207, 217, 226, 234, 242, 250, 11, 41,
    74, 103, 128, 151, 172, 191, 209, 225, 241, 255, 9, 43, 79, 110, 138,
    163, 186, 207, 227, 246, 12, 39, 71, 99, 123, 144, 164, 182, 198, 214,
    228, 241, 253, 9, 44, 81, 113, 142, 168, 192, 214, 235, 255, 7, 49,
    90, 127, 160, 191, 220, 247, 6, 51, 95, 134, 170, 203, 234, 7, 47,
    87, 123, 155, 184, 212, 237, 6, 52, 97, 137, 174, 208, 240, 5, 57,
    106, 151, 192, 231, 5, 59, 111, 158, 202, 243, 5, 55, 103, 147, 187,
    224, 5, 60, 113, 161, 206, 248, 4, 65, 122, 175, 224, 4, 67, 127,
    182, 234
};

static const int16_t celt_cache_index[105] = {
    -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 41, 41, 41,
    82, 82, 123, 164, 200, 222, 0, 0, 0, 0, 0, 0, 0, 0, 41,
    41, 41, 41, 123, 123, 123, 164, 164, 240, 266, 283, 295, 41, 41, 41,
    41, 41, 41, 41, 41, 123, 123, 123, 123, 240, 240, 240, 266, 266, 305,
    318, 328, 336, 123, 123, 123, 123, 123, 123, 123, 123, 240, 240, 240, 240,
    305, 305, 305, 318, 318, 343, 351, 358, 364, 240, 240, 240, 240, 240, 240,
    240, 240, 305, 305, 305, 305, 343, 343, 343, 351, 351, 370, 376, 382, 387,
};

static const uint8_t celt_log2_frac[] = {
    0, 8, 13, 16, 19, 21, 23, 24, 26, 27, 28, 29, 30, 31, 32, 32, 33, 34, 34, 35, 36, 36, 37, 37
};

static const uint8_t celt_bit_interleave[] = {
    0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3
};

static const uint8_t celt_bit_deinterleave[] = {
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

static const uint8_t celt_hadamard_ordery[] = {
    1,   0,
    3,   0,  2,  1,
    7,   0,  4,  3,  6,  1,  5,  2,
    15,  0,  8,  7, 12,  3, 11,  4, 14,  1,  9,  6, 13,  2, 10,  5
};

static const uint16_t celt_qn_exp2[] = {
    16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048
};

static const uint32_t celt_pvq_u[1272] = {
    /* N = 0, K = 0...176 */
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* N = 1, K = 1...176 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* N = 2, K = 2...176 */
    3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41,
    43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79,
    81, 83, 85, 87, 89, 91, 93, 95, 97, 99, 101, 103, 105, 107, 109, 111, 113,
    115, 117, 119, 121, 123, 125, 127, 129, 131, 133, 135, 137, 139, 141, 143,
    145, 147, 149, 151, 153, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173,
    175, 177, 179, 181, 183, 185, 187, 189, 191, 193, 195, 197, 199, 201, 203,
    205, 207, 209, 211, 213, 215, 217, 219, 221, 223, 225, 227, 229, 231, 233,
    235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255, 257, 259, 261, 263,
    265, 267, 269, 271, 273, 275, 277, 279, 281, 283, 285, 287, 289, 291, 293,
    295, 297, 299, 301, 303, 305, 307, 309, 311, 313, 315, 317, 319, 321, 323,
    325, 327, 329, 331, 333, 335, 337, 339, 341, 343, 345, 347, 349, 351,
    /* N = 3, K = 3...176 */
    13, 25, 41, 61, 85, 113, 145, 181, 221, 265, 313, 365, 421, 481, 545, 613,
    685, 761, 841, 925, 1013, 1105, 1201, 1301, 1405, 1513, 1625, 1741, 1861,
    1985, 2113, 2245, 2381, 2521, 2665, 2813, 2965, 3121, 3281, 3445, 3613, 3785,
    3961, 4141, 4325, 4513, 4705, 4901, 5101, 5305, 5513, 5725, 5941, 6161, 6385,
    6613, 6845, 7081, 7321, 7565, 7813, 8065, 8321, 8581, 8845, 9113, 9385, 9661,
    9941, 10225, 10513, 10805, 11101, 11401, 11705, 12013, 12325, 12641, 12961,
    13285, 13613, 13945, 14281, 14621, 14965, 15313, 15665, 16021, 16381, 16745,
    17113, 17485, 17861, 18241, 18625, 19013, 19405, 19801, 20201, 20605, 21013,
    21425, 21841, 22261, 22685, 23113, 23545, 23981, 24421, 24865, 25313, 25765,
    26221, 26681, 27145, 27613, 28085, 28561, 29041, 29525, 30013, 30505, 31001,
    31501, 32005, 32513, 33025, 33541, 34061, 34585, 35113, 35645, 36181, 36721,
    37265, 37813, 38365, 38921, 39481, 40045, 40613, 41185, 41761, 42341, 42925,
    43513, 44105, 44701, 45301, 45905, 46513, 47125, 47741, 48361, 48985, 49613,
    50245, 50881, 51521, 52165, 52813, 53465, 54121, 54781, 55445, 56113, 56785,
    57461, 58141, 58825, 59513, 60205, 60901, 61601,
    /* N = 4, K = 4...176 */
    63, 129, 231, 377, 575, 833, 1159, 1561, 2047, 2625, 3303, 4089, 4991, 6017,
    7175, 8473, 9919, 11521, 13287, 15225, 17343, 19649, 22151, 24857, 27775,
    30913, 34279, 37881, 41727, 45825, 50183, 54809, 59711, 64897, 70375, 76153,
    82239, 88641, 95367, 102425, 109823, 117569, 125671, 134137, 142975, 152193,
    161799, 171801, 182207, 193025, 204263, 215929, 228031, 240577, 253575,
    267033, 280959, 295361, 310247, 325625, 341503, 357889, 374791, 392217,
    410175, 428673, 447719, 467321, 487487, 508225, 529543, 551449, 573951,
    597057, 620775, 645113, 670079, 695681, 721927, 748825, 776383, 804609,
    833511, 863097, 893375, 924353, 956039, 988441, 1021567, 1055425, 1090023,
    1125369, 1161471, 1198337, 1235975, 1274393, 1313599, 1353601, 1394407,
    1436025, 1478463, 1521729, 1565831, 1610777, 1656575, 1703233, 1750759,
    1799161, 1848447, 1898625, 1949703, 2001689, 2054591, 2108417, 2163175,
    2218873, 2275519, 2333121, 2391687, 2451225, 2511743, 2573249, 2635751,
    2699257, 2763775, 2829313, 2895879, 2963481, 3032127, 3101825, 3172583,
    3244409, 3317311, 3391297, 3466375, 3542553, 3619839, 3698241, 3777767,
    3858425, 3940223, 4023169, 4107271, 4192537, 4278975, 4366593, 4455399,
    4545401, 4636607, 4729025, 4822663, 4917529, 5013631, 5110977, 5209575,
    5309433, 5410559, 5512961, 5616647, 5721625, 5827903, 5935489, 6044391,
    6154617, 6266175, 6379073, 6493319, 6608921, 6725887, 6844225, 6963943,
    7085049, 7207551,
    /* N = 5, K = 5...176 */
    321, 681, 1289, 2241, 3649, 5641, 8361, 11969, 16641, 22569, 29961, 39041,
    50049, 63241, 78889, 97281, 118721, 143529, 172041, 204609, 241601, 283401,
    330409, 383041, 441729, 506921, 579081, 658689, 746241, 842249, 947241,
    1061761, 1186369, 1321641, 1468169, 1626561, 1797441, 1981449, 2179241,
    2391489, 2618881, 2862121, 3121929, 3399041, 3694209, 4008201, 4341801,
    4695809, 5071041, 5468329, 5888521, 6332481, 6801089, 7295241, 7815849,
    8363841, 8940161, 9545769, 10181641, 10848769, 11548161, 12280841, 13047849,
    13850241, 14689089, 15565481, 16480521, 17435329, 18431041, 19468809,
    20549801, 21675201, 22846209, 24064041, 25329929, 26645121, 28010881,
    29428489, 30899241, 32424449, 34005441, 35643561, 37340169, 39096641,
    40914369, 42794761, 44739241, 46749249, 48826241, 50971689, 53187081,
    55473921, 57833729, 60268041, 62778409, 65366401, 68033601, 70781609,
    73612041, 76526529, 79526721, 82614281, 85790889, 89058241, 92418049,
    95872041, 99421961, 103069569, 106816641, 110664969, 114616361, 118672641,
    122835649, 127107241, 131489289, 135983681, 140592321, 145317129, 150160041,
    155123009, 160208001, 165417001, 170752009, 176215041, 181808129, 187533321,
    193392681, 199388289, 205522241, 211796649, 218213641, 224775361, 231483969,
    238341641, 245350569, 252512961, 259831041, 267307049, 274943241, 282741889,
    290705281, 298835721, 307135529, 315607041, 324252609, 333074601, 342075401,
    351257409, 360623041, 370174729, 379914921, 389846081, 399970689, 410291241,
    420810249, 431530241, 442453761, 453583369, 464921641, 476471169, 488234561,
    500214441, 512413449, 524834241, 537479489, 550351881, 563454121, 576788929,
    590359041, 604167209, 618216201, 632508801,
    /* N = 6, K = 6...96 (technically V(109,5) fits in 32 bits, but that can't be
     achieved by splitting an Opus band) */
    1683, 3653, 7183, 13073, 22363, 36365, 56695, 85305, 124515, 177045, 246047,
    335137, 448427, 590557, 766727, 982729, 1244979, 1560549, 1937199, 2383409,
    2908411, 3522221, 4235671, 5060441, 6009091, 7095093, 8332863, 9737793,
    11326283, 13115773, 15124775, 17372905, 19880915, 22670725, 25765455,
    29189457, 32968347, 37129037, 41699767, 46710137, 52191139, 58175189,
    64696159, 71789409, 79491819, 87841821, 96879431, 106646281, 117185651,
    128542501, 140763503, 153897073, 167993403, 183104493, 199284183, 216588185,
    235074115, 254801525, 275831935, 298228865, 322057867, 347386557, 374284647,
    402823977, 433078547, 465124549, 499040399, 534906769, 572806619, 612825229,
    655050231, 699571641, 746481891, 795875861, 847850911, 902506913, 959946283,
    1020274013, 1083597703, 1150027593, 1219676595, 1292660325, 1369097135,
    1449108145, 1532817275, 1620351277, 1711839767, 1807415257, 1907213187,
    2011371957, 2120032959,
    /* N = 7, K = 7...54 (technically V(60,6) fits in 32 bits, but that can't be
     achieved by splitting an Opus band) */
    8989, 19825, 40081, 75517, 134245, 227305, 369305, 579125, 880685, 1303777,
    1884961, 2668525, 3707509, 5064793, 6814249, 9041957, 11847485, 15345233,
    19665841, 24957661, 31388293, 39146185, 48442297, 59511829, 72616013,
    88043969, 106114625, 127178701, 151620757, 179861305, 212358985, 249612805,
    292164445, 340600625, 395555537, 457713341, 527810725, 606639529, 695049433,
    793950709, 904317037, 1027188385, 1163673953, 1314955181, 1482288821,
    1667010073, 1870535785, 2094367717,
    /* N = 8, K = 8...37 (technically V(40,7) fits in 32 bits, but that can't be
     achieved by splitting an Opus band) */
    48639, 108545, 224143, 433905, 795455, 1392065, 2340495, 3800305, 5984767,
    9173505, 13726991, 20103025, 28875327, 40754369, 56610575, 77500017,
    104692735, 139703809, 184327311, 240673265, 311207743, 398796225, 506750351,
    638878193, 799538175, 993696769, 1226990095, 1505789553, 1837271615,
    2229491905,
    /* N = 9, K = 9...28 (technically V(29,8) fits in 32 bits, but that can't be
     achieved by splitting an Opus band) */
    265729, 598417, 1256465, 2485825, 4673345, 8405905, 14546705, 24331777,
    39490049, 62390545, 96220561, 145198913, 214828609, 312193553, 446304145,
    628496897, 872893441, 1196924561, 1621925137, 2173806145,
    /* N = 10, K = 10...24 */
    1462563, 3317445, 7059735, 14218905, 27298155, 50250765, 89129247, 152951073,
    254831667, 413442773, 654862247, 1014889769, 1541911931, 2300409629,
    3375210671,
    /* N = 11, K = 11...19 (technically V(20,10) fits in 32 bits, but that can't be
     achieved by splitting an Opus band) */
    8097453, 18474633, 39753273, 81270333, 158819253, 298199265, 540279585,
    948062325, 1616336765,
    /* N = 12, K = 12...18 */
    45046719, 103274625, 224298231, 464387817, 921406335, 1759885185,
    3248227095,
    /* N = 13, K = 13...16 */
    251595969, 579168825, 1267854873, 2653649025,
    /* N = 14, K = 14 */
    1409933619
};

DECLARE_ALIGNED(32, static const float, celt_window)[120] = {
    6.7286966e-05f, 0.00060551348f, 0.0016815970f, 0.0032947962f, 0.0054439943f,
    0.0081276923f, 0.011344001f, 0.015090633f, 0.019364886f, 0.024163635f,
    0.029483315f, 0.035319905f, 0.041668911f, 0.048525347f, 0.055883718f,
    0.063737999f, 0.072081616f, 0.080907428f, 0.090207705f, 0.099974111f,
    0.11019769f, 0.12086883f, 0.13197729f, 0.14351214f, 0.15546177f,
    0.16781389f, 0.18055550f, 0.19367290f, 0.20715171f, 0.22097682f,
    0.23513243f, 0.24960208f, 0.26436860f, 0.27941419f, 0.29472040f,
    0.31026818f, 0.32603788f, 0.34200931f, 0.35816177f, 0.37447407f,
    0.39092462f, 0.40749142f, 0.42415215f, 0.44088423f, 0.45766484f,
    0.47447104f, 0.49127978f, 0.50806798f, 0.52481261f, 0.54149077f,
    0.55807973f, 0.57455701f, 0.59090049f, 0.60708841f, 0.62309951f,
    0.63891306f, 0.65450896f, 0.66986776f, 0.68497077f, 0.69980010f,
    0.71433873f, 0.72857055f, 0.74248043f, 0.75605424f, 0.76927895f,
    0.78214257f, 0.79463430f, 0.80674445f, 0.81846456f, 0.82978733f,
    0.84070669f, 0.85121779f, 0.86131698f, 0.87100183f, 0.88027111f,
    0.88912479f, 0.89756398f, 0.90559094f, 0.91320904f, 0.92042270f,
    0.92723738f, 0.93365955f, 0.93969656f, 0.94535671f, 0.95064907f,
    0.95558353f, 0.96017067f, 0.96442171f, 0.96834849f, 0.97196334f,
    0.97527906f, 0.97830883f, 0.98106616f, 0.98356480f, 0.98581869f,
    0.98784191f, 0.98964856f, 0.99125274f, 0.99266849f, 0.99390969f,
    0.99499004f, 0.99592297f, 0.99672162f, 0.99739874f, 0.99796667f,
    0.99843728f, 0.99882195f, 0.99913147f, 0.99937606f, 0.99956527f,
    0.99970802f, 0.99981248f, 0.99988613f, 0.99993565f, 0.99996697f,
    0.99998518f, 0.99999457f, 0.99999859f, 0.99999982f, 1.0000000f,
};

/* square of the window, used for the postfilter */
const float ff_celt_window2[120] = {
    4.5275357e-09f, 3.66647e-07f, 2.82777e-06f, 1.08557e-05f, 2.96371e-05f, 6.60594e-05f,
    0.000128686f, 0.000227727f, 0.000374999f, 0.000583881f, 0.000869266f, 0.0012475f,
    0.0017363f, 0.00235471f, 0.00312299f, 0.00406253f, 0.00519576f, 0.00654601f,
    0.00813743f, 0.00999482f, 0.0121435f, 0.0146093f, 0.017418f, 0.0205957f, 0.0241684f,
    0.0281615f, 0.0326003f, 0.0375092f, 0.0429118f, 0.0488308f, 0.0552873f, 0.0623012f,
    0.0698908f, 0.0780723f, 0.0868601f, 0.0962664f, 0.106301f, 0.11697f, 0.12828f,
    0.140231f, 0.152822f, 0.166049f, 0.179905f, 0.194379f, 0.209457f, 0.225123f, 0.241356f,
    0.258133f, 0.275428f, 0.293212f, 0.311453f, 0.330116f, 0.349163f, 0.368556f, 0.388253f,
    0.40821f, 0.428382f, 0.448723f, 0.469185f, 0.48972f, 0.51028f, 0.530815f, 0.551277f,
    0.571618f, 0.59179f, 0.611747f, 0.631444f, 0.650837f, 0.669884f, 0.688547f, 0.706788f,
    0.724572f, 0.741867f, 0.758644f, 0.774877f, 0.790543f, 0.805621f, 0.820095f, 0.833951f,
    0.847178f, 0.859769f, 0.87172f, 0.88303f, 0.893699f, 0.903734f, 0.91314f, 0.921928f,
    0.930109f, 0.937699f, 0.944713f, 0.951169f, 0.957088f, 0.962491f, 0.9674f, 0.971838f,
    0.975832f, 0.979404f, 0.982582f, 0.985391f, 0.987857f, 0.990005f, 0.991863f, 0.993454f,
    0.994804f, 0.995937f, 0.996877f, 0.997645f, 0.998264f, 0.998753f, 0.999131f, 0.999416f,
    0.999625f, 0.999772f, 0.999871f, 0.999934f, 0.99997f, 0.999989f, 0.999997f, 0.99999964f, 1.0f,
};

static const uint32_t * const celt_pvq_u_row[15] = {
    celt_pvq_u +    0, celt_pvq_u +  176, celt_pvq_u +  351,
    celt_pvq_u +  525, celt_pvq_u +  698, celt_pvq_u +  870,
    celt_pvq_u + 1041, celt_pvq_u + 1131, celt_pvq_u + 1178,
    celt_pvq_u + 1207, celt_pvq_u + 1226, celt_pvq_u + 1240,
    celt_pvq_u + 1248, celt_pvq_u + 1254, celt_pvq_u + 1257
};

static inline int16_t celt_cos(int16_t x)
{
    x = (MUL16(x, x) + 4096) >> 13;
    x = (32767-x) + ROUND_MUL16(x, (-7651 + ROUND_MUL16(x, (8277 + ROUND_MUL16(-626, x)))));
    return 1+x;
}

static inline int celt_log2tan(int isin, int icos)
{
    int lc, ls;
    lc = opus_ilog(icos);
    ls = opus_ilog(isin);
    icos <<= 15 - lc;
    isin <<= 15 - ls;
    return (ls << 11) - (lc << 11) +
           ROUND_MUL16(isin, ROUND_MUL16(isin, -2597) + 7932) -
           ROUND_MUL16(icos, ROUND_MUL16(icos, -2597) + 7932);
}

static inline uint32_t celt_rng(CeltContext *s)
{
    s->seed = 1664525 * s->seed + 1013904223;
    return s->seed;
}

static void celt_decode_coarse_energy(CeltContext *s, OpusRangeCoder *rc)
{
    int i, j;
    float prev[2] = {0};
    float alpha, beta;
    const uint8_t *model;

    /* use the 2D z-transform to apply prediction in both */
    /* the time domain (alpha) and the frequency domain (beta) */

    if (opus_rc_tell(rc)+3 <= s->framebits && opus_rc_p2model(rc, 3)) {
        /* intra frame */
        alpha = 0;
        beta  = 1.0f - 4915.0f/32768.0f;
        model = celt_coarse_energy_dist[s->duration][1];
    } else {
        alpha = celt_alpha_coef[s->duration];
        beta  = 1.0f - celt_beta_coef[s->duration];
        model = celt_coarse_energy_dist[s->duration][0];
    }

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        for (j = 0; j < s->coded_channels; j++) {
            CeltFrame *frame = &s->frame[j];
            float value;
            int available;

            if (i < s->startband || i >= s->endband) {
                frame->energy[i] = 0.0;
                continue;
            }

            available = s->framebits - opus_rc_tell(rc);
            if (available >= 15) {
                /* decode using a Laplace distribution */
                int k = FFMIN(i, 20) << 1;
                value = opus_rc_laplace(rc, model[k] << 7, model[k+1] << 6);
            } else if (available >= 2) {
                int x = opus_rc_getsymbol(rc, celt_model_energy_small);
                value = (x>>1) ^ -(x&1);
            } else if (available >= 1) {
                value = -(float)opus_rc_p2model(rc, 1);
            } else value = -1;

            frame->energy[i] = FFMAX(-9.0f, frame->energy[i]) * alpha + prev[j] + value;
            prev[j] += beta * value;
        }
    }
}

static void celt_decode_fine_energy(CeltContext *s, OpusRangeCoder *rc)
{
    int i;
    for (i = s->startband; i < s->endband; i++) {
        int j;
        if (!s->fine_bits[i])
            continue;

        for (j = 0; j < s->coded_channels; j++) {
            CeltFrame *frame = &s->frame[j];
            int q2;
            float offset;
            q2 = opus_getrawbits(rc, s->fine_bits[i]);
            offset = (q2 + 0.5f) * (1 << (14 - s->fine_bits[i])) / 16384.0f - 0.5f;
            frame->energy[i] += offset;
        }
    }
}

static void celt_decode_final_energy(CeltContext *s, OpusRangeCoder *rc,
                                     int bits_left)
{
    int priority, i, j;

    for (priority = 0; priority < 2; priority++) {
        for (i = s->startband; i < s->endband && bits_left >= s->coded_channels; i++) {
            if (s->fine_priority[i] != priority || s->fine_bits[i] >= CELT_MAX_FINE_BITS)
                continue;

            for (j = 0; j < s->coded_channels; j++) {
                int q2;
                float offset;
                q2 = opus_getrawbits(rc, 1);
                offset = (q2 - 0.5f) * (1 << (14 - s->fine_bits[i] - 1)) / 16384.0f;
                s->frame[j].energy[i] += offset;
                bits_left--;
            }
        }
    }
}

static void celt_decode_tf_changes(CeltContext *s, OpusRangeCoder *rc,
                                   int transient)
{
    int i, diff = 0, tf_select = 0, tf_changed = 0, tf_select_bit;
    int consumed, bits = transient ? 2 : 4;

    consumed = opus_rc_tell(rc);
    tf_select_bit = (s->duration != 0 && consumed+bits+1 <= s->framebits);

    for (i = s->startband; i < s->endband; i++) {
        if (consumed+bits+tf_select_bit <= s->framebits) {
            diff ^= opus_rc_p2model(rc, bits);
            consumed = opus_rc_tell(rc);
            tf_changed |= diff;
        }
        s->tf_change[i] = diff;
        bits = transient ? 4 : 5;
    }

    if (tf_select_bit && celt_tf_select[s->duration][transient][0][tf_changed] !=
                         celt_tf_select[s->duration][transient][1][tf_changed])
        tf_select = opus_rc_p2model(rc, 1);

    for (i = s->startband; i < s->endband; i++) {
        s->tf_change[i] = celt_tf_select[s->duration][transient][tf_select][s->tf_change[i]];
    }
}

static void celt_decode_allocation(CeltContext *s, OpusRangeCoder *rc)
{
    // approx. maximum bit allocation for each band before boost/trim
    int cap[CELT_MAX_BANDS];
    int boost[CELT_MAX_BANDS];
    int threshold[CELT_MAX_BANDS];
    int bits1[CELT_MAX_BANDS];
    int bits2[CELT_MAX_BANDS];
    int trim_offset[CELT_MAX_BANDS];

    int skip_startband = s->startband;
    int dynalloc       = 6;
    int alloctrim      = 5;
    int extrabits      = 0;

    int skip_bit            = 0;
    int intensitystereo_bit = 0;
    int dualstereo_bit      = 0;

    int remaining, bandbits;
    int low, high, total, done;
    int totalbits;
    int consumed;
    int i, j;

    consumed = opus_rc_tell(rc);

    /* obtain spread flag */
    s->spread = CELT_SPREAD_NORMAL;
    if (consumed + 4 <= s->framebits)
        s->spread = opus_rc_getsymbol(rc, celt_model_spread);

    /* generate static allocation caps */
    for (i = 0; i < CELT_MAX_BANDS; i++) {
        cap[i] = (celt_static_caps[s->duration][s->coded_channels - 1][i] + 64)
                 * celt_freq_range[i] << (s->coded_channels - 1) << s->duration >> 2;
    }

    /* obtain band boost */
    totalbits = s->framebits << 3; // convert to 1/8 bits
    consumed = opus_rc_tell_frac(rc);
    for (i = s->startband; i < s->endband; i++) {
        int quanta, band_dynalloc;

        boost[i] = 0;

        quanta = celt_freq_range[i] << (s->coded_channels - 1) << s->duration;
        quanta = FFMIN(quanta << 3, FFMAX(6 << 3, quanta));
        band_dynalloc = dynalloc;
        while (consumed + (band_dynalloc<<3) < totalbits && boost[i] < cap[i]) {
            int add = opus_rc_p2model(rc, band_dynalloc);
            consumed = opus_rc_tell_frac(rc);
            if (!add)
                break;

            boost[i]     += quanta;
            totalbits    -= quanta;
            band_dynalloc = 1;
        }
        /* dynalloc is more likely to occur if it's already been used for earlier bands */
        if (boost[i])
            dynalloc = FFMAX(2, dynalloc - 1);
    }

    /* obtain allocation trim */
    if (consumed + (6 << 3) <= totalbits)
        alloctrim = opus_rc_getsymbol(rc, celt_model_alloc_trim);

    /* anti-collapse bit reservation */
    totalbits = (s->framebits << 3) - opus_rc_tell_frac(rc) - 1;
    s->anticollapse_bit = 0;
    if (s->blocks > 1 && s->duration >= 2 &&
        totalbits >= ((s->duration + 2) << 3))
        s->anticollapse_bit = 1 << 3;
    totalbits -= s->anticollapse_bit;

    /* band skip bit reservation */
    if (totalbits >= 1 << 3)
        skip_bit = 1 << 3;
    totalbits -= skip_bit;

    /* intensity/dual stereo bit reservation */
    if (s->coded_channels == 2) {
        intensitystereo_bit = celt_log2_frac[s->endband - s->startband];
        if (intensitystereo_bit <= totalbits) {
            totalbits -= intensitystereo_bit;
            if (totalbits >= 1 << 3) {
                dualstereo_bit = 1 << 3;
                totalbits -= 1 << 3;
            }
        } else
            intensitystereo_bit = 0;
    }

    for (i = s->startband; i < s->endband; i++) {
        int trim     = alloctrim - 5 - s->duration;
        int band     = celt_freq_range[i] * (s->endband - i - 1);
        int duration = s->duration + 3;
        int scale    = duration + s->coded_channels - 1;

        /* PVQ minimum allocation threshold, below this value the band is
         * skipped */
        threshold[i] = FFMAX(3 * celt_freq_range[i] << duration >> 4,
                             s->coded_channels << 3);

        trim_offset[i] = trim * (band << scale) >> 6;

        if (celt_freq_range[i] << s->duration == 1)
            trim_offset[i] -= s->coded_channels << 3;
    }

    /* bisection */
    low  = 1;
    high = CELT_VECTORS - 1;
    while (low <= high) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (i = s->endband - 1; i >= s->startband; i--) {
            bandbits = celt_freq_range[i] * celt_static_alloc[center][i]
                       << (s->coded_channels - 1) << s->duration >> 2;

            if (bandbits)
                bandbits = FFMAX(0, bandbits + trim_offset[i]);
            bandbits += boost[i];

            if (bandbits >= threshold[i] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[i]);
            } else if (bandbits >= s->coded_channels << 3)
                total += s->coded_channels << 3;
        }

        if (total > totalbits)
            high = center - 1;
        else
            low = center + 1;
    }
    high = low--;

    for (i = s->startband; i < s->endband; i++) {
        bits1[i] = celt_freq_range[i] * celt_static_alloc[low][i]
                   << (s->coded_channels - 1) << s->duration >> 2;
        bits2[i] = high >= CELT_VECTORS ? cap[i] :
                   celt_freq_range[i] * celt_static_alloc[high][i]
                   << (s->coded_channels - 1) << s->duration >> 2;

        if (bits1[i])
            bits1[i] = FFMAX(0, bits1[i] + trim_offset[i]);
        if (bits2[i])
            bits2[i] = FFMAX(0, bits2[i] + trim_offset[i]);
        if (low)
            bits1[i] += boost[i];
        bits2[i] += boost[i];

        if (boost[i])
            skip_startband = i;
        bits2[i] = FFMAX(0, bits2[i] - bits1[i]);
    }

    /* bisection */
    low  = 0;
    high = 1 << CELT_ALLOC_STEPS;
    for (i = 0; i < CELT_ALLOC_STEPS; i++) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (j = s->endband - 1; j >= s->startband; j--) {
            bandbits = bits1[j] + (center * bits2[j] >> CELT_ALLOC_STEPS);

            if (bandbits >= threshold[j] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[j]);
            } else if (bandbits >= s->coded_channels << 3)
                total += s->coded_channels << 3;
        }
        if (total > totalbits)
            high = center;
        else
            low = center;
    }

    done = total = 0;
    for (i = s->endband - 1; i >= s->startband; i--) {
        bandbits = bits1[i] + (low * bits2[i] >> CELT_ALLOC_STEPS);

        if (bandbits >= threshold[i] || done)
            done = 1;
        else
            bandbits = (bandbits >= s->coded_channels << 3) ?
                       s->coded_channels << 3 : 0;

        bandbits     = FFMIN(bandbits, cap[i]);
        s->pulses[i] = bandbits;
        total      += bandbits;
    }

    /* band skipping */
    for (s->codedbands = s->endband; ; s->codedbands--) {
        int allocation;
        j = s->codedbands - 1;

        if (j == skip_startband) {
            /* all remaining bands are not skipped */
            totalbits += skip_bit;
            break;
        }

        /* determine the number of bits available for coding "do not skip" markers */
        remaining   = totalbits - total;
        bandbits    = remaining / (celt_freq_bands[j+1] - celt_freq_bands[s->startband]);
        remaining  -= bandbits  * (celt_freq_bands[j+1] - celt_freq_bands[s->startband]);
        allocation  = s->pulses[j] + bandbits * celt_freq_range[j]
                      + FFMAX(0, remaining - (celt_freq_bands[j] - celt_freq_bands[s->startband]));

        /* a "do not skip" marker is only coded if the allocation is
           above the chosen threshold */
        if (allocation >= FFMAX(threshold[j], (s->coded_channels + 1) <<3 )) {
            if (opus_rc_p2model(rc, 1))
                break;

            total      += 1 << 3;
            allocation -= 1 << 3;
        }

        /* the band is skipped, so reclaim its bits */
        total -= s->pulses[j];
        if (intensitystereo_bit) {
            total -= intensitystereo_bit;
            intensitystereo_bit = celt_log2_frac[j - s->startband];
            total += intensitystereo_bit;
        }

        total += s->pulses[j] = (allocation >= s->coded_channels << 3) ?
                              s->coded_channels << 3 : 0;
    }

    /* obtain stereo flags */
    s->intensitystereo = 0;
    s->dualstereo      = 0;
    if (intensitystereo_bit)
        s->intensitystereo = s->startband +
                          opus_rc_unimodel(rc, s->codedbands + 1 - s->startband);
    if (s->intensitystereo <= s->startband)
        totalbits += dualstereo_bit; /* no intensity stereo means no dual stereo */
    else if (dualstereo_bit)
        s->dualstereo = opus_rc_p2model(rc, 1);

    /* supply the remaining bits in this frame to lower bands */
    remaining = totalbits - total;
    bandbits  = remaining / (celt_freq_bands[s->codedbands] - celt_freq_bands[s->startband]);
    remaining -= bandbits * (celt_freq_bands[s->codedbands] - celt_freq_bands[s->startband]);
    for (i = s->startband; i < s->codedbands; i++) {
        int bits = FFMIN(remaining, celt_freq_range[i]);

        s->pulses[i] += bits + bandbits * celt_freq_range[i];
        remaining    -= bits;
    }

    for (i = s->startband; i < s->codedbands; i++) {
        int N = celt_freq_range[i] << s->duration;
        int prev_extra = extrabits;
        s->pulses[i] += extrabits;

        if (N > 1) {
            int dof;        // degrees of freedom
            int temp;       // dof * channels * log(dof)
            int offset;     // fine energy quantization offset, i.e.
                            // extra bits assigned over the standard
                            // totalbits/dof
            int fine_bits, max_bits;

            extrabits = FFMAX(0, s->pulses[i] - cap[i]);
            s->pulses[i] -= extrabits;

            /* intensity stereo makes use of an extra degree of freedom */
            dof = N * s->coded_channels
                  + (s->coded_channels == 2 && N > 2 && !s->dualstereo && i < s->intensitystereo);
            temp = dof * (celt_log_freq_range[i] + (s->duration<<3));
            offset = (temp >> 1) - dof * CELT_FINE_OFFSET;
            if (N == 2) /* dof=2 is the only case that doesn't fit the model */
                offset += dof<<1;

            /* grant an additional bias for the first and second pulses */
            if (s->pulses[i] + offset < 2 * (dof << 3))
                offset += temp >> 2;
            else if (s->pulses[i] + offset < 3 * (dof << 3))
                offset += temp >> 3;

            fine_bits = (s->pulses[i] + offset + (dof << 2)) / (dof << 3);
            max_bits  = FFMIN((s->pulses[i]>>3) >> (s->coded_channels - 1),
                              CELT_MAX_FINE_BITS);

            max_bits  = FFMAX(max_bits, 0);

            s->fine_bits[i] = av_clip(fine_bits, 0, max_bits);

            /* if fine_bits was rounded down or capped,
               give priority for the final fine energy pass */
            s->fine_priority[i] = (s->fine_bits[i] * (dof<<3) >= s->pulses[i] + offset);

            /* the remaining bits are assigned to PVQ */
            s->pulses[i] -= s->fine_bits[i] << (s->coded_channels - 1) << 3;
        } else {
            /* all bits go to fine energy except for the sign bit */
            extrabits = FFMAX(0, s->pulses[i] - (s->coded_channels << 3));
            s->pulses[i] -= extrabits;
            s->fine_bits[i] = 0;
            s->fine_priority[i] = 1;
        }

        /* hand back a limited number of extra fine energy bits to this band */
        if (extrabits > 0) {
            int fineextra = FFMIN(extrabits >> (s->coded_channels + 2),
                                  CELT_MAX_FINE_BITS - s->fine_bits[i]);
            s->fine_bits[i] += fineextra;

            fineextra <<= s->coded_channels + 2;
            s->fine_priority[i] = (fineextra >= extrabits - prev_extra);
            extrabits -= fineextra;
        }
    }
    s->remaining = extrabits;

    /* skipped bands dedicate all of their bits for fine energy */
    for (; i < s->endband; i++) {
        s->fine_bits[i]     = s->pulses[i] >> (s->coded_channels - 1) >> 3;
        s->pulses[i]        = 0;
        s->fine_priority[i] = s->fine_bits[i] < 1;
    }
}

static inline int celt_bits2pulses(const uint8_t *cache, int bits)
{
    // TODO: Find the size of cache and make it into an array in the parameters list
    int i, low = 0, high;

    high = cache[0];
    bits--;

    for (i = 0; i < 6; i++) {
        int center = (low + high + 1) >> 1;
        if (cache[center] >= bits)
            high = center;
        else
            low = center;
    }

    return (bits - (low == 0 ? -1 : cache[low]) <= cache[high] - bits) ? low : high;
}

static inline int celt_pulses2bits(const uint8_t *cache, int pulses)
{
    // TODO: Find the size of cache and make it into an array in the parameters list
   return (pulses == 0) ? 0 : cache[pulses] + 1;
}

static inline void celt_normalize_residual(const int * av_restrict iy, float * av_restrict X,
                                           int N, float g)
{
    int i;
    for (i = 0; i < N; i++)
        X[i] = g * iy[i];
}

static void celt_exp_rotation1(float *X, unsigned int len, unsigned int stride,
                               float c, float s)
{
    float *Xptr;
    int i;

    Xptr = X;
    for (i = 0; i < len - stride; i++) {
        float x1, x2;
        x1           = Xptr[0];
        x2           = Xptr[stride];
        Xptr[stride] = c * x2 + s * x1;
        *Xptr++      = c * x1 - s * x2;
    }

    Xptr = &X[len - 2 * stride - 1];
    for (i = len - 2 * stride - 1; i >= 0; i--) {
        float x1, x2;
        x1           = Xptr[0];
        x2           = Xptr[stride];
        Xptr[stride] = c * x2 + s * x1;
        *Xptr--      = c * x1 - s * x2;
    }
}

static inline void celt_exp_rotation(float *X, unsigned int len,
                                     unsigned int stride, unsigned int K,
                                     enum CeltSpread spread)
{
    unsigned int stride2 = 0;
    float c, s;
    float gain, theta;
    int i;

    if (2*K >= len || spread == CELT_SPREAD_NONE)
        return;

    gain = (float)len / (len + (20 - 5*spread) * K);
    theta = M_PI * gain * gain / 4;

    c = cos(theta);
    s = sin(theta);

    if (len >= stride << 3) {
        stride2 = 1;
        /* This is just a simple (equivalent) way of computing sqrt(len/stride) with rounding.
        It's basically incrementing long as (stride2+0.5)^2 < len/stride. */
        while ((stride2 * stride2 + stride2) * stride + (stride >> 2) < len)
            stride2++;
    }

    /*NOTE: As a minor optimization, we could be passing around log2(B), not B, for both this and for
    extract_collapse_mask().*/
    len /= stride;
    for (i = 0; i < stride; i++) {
        if (stride2)
            celt_exp_rotation1(X + i * len, len, stride2, s, c);
        celt_exp_rotation1(X + i * len, len, 1, c, s);
    }
}

static inline unsigned int celt_extract_collapse_mask(const int *iy,
                                                      unsigned int N,
                                                      unsigned int B)
{
    unsigned int collapse_mask;
    int N0;
    int i, j;

    if (B <= 1)
        return 1;

    /*NOTE: As a minor optimization, we could be passing around log2(B), not B, for both this and for
    exp_rotation().*/
    N0 = N/B;
    collapse_mask = 0;
    for (i = 0; i < B; i++)
        for (j = 0; j < N0; j++)
            collapse_mask |= (iy[i*N0+j]!=0)<<i;
    return collapse_mask;
}

static inline void celt_renormalize_vector(float *X, int N, float gain)
{
    int i;
    float g = 1e-15f;
    for (i = 0; i < N; i++)
        g += X[i] * X[i];
    g = gain / sqrtf(g);

    for (i = 0; i < N; i++)
        X[i] *= g;
}

static inline void celt_stereo_merge(float *X, float *Y, float mid, int N)
{
    int i;
    float xp = 0, side = 0;
    float E[2];
    float mid2;
    float t, gain[2];

    /* Compute the norm of X+Y and X-Y as |X|^2 + |Y|^2 +/- sum(xy) */
    for (i = 0; i < N; i++) {
        xp   += X[i] * Y[i];
        side += Y[i] * Y[i];
    }

    /* Compensating for the mid normalization */
    xp *= mid;
    mid2 = mid;
    E[0] = mid2 * mid2 + side - 2 * xp;
    E[1] = mid2 * mid2 + side + 2 * xp;
    if (E[0] < 6e-4f || E[1] < 6e-4f) {
        for (i = 0; i < N; i++)
            Y[i] = X[i];
        return;
    }

    t = E[0];
    gain[0] = 1.0f / sqrtf(t);
    t = E[1];
    gain[1] = 1.0f / sqrtf(t);

    for (i = 0; i < N; i++) {
        float value[2];
        /* Apply mid scaling (side is already scaled) */
        value[0] = mid * X[i];
        value[1] = Y[i];
        X[i] = gain[0] * (value[0] - value[1]);
        Y[i] = gain[1] * (value[0] + value[1]);
    }
}

static void celt_interleave_hadamard(float *tmp, float *X, int N0,
                                     int stride, int hadamard)
{
    int i, j;
    int N = N0*stride;

    if (hadamard) {
        const uint8_t *ordery = celt_hadamard_ordery + stride - 2;
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[j*stride+i] = X[ordery[i]*N0+j];
    } else {
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[j*stride+i] = X[i*N0+j];
    }

    for (i = 0; i < N; i++)
        X[i] = tmp[i];
}

static void celt_deinterleave_hadamard(float *tmp, float *X, int N0,
                                       int stride, int hadamard)
{
    int i, j;
    int N = N0*stride;

    if (hadamard) {
        const uint8_t *ordery = celt_hadamard_ordery + stride - 2;
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[ordery[i]*N0+j] = X[j*stride+i];
    } else {
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[i*N0+j] = X[j*stride+i];
    }

    for (i = 0; i < N; i++)
        X[i] = tmp[i];
}

static void celt_haar1(float *X, int N0, int stride)
{
    int i, j;
    N0 >>= 1;
    for (i = 0; i < stride; i++) {
        for (j = 0; j < N0; j++) {
            float x0 = X[stride * (2 * j + 0) + i];
            float x1 = X[stride * (2 * j + 1) + i];
            X[stride * (2 * j + 0) + i] = (x0 + x1) * M_SQRT1_2;
            X[stride * (2 * j + 1) + i] = (x0 - x1) * M_SQRT1_2;
        }
    }
}

static inline int celt_compute_qn(int N, int b, int offset, int pulse_cap,
                                  int dualstereo)
{
    int qn, qb;
    int N2 = 2 * N - 1;
    if (dualstereo && N == 2)
        N2--;

    /* The upper limit ensures that in a stereo split with itheta==16384, we'll
     * always have enough bits left over to code at least one pulse in the
     * side; otherwise it would collapse, since it doesn't get folded. */
    qb = FFMIN3(b - pulse_cap - (4 << 3), (b + N2 * offset) / N2, 8 << 3);
    qn = (qb < (1 << 3 >> 1)) ? 1 : ((celt_qn_exp2[qb & 0x7] >> (14 - (qb >> 3))) + 1) >> 1 << 1;
    return qn;
}

// this code was adapted from libopus
static inline uint64_t celt_cwrsi(unsigned int N, unsigned int K, unsigned int i, int *y)
{
    uint64_t norm = 0;
    uint32_t p;
    int s, val;
    int k0;

    while (N > 2) {
        uint32_t q;

        /*Lots of pulses case:*/
        if (K >= N) {
            const uint32_t *row = celt_pvq_u_row[N];

            /* Are the pulses in this dimension negative? */
            p  = row[K + 1];
            s  = -(i >= p);
            i -= p & s;

            /*Count how many pulses were placed in this dimension.*/
            k0 = K;
            q = row[N];
            if (q > i) {
                K = N;
                do {
                    p = celt_pvq_u_row[--K][N];
                } while (p > i);
            } else
                for (p = row[K]; p > i; p = row[K])
                    K--;

            i    -= p;
            val   = (k0 - K + s) ^ s;
            norm += val * val;
            *y++  = val;
        } else { /*Lots of dimensions case:*/
            /*Are there any pulses in this dimension at all?*/
            p = celt_pvq_u_row[K    ][N];
            q = celt_pvq_u_row[K + 1][N];

            if (p <= i && i < q) {
                i -= p;
                *y++ = 0;
            } else {
                /*Are the pulses in this dimension negative?*/
                s  = -(i >= q);
                i -= q & s;

                /*Count how many pulses were placed in this dimension.*/
                k0 = K;
                do p = celt_pvq_u_row[--K][N];
                while (p > i);

                i    -= p;
                val   = (k0 - K + s) ^ s;
                norm += val * val;
                *y++  = val;
            }
        }
        N--;
    }

    /* N == 2 */
    p  = 2 * K + 1;
    s  = -(i >= p);
    i -= p & s;
    k0 = K;
    K  = (i + 1) / 2;

    if (K)
        i -= 2 * K - 1;

    val   = (k0 - K + s) ^ s;
    norm += val * val;
    *y++  = val;

    /* N==1 */
    s     = -i;
    val   = (K + s) ^ s;
    norm += val * val;
    *y    = val;

    return norm;
}

static inline float celt_decode_pulses(OpusRangeCoder *rc, int *y, unsigned int N, unsigned int K)
{
    unsigned int idx;
#define CELT_PVQ_U(n, k) (celt_pvq_u_row[FFMIN(n, k)][FFMAX(n, k)])
#define CELT_PVQ_V(n, k) (CELT_PVQ_U(n, k) + CELT_PVQ_U(n, (k) + 1))
    idx = opus_rc_unimodel(rc, CELT_PVQ_V(N, K));
    return celt_cwrsi(N, K, idx, y);
}

/** Decode pulse vector and combine the result with the pitch vector to produce
    the final normalised signal in the current band. */
static inline unsigned int celt_alg_unquant(OpusRangeCoder *rc, float *X,
                                            unsigned int N, unsigned int K,
                                            enum CeltSpread spread,
                                            unsigned int blocks, float gain)
{
    int y[176];

    gain /= sqrtf(celt_decode_pulses(rc, y, N, K));
    celt_normalize_residual(y, X, N, gain);
    celt_exp_rotation(X, N, blocks, K, spread);
    return celt_extract_collapse_mask(y, N, blocks);
}

static unsigned int celt_decode_band(CeltContext *s, OpusRangeCoder *rc,
                                     const int band, float *X, float *Y,
                                     int N, int b, unsigned int blocks,
                                     float *lowband, int duration,
                                     float *lowband_out, int level,
                                     float gain, float *lowband_scratch,
                                     int fill)
{
    const uint8_t *cache;
    int dualstereo, split;
    int imid = 0, iside = 0;
    unsigned int N0 = N;
    int N_B;
    int N_B0;
    int B0 = blocks;
    int time_divide = 0;
    int recombine = 0;
    int inv = 0;
    float mid = 0, side = 0;
    int longblocks = (B0 == 1);
    unsigned int cm = 0;

    N_B0 = N_B = N / blocks;
    split = dualstereo = (Y != NULL);

    if (N == 1) {
        /* special case for one sample */
        int i;
        float *x = X;
        for (i = 0; i <= dualstereo; i++) {
            int sign = 0;
            if (s->remaining2 >= 1<<3) {
                sign           = opus_getrawbits(rc, 1);
                s->remaining2 -= 1 << 3;
                b             -= 1 << 3;
            }
            x[0] = sign ? -1.0f : 1.0f;
            x = Y;
        }
        if (lowband_out)
            lowband_out[0] = X[0];
        return 1;
    }

    if (!dualstereo && level == 0) {
        int tf_change = s->tf_change[band];
        int k;
        if (tf_change > 0)
            recombine = tf_change;
        /* Band recombining to increase frequency resolution */

        if (lowband &&
            (recombine || ((N_B & 1) == 0 && tf_change < 0) || B0 > 1)) {
            int j;
            for (j = 0; j < N; j++)
                lowband_scratch[j] = lowband[j];
            lowband = lowband_scratch;
        }

        for (k = 0; k < recombine; k++) {
            if (lowband)
                celt_haar1(lowband, N >> k, 1 << k);
            fill = celt_bit_interleave[fill & 0xF] | celt_bit_interleave[fill >> 4] << 2;
        }
        blocks >>= recombine;
        N_B <<= recombine;

        /* Increasing the time resolution */
        while ((N_B & 1) == 0 && tf_change < 0) {
            if (lowband)
                celt_haar1(lowband, N_B, blocks);
            fill |= fill << blocks;
            blocks <<= 1;
            N_B >>= 1;
            time_divide++;
            tf_change++;
        }
        B0 = blocks;
        N_B0 = N_B;

        /* Reorganize the samples in time order instead of frequency order */
        if (B0 > 1 && lowband)
            celt_deinterleave_hadamard(s->scratch, lowband, N_B >> recombine,
                                       B0 << recombine, longblocks);
    }

    /* If we need 1.5 more bit than we can produce, split the band in two. */
    cache = celt_cache_bits +
            celt_cache_index[(duration + 1) * CELT_MAX_BANDS + band];
    if (!dualstereo && duration >= 0 && b > cache[cache[0]] + 12 && N > 2) {
        N >>= 1;
        Y = X + N;
        split = 1;
        duration -= 1;
        if (blocks == 1)
            fill = (fill & 1) | (fill << 1);
        blocks = (blocks + 1) >> 1;
    }

    if (split) {
        int qn;
        int itheta = 0;
        int mbits, sbits, delta;
        int qalloc;
        int pulse_cap;
        int offset;
        int orig_fill;
        int tell;

        /* Decide on the resolution to give to the split parameter theta */
        pulse_cap = celt_log_freq_range[band] + duration * 8;
        offset = (pulse_cap >> 1) - (dualstereo && N == 2 ? CELT_QTHETA_OFFSET_TWOPHASE :
                                                          CELT_QTHETA_OFFSET);
        qn = (dualstereo && band >= s->intensitystereo) ? 1 :
             celt_compute_qn(N, b, offset, pulse_cap, dualstereo);
        tell = opus_rc_tell_frac(rc);
        if (qn != 1) {
            /* Entropy coding of the angle. We use a uniform pdf for the
            time split, a step for stereo, and a triangular one for the rest. */
            if (dualstereo && N > 2)
                itheta = opus_rc_stepmodel(rc, qn/2);
            else if (dualstereo || B0 > 1)
                itheta = opus_rc_unimodel(rc, qn+1);
            else
                itheta = opus_rc_trimodel(rc, qn);
            itheta = itheta * 16384 / qn;
            /* NOTE: Renormalising X and Y *may* help fixed-point a bit at very high rate.
            Let's do that at higher complexity */
        } else if (dualstereo) {
            inv = (b > 2 << 3 && s->remaining2 > 2 << 3) ? opus_rc_p2model(rc, 2) : 0;
            itheta = 0;
        }
        qalloc = opus_rc_tell_frac(rc) - tell;
        b -= qalloc;

        orig_fill = fill;
        if (itheta == 0) {
            imid = 32767;
            iside = 0;
            fill = av_mod_uintp2(fill, blocks);
            delta = -16384;
        } else if (itheta == 16384) {
            imid = 0;
            iside = 32767;
            fill &= ((1 << blocks) - 1) << blocks;
            delta = 16384;
        } else {
            imid = celt_cos(itheta);
            iside = celt_cos(16384-itheta);
            /* This is the mid vs side allocation that minimizes squared error
            in that band. */
            delta = ROUND_MUL16((N - 1) << 7, celt_log2tan(iside, imid));
        }

        mid  = imid  / 32768.0f;
        side = iside / 32768.0f;

        /* This is a special case for N=2 that only works for stereo and takes
        advantage of the fact that mid and side are orthogonal to encode
        the side with just one bit. */
        if (N == 2 && dualstereo) {
            int c;
            int sign = 0;
            float tmp;
            float *x2, *y2;
            mbits = b;
            /* Only need one bit for the side */
            sbits = (itheta != 0 && itheta != 16384) ? 1 << 3 : 0;
            mbits -= sbits;
            c = (itheta > 8192);
            s->remaining2 -= qalloc+sbits;

            x2 = c ? Y : X;
            y2 = c ? X : Y;
            if (sbits)
                sign = opus_getrawbits(rc, 1);
            sign = 1 - 2 * sign;
            /* We use orig_fill here because we want to fold the side, but if
            itheta==16384, we'll have cleared the low bits of fill. */
            cm = celt_decode_band(s, rc, band, x2, NULL, N, mbits, blocks,
                                  lowband, duration, lowband_out, level, gain,
                                  lowband_scratch, orig_fill);
            /* We don't split N=2 bands, so cm is either 1 or 0 (for a fold-collapse),
            and there's no need to worry about mixing with the other channel. */
            y2[0] = -sign * x2[1];
            y2[1] =  sign * x2[0];
            X[0] *= mid;
            X[1] *= mid;
            Y[0] *= side;
            Y[1] *= side;
            tmp = X[0];
            X[0] = tmp - Y[0];
            Y[0] = tmp + Y[0];
            tmp = X[1];
            X[1] = tmp - Y[1];
            Y[1] = tmp + Y[1];
        } else {
            /* "Normal" split code */
            float *next_lowband2     = NULL;
            float *next_lowband_out1 = NULL;
            int next_level = 0;
            int rebalance;

            /* Give more bits to low-energy MDCTs than they would
             * otherwise deserve */
            if (B0 > 1 && !dualstereo && (itheta & 0x3fff)) {
                if (itheta > 8192)
                    /* Rough approximation for pre-echo masking */
                    delta -= delta >> (4 - duration);
                else
                    /* Corresponds to a forward-masking slope of
                     * 1.5 dB per 10 ms */
                    delta = FFMIN(0, delta + (N << 3 >> (5 - duration)));
            }
            mbits = av_clip((b - delta) / 2, 0, b);
            sbits = b - mbits;
            s->remaining2 -= qalloc;

            if (lowband && !dualstereo)
                next_lowband2 = lowband + N; /* >32-bit split case */

            /* Only stereo needs to pass on lowband_out.
             * Otherwise, it's handled at the end */
            if (dualstereo)
                next_lowband_out1 = lowband_out;
            else
                next_level = level + 1;

            rebalance = s->remaining2;
            if (mbits >= sbits) {
                /* In stereo mode, we do not apply a scaling to the mid
                 * because we need the normalized mid for folding later */
                cm = celt_decode_band(s, rc, band, X, NULL, N, mbits, blocks,
                                      lowband, duration, next_lowband_out1,
                                      next_level, dualstereo ? 1.0f : (gain * mid),
                                      lowband_scratch, fill);

                rebalance = mbits - (rebalance - s->remaining2);
                if (rebalance > 3 << 3 && itheta != 0)
                    sbits += rebalance - (3 << 3);

                /* For a stereo split, the high bits of fill are always zero,
                 * so no folding will be done to the side. */
                cm |= celt_decode_band(s, rc, band, Y, NULL, N, sbits, blocks,
                                       next_lowband2, duration, NULL,
                                       next_level, gain * side, NULL,
                                       fill >> blocks) << ((B0 >> 1) & (dualstereo - 1));
            } else {
                /* For a stereo split, the high bits of fill are always zero,
                 * so no folding will be done to the side. */
                cm = celt_decode_band(s, rc, band, Y, NULL, N, sbits, blocks,
                                      next_lowband2, duration, NULL,
                                      next_level, gain * side, NULL,
                                      fill >> blocks) << ((B0 >> 1) & (dualstereo - 1));

                rebalance = sbits - (rebalance - s->remaining2);
                if (rebalance > 3 << 3 && itheta != 16384)
                    mbits += rebalance - (3 << 3);

                /* In stereo mode, we do not apply a scaling to the mid because
                 * we need the normalized mid for folding later */
                cm |= celt_decode_band(s, rc, band, X, NULL, N, mbits, blocks,
                                       lowband, duration, next_lowband_out1,
                                       next_level, dualstereo ? 1.0f : (gain * mid),
                                       lowband_scratch, fill);
            }
        }
    } else {
        /* This is the basic no-split case */
        unsigned int q         = celt_bits2pulses(cache, b);
        unsigned int curr_bits = celt_pulses2bits(cache, q);
        s->remaining2 -= curr_bits;

        /* Ensures we can never bust the budget */
        while (s->remaining2 < 0 && q > 0) {
            s->remaining2 += curr_bits;
            curr_bits      = celt_pulses2bits(cache, --q);
            s->remaining2 -= curr_bits;
        }

        if (q != 0) {
            /* Finally do the actual quantization */
            cm = celt_alg_unquant(rc, X, N, (q < 8) ? q : (8 + (q & 7)) << ((q >> 3) - 1),
                                  s->spread, blocks, gain);
        } else {
            /* If there's no pulse, fill the band anyway */
            int j;
            unsigned int cm_mask = (1 << blocks) - 1;
            fill &= cm_mask;
            if (!fill) {
                for (j = 0; j < N; j++)
                    X[j] = 0.0f;
            } else {
                if (!lowband) {
                    /* Noise */
                    for (j = 0; j < N; j++)
                        X[j] = (((int32_t)celt_rng(s)) >> 20);
                    cm = cm_mask;
                } else {
                    /* Folded spectrum */
                    for (j = 0; j < N; j++) {
                        /* About 48 dB below the "normal" folding level */
                        X[j] = lowband[j] + (((celt_rng(s)) & 0x8000) ? 1.0f / 256 : -1.0f / 256);
                    }
                    cm = fill;
                }
                celt_renormalize_vector(X, N, gain);
            }
        }
    }

    /* This code is used by the decoder and by the resynthesis-enabled encoder */
    if (dualstereo) {
        int j;
        if (N != 2)
            celt_stereo_merge(X, Y, mid, N);
        if (inv) {
            for (j = 0; j < N; j++)
                Y[j] *= -1;
        }
    } else if (level == 0) {
        int k;

        /* Undo the sample reorganization going from time order to frequency order */
        if (B0 > 1)
            celt_interleave_hadamard(s->scratch, X, N_B>>recombine,
                                     B0<<recombine, longblocks);

        /* Undo time-freq changes that we did earlier */
        N_B = N_B0;
        blocks = B0;
        for (k = 0; k < time_divide; k++) {
            blocks >>= 1;
            N_B <<= 1;
            cm |= cm >> blocks;
            celt_haar1(X, N_B, blocks);
        }

        for (k = 0; k < recombine; k++) {
            cm = celt_bit_deinterleave[cm];
            celt_haar1(X, N0>>k, 1<<k);
        }
        blocks <<= recombine;

        /* Scale output for later folding */
        if (lowband_out) {
            int j;
            float n = sqrtf(N0);
            for (j = 0; j < N0; j++)
                lowband_out[j] = n * X[j];
        }
        cm = av_mod_uintp2(cm, blocks);
    }
    return cm;
}

static void celt_denormalize(CeltContext *s, CeltFrame *frame, float *data)
{
    int i, j;

    for (i = s->startband; i < s->endband; i++) {
        float *dst = data + (celt_freq_bands[i] << s->duration);
        float norm = exp2(frame->energy[i] + celt_mean_energy[i]);

        for (j = 0; j < celt_freq_range[i] << s->duration; j++)
            dst[j] *= norm;
    }
}

static void celt_postfilter_apply_transition(CeltFrame *frame, float *data)
{
    const int T0 = frame->pf_period_old;
    const int T1 = frame->pf_period;

    float g00, g01, g02;
    float g10, g11, g12;

    float x0, x1, x2, x3, x4;

    int i;

    if (frame->pf_gains[0]     == 0.0 &&
        frame->pf_gains_old[0] == 0.0)
        return;

    g00 = frame->pf_gains_old[0];
    g01 = frame->pf_gains_old[1];
    g02 = frame->pf_gains_old[2];
    g10 = frame->pf_gains[0];
    g11 = frame->pf_gains[1];
    g12 = frame->pf_gains[2];

    x1 = data[-T1 + 1];
    x2 = data[-T1];
    x3 = data[-T1 - 1];
    x4 = data[-T1 - 2];

    for (i = 0; i < CELT_OVERLAP; i++) {
        float w = ff_celt_window2[i];
        x0 = data[i - T1 + 2];

        data[i] +=  (1.0 - w) * g00 * data[i - T0]                          +
                    (1.0 - w) * g01 * (data[i - T0 - 1] + data[i - T0 + 1]) +
                    (1.0 - w) * g02 * (data[i - T0 - 2] + data[i - T0 + 2]) +
                    w         * g10 * x2                                    +
                    w         * g11 * (x1 + x3)                             +
                    w         * g12 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static void celt_postfilter_apply(CeltFrame *frame,
                                  float *data, int len)
{
    const int T = frame->pf_period;
    float g0, g1, g2;
    float x0, x1, x2, x3, x4;
    int i;

    if (frame->pf_gains[0] == 0.0 || len <= 0)
        return;

    g0 = frame->pf_gains[0];
    g1 = frame->pf_gains[1];
    g2 = frame->pf_gains[2];

    x4 = data[-T - 2];
    x3 = data[-T - 1];
    x2 = data[-T];
    x1 = data[-T + 1];

    for (i = 0; i < len; i++) {
        x0 = data[i - T + 2];
        data[i] += g0 * x2        +
                   g1 * (x1 + x3) +
                   g2 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static void celt_postfilter(CeltContext *s, CeltFrame *frame)
{
    int len = s->blocksize * s->blocks;

    celt_postfilter_apply_transition(frame, frame->buf + 1024);

    frame->pf_period_old = frame->pf_period;
    memcpy(frame->pf_gains_old, frame->pf_gains, sizeof(frame->pf_gains));

    frame->pf_period = frame->pf_period_new;
    memcpy(frame->pf_gains, frame->pf_gains_new, sizeof(frame->pf_gains));

    if (len > CELT_OVERLAP) {
        celt_postfilter_apply_transition(frame, frame->buf + 1024 + CELT_OVERLAP);
        celt_postfilter_apply(frame, frame->buf + 1024 + 2 * CELT_OVERLAP,
                              len - 2 * CELT_OVERLAP);

        frame->pf_period_old = frame->pf_period;
        memcpy(frame->pf_gains_old, frame->pf_gains, sizeof(frame->pf_gains));
    }

    memmove(frame->buf, frame->buf + len, (1024 + CELT_OVERLAP / 2) * sizeof(float));
}

static int parse_postfilter(CeltContext *s, OpusRangeCoder *rc, int consumed)
{
    static const float postfilter_taps[3][3] = {
        { 0.3066406250f, 0.2170410156f, 0.1296386719f },
        { 0.4638671875f, 0.2680664062f, 0.0           },
        { 0.7998046875f, 0.1000976562f, 0.0           }
    };
    int i;

    memset(s->frame[0].pf_gains_new, 0, sizeof(s->frame[0].pf_gains_new));
    memset(s->frame[1].pf_gains_new, 0, sizeof(s->frame[1].pf_gains_new));

    if (s->startband == 0 && consumed + 16 <= s->framebits) {
        int has_postfilter = opus_rc_p2model(rc, 1);
        if (has_postfilter) {
            float gain;
            int tapset, octave, period;

            octave = opus_rc_unimodel(rc, 6);
            period = (16 << octave) + opus_getrawbits(rc, 4 + octave) - 1;
            gain   = 0.09375f * (opus_getrawbits(rc, 3) + 1);
            tapset = (opus_rc_tell(rc) + 2 <= s->framebits) ?
                     opus_rc_getsymbol(rc, celt_model_tapset) : 0;

            for (i = 0; i < 2; i++) {
                CeltFrame *frame = &s->frame[i];

                frame->pf_period_new = FFMAX(period, CELT_POSTFILTER_MINPERIOD);
                frame->pf_gains_new[0] = gain * postfilter_taps[tapset][0];
                frame->pf_gains_new[1] = gain * postfilter_taps[tapset][1];
                frame->pf_gains_new[2] = gain * postfilter_taps[tapset][2];
            }
        }

        consumed = opus_rc_tell(rc);
    }

    return consumed;
}

static void process_anticollapse(CeltContext *s, CeltFrame *frame, float *X)
{
    int i, j, k;

    for (i = s->startband; i < s->endband; i++) {
        int renormalize = 0;
        float *xptr;
        float prev[2];
        float Ediff, r;
        float thresh, sqrt_1;
        int depth;

        /* depth in 1/8 bits */
        depth = (1 + s->pulses[i]) / (celt_freq_range[i] << s->duration);
        thresh = exp2f(-1.0 - 0.125f * depth);
        sqrt_1 = 1.0f / sqrtf(celt_freq_range[i] << s->duration);

        xptr = X + (celt_freq_bands[i] << s->duration);

        prev[0] = frame->prev_energy[0][i];
        prev[1] = frame->prev_energy[1][i];
        if (s->coded_channels == 1) {
            CeltFrame *frame1 = &s->frame[1];

            prev[0] = FFMAX(prev[0], frame1->prev_energy[0][i]);
            prev[1] = FFMAX(prev[1], frame1->prev_energy[1][i]);
        }
        Ediff = frame->energy[i] - FFMIN(prev[0], prev[1]);
        Ediff = FFMAX(0, Ediff);

        /* r needs to be multiplied by 2 or 2*sqrt(2) depending on LM because
        short blocks don't have the same energy as long */
        r = exp2(1 - Ediff);
        if (s->duration == 3)
            r *= M_SQRT2;
        r = FFMIN(thresh, r) * sqrt_1;
        for (k = 0; k < 1 << s->duration; k++) {
            /* Detect collapse */
            if (!(frame->collapse_masks[i] & 1 << k)) {
                /* Fill with noise */
                for (j = 0; j < celt_freq_range[i]; j++)
                    xptr[(j << s->duration) + k] = (celt_rng(s) & 0x8000) ? r : -r;
                renormalize = 1;
            }
        }

        /* We just added some energy, so we need to renormalize */
        if (renormalize)
            celt_renormalize_vector(xptr, celt_freq_range[i] << s->duration, 1.0f);
    }
}

static void celt_decode_bands(CeltContext *s, OpusRangeCoder *rc)
{
    float lowband_scratch[8 * 22];
    float norm[2 * 8 * 100];

    int totalbits = (s->framebits << 3) - s->anticollapse_bit;

    int update_lowband = 1;
    int lowband_offset = 0;

    int i, j;

    memset(s->coeffs, 0, sizeof(s->coeffs));

    for (i = s->startband; i < s->endband; i++) {
        int band_offset = celt_freq_bands[i] << s->duration;
        int band_size   = celt_freq_range[i] << s->duration;
        float *X = s->coeffs[0] + band_offset;
        float *Y = (s->coded_channels == 2) ? s->coeffs[1] + band_offset : NULL;

        int consumed = opus_rc_tell_frac(rc);
        float *norm2 = norm + 8 * 100;
        int effective_lowband = -1;
        unsigned int cm[2];
        int b;

        /* Compute how many bits we want to allocate to this band */
        if (i != s->startband)
            s->remaining -= consumed;
        s->remaining2 = totalbits - consumed - 1;
        if (i <= s->codedbands - 1) {
            int curr_balance = s->remaining / FFMIN(3, s->codedbands-i);
            b = av_clip_uintp2(FFMIN(s->remaining2 + 1, s->pulses[i] + curr_balance), 14);
        } else
            b = 0;

        if (celt_freq_bands[i] - celt_freq_range[i] >= celt_freq_bands[s->startband] &&
            (update_lowband || lowband_offset == 0))
            lowband_offset = i;

        /* Get a conservative estimate of the collapse_mask's for the bands we're
        going to be folding from. */
        if (lowband_offset != 0 && (s->spread != CELT_SPREAD_AGGRESSIVE ||
                                    s->blocks > 1 || s->tf_change[i] < 0)) {
            int foldstart, foldend;

            /* This ensures we never repeat spectral content within one band */
            effective_lowband = FFMAX(celt_freq_bands[s->startband],
                                      celt_freq_bands[lowband_offset] - celt_freq_range[i]);
            foldstart = lowband_offset;
            while (celt_freq_bands[--foldstart] > effective_lowband);
            foldend = lowband_offset - 1;
            while (celt_freq_bands[++foldend] < effective_lowband + celt_freq_range[i]);

            cm[0] = cm[1] = 0;
            for (j = foldstart; j < foldend; j++) {
                cm[0] |= s->frame[0].collapse_masks[j];
                cm[1] |= s->frame[s->coded_channels - 1].collapse_masks[j];
            }
        } else
            /* Otherwise, we'll be using the LCG to fold, so all blocks will (almost
            always) be non-zero.*/
            cm[0] = cm[1] = (1 << s->blocks) - 1;

        if (s->dualstereo && i == s->intensitystereo) {
            /* Switch off dual stereo to do intensity */
            s->dualstereo = 0;
            for (j = celt_freq_bands[s->startband] << s->duration; j < band_offset; j++)
                norm[j] = (norm[j] + norm2[j]) / 2;
        }

        if (s->dualstereo) {
            cm[0] = celt_decode_band(s, rc, i, X, NULL, band_size, b / 2, s->blocks,
                                     effective_lowband != -1 ? norm + (effective_lowband << s->duration) : NULL, s->duration,
            norm + band_offset, 0, 1.0f, lowband_scratch, cm[0]);

            cm[1] = celt_decode_band(s, rc, i, Y, NULL, band_size, b/2, s->blocks,
                                     effective_lowband != -1 ? norm2 + (effective_lowband << s->duration) : NULL, s->duration,
            norm2 + band_offset, 0, 1.0f, lowband_scratch, cm[1]);
        } else {
            cm[0] = celt_decode_band(s, rc, i, X, Y, band_size, b, s->blocks,
            effective_lowband != -1 ? norm + (effective_lowband << s->duration) : NULL, s->duration,
            norm + band_offset, 0, 1.0f, lowband_scratch, cm[0]|cm[1]);

            cm[1] = cm[0];
        }

        s->frame[0].collapse_masks[i]                     = (uint8_t)cm[0];
        s->frame[s->coded_channels - 1].collapse_masks[i] = (uint8_t)cm[1];
        s->remaining += s->pulses[i] + consumed;

        /* Update the folding position only as long as we have 1 bit/sample depth */
        update_lowband = (b > band_size << 3);
    }
}

int ff_celt_decode_frame(CeltContext *s, OpusRangeCoder *rc,
                         float **output, int coded_channels, int frame_size,
                         int startband,  int endband)
{
    int i, j;

    int consumed;           // bits of entropy consumed thus far for this frame
    int silence = 0;
    int transient = 0;
    int anticollapse = 0;
    IMDCT15Context *imdct;
    float imdct_scale = 1.0;

    if (coded_channels != 1 && coded_channels != 2) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid number of coded channels: %d\n",
               coded_channels);
        return AVERROR_INVALIDDATA;
    }
    if (startband < 0 || startband > endband || endband > CELT_MAX_BANDS) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid start/end band: %d %d\n",
               startband, endband);
        return AVERROR_INVALIDDATA;
    }

    s->flushed        = 0;
    s->coded_channels = coded_channels;
    s->startband      = startband;
    s->endband        = endband;
    s->framebits      = rc->rb.bytes * 8;

    s->duration = av_log2(frame_size / CELT_SHORT_BLOCKSIZE);
    if (s->duration > CELT_MAX_LOG_BLOCKS ||
        frame_size != CELT_SHORT_BLOCKSIZE * (1 << s->duration)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid CELT frame size: %d\n",
               frame_size);
        return AVERROR_INVALIDDATA;
    }

    if (!s->output_channels)
        s->output_channels = coded_channels;

    memset(s->frame[0].collapse_masks, 0, sizeof(s->frame[0].collapse_masks));
    memset(s->frame[1].collapse_masks, 0, sizeof(s->frame[1].collapse_masks));

    consumed = opus_rc_tell(rc);

    /* obtain silence flag */
    if (consumed >= s->framebits)
        silence = 1;
    else if (consumed == 1)
        silence = opus_rc_p2model(rc, 15);


    if (silence) {
        consumed = s->framebits;
        rc->total_read_bits += s->framebits - opus_rc_tell(rc);
    }

    /* obtain post-filter options */
    consumed = parse_postfilter(s, rc, consumed);

    /* obtain transient flag */
    if (s->duration != 0 && consumed+3 <= s->framebits)
        transient = opus_rc_p2model(rc, 3);

    s->blocks    = transient ? 1 << s->duration : 1;
    s->blocksize = frame_size / s->blocks;

    imdct = s->imdct[transient ? 0 : s->duration];

    if (coded_channels == 1) {
        for (i = 0; i < CELT_MAX_BANDS; i++)
            s->frame[0].energy[i] = FFMAX(s->frame[0].energy[i], s->frame[1].energy[i]);
    }

    celt_decode_coarse_energy(s, rc);
    celt_decode_tf_changes   (s, rc, transient);
    celt_decode_allocation   (s, rc);
    celt_decode_fine_energy  (s, rc);
    celt_decode_bands        (s, rc);

    if (s->anticollapse_bit)
        anticollapse = opus_getrawbits(rc, 1);

    celt_decode_final_energy(s, rc, s->framebits - opus_rc_tell(rc));

    /* apply anti-collapse processing and denormalization to
     * each coded channel */
    for (i = 0; i < s->coded_channels; i++) {
        CeltFrame *frame = &s->frame[i];

        if (anticollapse)
            process_anticollapse(s, frame, s->coeffs[i]);

        celt_denormalize(s, frame, s->coeffs[i]);
    }

    /* stereo -> mono downmix */
    if (s->output_channels < s->coded_channels) {
        s->dsp->vector_fmac_scalar(s->coeffs[0], s->coeffs[1], 1.0, FFALIGN(frame_size, 16));
        imdct_scale = 0.5;
    } else if (s->output_channels > s->coded_channels)
        memcpy(s->coeffs[1], s->coeffs[0], frame_size * sizeof(float));

    if (silence) {
        for (i = 0; i < 2; i++) {
            CeltFrame *frame = &s->frame[i];

            for (j = 0; j < FF_ARRAY_ELEMS(frame->energy); j++)
                frame->energy[j] = CELT_ENERGY_SILENCE;
        }
        memset(s->coeffs, 0, sizeof(s->coeffs));
    }

    /* transform and output for each output channel */
    for (i = 0; i < s->output_channels; i++) {
        CeltFrame *frame = &s->frame[i];
        float m = frame->deemph_coeff;

        /* iMDCT and overlap-add */
        for (j = 0; j < s->blocks; j++) {
            float *dst  = frame->buf + 1024 + j * s->blocksize;

            imdct->imdct_half(imdct, dst + CELT_OVERLAP / 2, s->coeffs[i] + j,
                              s->blocks, imdct_scale);
            s->dsp->vector_fmul_window(dst, dst, dst + CELT_OVERLAP / 2,
                                      celt_window, CELT_OVERLAP / 2);
        }

        /* postfilter */
        celt_postfilter(s, frame);

        /* deemphasis and output scaling */
        for (j = 0; j < frame_size; j++) {
            float tmp = frame->buf[1024 - frame_size + j] + m;
            m = tmp * CELT_DEEMPH_COEFF;
            output[i][j] = tmp / 32768.;
        }
        frame->deemph_coeff = m;
    }

    if (coded_channels == 1)
        memcpy(s->frame[1].energy, s->frame[0].energy, sizeof(s->frame[0].energy));

    for (i = 0; i < 2; i++ ) {
        CeltFrame *frame = &s->frame[i];

        if (!transient) {
            memcpy(frame->prev_energy[1], frame->prev_energy[0], sizeof(frame->prev_energy[0]));
            memcpy(frame->prev_energy[0], frame->energy,         sizeof(frame->prev_energy[0]));
        } else {
            for (j = 0; j < CELT_MAX_BANDS; j++)
                frame->prev_energy[0][j] = FFMIN(frame->prev_energy[0][j], frame->energy[j]);
        }

        for (j = 0; j < s->startband; j++) {
            frame->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            frame->energy[j]         = 0.0;
        }
        for (j = s->endband; j < CELT_MAX_BANDS; j++) {
            frame->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            frame->energy[j]         = 0.0;
        }
    }

    s->seed = rc->range;

    return 0;
}

void ff_celt_flush(CeltContext *s)
{
    int i, j;

    if (s->flushed)
        return;

    for (i = 0; i < 2; i++) {
        CeltFrame *frame = &s->frame[i];

        for (j = 0; j < CELT_MAX_BANDS; j++)
            frame->prev_energy[0][j] = frame->prev_energy[1][j] = CELT_ENERGY_SILENCE;

        memset(frame->energy, 0, sizeof(frame->energy));
        memset(frame->buf,    0, sizeof(frame->buf));

        memset(frame->pf_gains,     0, sizeof(frame->pf_gains));
        memset(frame->pf_gains_old, 0, sizeof(frame->pf_gains_old));
        memset(frame->pf_gains_new, 0, sizeof(frame->pf_gains_new));

        frame->deemph_coeff = 0.0;
    }
    s->seed = 0;

    s->flushed = 1;
}

void ff_celt_free(CeltContext **ps)
{
    CeltContext *s = *ps;
    int i;

    if (!s)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(s->imdct); i++)
        ff_imdct15_uninit(&s->imdct[i]);

    av_freep(&s->dsp);
    av_freep(ps);
}

int ff_celt_init(AVCodecContext *avctx, CeltContext **ps, int output_channels)
{
    CeltContext *s;
    int i, ret;

    if (output_channels != 1 && output_channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of output channels: %d\n",
               output_channels);
        return AVERROR(EINVAL);
    }

    s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    s->avctx           = avctx;
    s->output_channels = output_channels;

    for (i = 0; i < FF_ARRAY_ELEMS(s->imdct); i++) {
        ret = ff_imdct15_init(&s->imdct[i], i + 3);
        if (ret < 0)
            goto fail;
    }

    s->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->dsp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ff_celt_flush(s);

    *ps = s;

    return 0;
fail:
    ff_celt_free(&s);
    return ret;
}
