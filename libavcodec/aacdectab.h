/*
 * AAC decoder data
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
 * @file aacdectab.h
 * AAC decoder data
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef FFMPEG_AACDECTAB_H
#define FFMPEG_AACDECTAB_H

#include "aac.h"

#include <stdint.h>

/* @name swb_offsets
 * Sample offset into the window indicating the beginning of a scalefactor
 * window band
 *
 * scalefactor window band - term for scalefactor bands within a window,
 * given in Table 4.110 to Table 4.128.
 *
 * scalefactor band - a set of spectral coefficients which are scaled by one
 * scalefactor. In case of EIGHT_SHORT_SEQUENCE and grouping a scalefactor band
 * may contain several scalefactor window bands of corresponding frequency. For
 * all other window_sequences scalefactor bands and scalefactor window bands are
 * identical.
 * @{
 */

static const uint16_t swb_offset_1024_96[] = {
      0,   4,   8,  12,  16,  20,  24,  28,
     32,  36,  40,  44,  48,  52,  56,  64,
     72,  80,  88,  96, 108, 120, 132, 144,
    156, 172, 188, 212, 240, 276, 320, 384,
    448, 512, 576, 640, 704, 768, 832, 896,
    960, 1024
};

static const uint16_t swb_offset_128_96[] = {
    0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 92, 128
};

static const uint16_t swb_offset_1024_64[] = {
      0,   4,   8,  12,  16,  20,  24,  28,
     32,  36,  40,  44,  48,  52,  56,  64,
     72,  80,  88, 100, 112, 124, 140, 156,
    172, 192, 216, 240, 268, 304, 344, 384,
    424, 464, 504, 544, 584, 624, 664, 704,
    744, 784, 824, 864, 904, 944, 984, 1024
};

static const uint16_t swb_offset_1024_48[] = {
      0,   4,   8,  12,  16,  20,  24,  28,
     32,  36,  40,  48,  56,  64,  72,  80,
     88,  96, 108, 120, 132, 144, 160, 176,
    196, 216, 240, 264, 292, 320, 352, 384,
    416, 448, 480, 512, 544, 576, 608, 640,
    672, 704, 736, 768, 800, 832, 864, 896,
    928, 1024
};

static const uint16_t swb_offset_128_48[] = {
     0,   4,   8,  12,  16,  20,  28,  36,
    44,  56,  68,  80,  96, 112, 128
};

static const uint16_t swb_offset_1024_32[] = {
      0,   4,   8,  12,  16,  20,  24,  28,
     32,  36,  40,  48,  56,  64,  72,  80,
     88,  96, 108, 120, 132, 144, 160, 176,
    196, 216, 240, 264, 292, 320, 352, 384,
    416, 448, 480, 512, 544, 576, 608, 640,
    672, 704, 736, 768, 800, 832, 864, 896,
    928, 960, 992, 1024
};

static const uint16_t swb_offset_1024_24[] = {
      0,   4,   8,  12,  16,  20,  24,  28,
     32,  36,  40,  44,  52,  60,  68,  76,
     84,  92, 100, 108, 116, 124, 136, 148,
    160, 172, 188, 204, 220, 240, 260, 284,
    308, 336, 364, 396, 432, 468, 508, 552,
    600, 652, 704, 768, 832, 896, 960, 1024
};

static const uint16_t swb_offset_128_24[] = {
     0,   4,   8,  12,  16,  20,  24,  28,
    36,  44,  52,  64,  76,  92, 108, 128
};

static const uint16_t swb_offset_1024_16[] = {
      0,   8,  16,  24,  32,  40,  48,  56,
     64,  72,  80,  88, 100, 112, 124, 136,
    148, 160, 172, 184, 196, 212, 228, 244,
    260, 280, 300, 320, 344, 368, 396, 424,
    456, 492, 532, 572, 616, 664, 716, 772,
    832, 896, 960, 1024
};

static const uint16_t swb_offset_128_16[] = {
     0,   4,   8,  12,  16,  20,  24,  28,
    32,  40,  48,  60,  72,  88, 108, 128
};

static const uint16_t swb_offset_1024_8[] = {
      0,  12,  24,  36,  48,  60,  72,  84,
     96, 108, 120, 132, 144, 156, 172, 188,
    204, 220, 236, 252, 268, 288, 308, 328,
    348, 372, 396, 420, 448, 476, 508, 544,
    580, 620, 664, 712, 764, 820, 880, 944,
    1024
};

static const uint16_t swb_offset_128_8[] = {
     0,   4,   8,  12,  16,  20,  24,  28,
    36,  44,  52,  60,  72,  88, 108, 128
};

static const uint16_t *swb_offset_1024[] = {
    swb_offset_1024_96, swb_offset_1024_96, swb_offset_1024_64,
    swb_offset_1024_48, swb_offset_1024_48, swb_offset_1024_32,
    swb_offset_1024_24, swb_offset_1024_24, swb_offset_1024_16,
    swb_offset_1024_16, swb_offset_1024_16, swb_offset_1024_8
};

static const uint16_t *swb_offset_128[] = {
    /* The last entry on the following row is swb_offset_128_64 but is a
       duplicate of swb_offset_128_96. */
    swb_offset_128_96, swb_offset_128_96, swb_offset_128_96,
    swb_offset_128_48, swb_offset_128_48, swb_offset_128_48,
    swb_offset_128_24, swb_offset_128_24, swb_offset_128_16,
    swb_offset_128_16, swb_offset_128_16, swb_offset_128_8
};

// @}

#endif /* FFMPEG_AACDECTAB_H */
