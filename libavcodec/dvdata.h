/*
 * Constants for DV codec
 * Copyright (c) 2002 Fabrice Bellard
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
 * @file libavcodec/dvdata.h
 * Constants for DV codec.
 */

#ifndef AVCODEC_DVDATA_H
#define AVCODEC_DVDATA_H

#include "libavutil/rational.h"
#include "avcodec.h"

typedef struct DVwork_chunk {
    uint16_t  buf_offset;
    uint16_t  mb_coordinates[5];
} DVwork_chunk;

/*
 * DVprofile is used to express the differences between various
 * DV flavors. For now it's primarily used for differentiating
 * 525/60 and 625/50, but the plans are to use it for various
 * DV specs as well (e.g. SMPTE314M vs. IEC 61834).
 */
typedef struct DVprofile {
    int              dsf;                   /* value of the dsf in the DV header */
    int              video_stype;           /* stype for VAUX source pack */
    int              frame_size;            /* total size of one frame in bytes */
    int              difseg_size;           /* number of DIF segments per DIF channel */
    int              n_difchan;             /* number of DIF channels per frame */
    AVRational       time_base;             /* 1/framerate */
    int              ltc_divisor;           /* FPS from the LTS standpoint */
    int              height;                /* picture height in pixels */
    int              width;                 /* picture width in pixels */
    AVRational       sar[2];                /* sample aspect ratios for 4:3 and 16:9 */
    DVwork_chunk    *work_chunks;           /* each thread gets its own chunk of frame to work on */
    uint32_t        *idct_factor;           /* set of iDCT factor tables */
    enum PixelFormat pix_fmt;               /* picture pixel format */
    int              bpm;                   /* blocks per macroblock */
    const uint8_t   *block_sizes;           /* AC block sizes, in bits */
    int              audio_stride;          /* size of audio_shuffle table */
    int              audio_min_samples[3];  /* min amount of audio samples */
                                            /* for 48kHz, 44.1kHz and 32kHz */
    int              audio_samples_dist[5]; /* how many samples are supposed to be */
                                            /* in each frame in a 5 frames window */
    const uint8_t  (*audio_shuffle)[9];     /* PCM shuffling table */
} DVprofile;

#define NB_DV_VLC 409

/*
 * There's a catch about the following three tables: the mapping they establish
 * between (run, level) and vlc is not 1-1. So you have to watch out for that
 * when building misc. tables. E.g. (1, 0) can be either 0x7cf or 0x1f82.
 */
static const uint16_t dv_vlc_bits[409] = {
 0x0000, 0x0002, 0x0007, 0x0008, 0x0009, 0x0014, 0x0015, 0x0016,
 0x0017, 0x0030, 0x0031, 0x0032, 0x0033, 0x0068, 0x0069, 0x006a,
 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x00e0, 0x00e1, 0x00e2,
 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7, 0x00e8, 0x00e9, 0x00ea,
 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x01e0, 0x01e1, 0x01e2,
 0x01e3, 0x01e4, 0x01e5, 0x01e6, 0x01e7, 0x01e8, 0x01e9, 0x01ea,
 0x01eb, 0x01ec, 0x01ed, 0x01ee, 0x01ef, 0x03e0, 0x03e1, 0x03e2,
 0x03e3, 0x03e4, 0x03e5, 0x03e6, 0x07ce, 0x07cf, 0x07d0, 0x07d1,
 0x07d2, 0x07d3, 0x07d4, 0x07d5, 0x0fac, 0x0fad, 0x0fae, 0x0faf,
 0x0fb0, 0x0fb1, 0x0fb2, 0x0fb3, 0x0fb4, 0x0fb5, 0x0fb6, 0x0fb7,
 0x0fb8, 0x0fb9, 0x0fba, 0x0fbb, 0x0fbc, 0x0fbd, 0x0fbe, 0x0fbf,
 0x1f80, 0x1f81, 0x1f82, 0x1f83, 0x1f84, 0x1f85, 0x1f86, 0x1f87,
 0x1f88, 0x1f89, 0x1f8a, 0x1f8b, 0x1f8c, 0x1f8d, 0x1f8e, 0x1f8f,
 0x1f90, 0x1f91, 0x1f92, 0x1f93, 0x1f94, 0x1f95, 0x1f96, 0x1f97,
 0x1f98, 0x1f99, 0x1f9a, 0x1f9b, 0x1f9c, 0x1f9d, 0x1f9e, 0x1f9f,
 0x1fa0, 0x1fa1, 0x1fa2, 0x1fa3, 0x1fa4, 0x1fa5, 0x1fa6, 0x1fa7,
 0x1fa8, 0x1fa9, 0x1faa, 0x1fab, 0x1fac, 0x1fad, 0x1fae, 0x1faf,
 0x1fb0, 0x1fb1, 0x1fb2, 0x1fb3, 0x1fb4, 0x1fb5, 0x1fb6, 0x1fb7,
 0x1fb8, 0x1fb9, 0x1fba, 0x1fbb, 0x1fbc, 0x1fbd, 0x1fbe, 0x1fbf,
 0x7f00, 0x7f01, 0x7f02, 0x7f03, 0x7f04, 0x7f05, 0x7f06, 0x7f07,
 0x7f08, 0x7f09, 0x7f0a, 0x7f0b, 0x7f0c, 0x7f0d, 0x7f0e, 0x7f0f,
 0x7f10, 0x7f11, 0x7f12, 0x7f13, 0x7f14, 0x7f15, 0x7f16, 0x7f17,
 0x7f18, 0x7f19, 0x7f1a, 0x7f1b, 0x7f1c, 0x7f1d, 0x7f1e, 0x7f1f,
 0x7f20, 0x7f21, 0x7f22, 0x7f23, 0x7f24, 0x7f25, 0x7f26, 0x7f27,
 0x7f28, 0x7f29, 0x7f2a, 0x7f2b, 0x7f2c, 0x7f2d, 0x7f2e, 0x7f2f,
 0x7f30, 0x7f31, 0x7f32, 0x7f33, 0x7f34, 0x7f35, 0x7f36, 0x7f37,
 0x7f38, 0x7f39, 0x7f3a, 0x7f3b, 0x7f3c, 0x7f3d, 0x7f3e, 0x7f3f,
 0x7f40, 0x7f41, 0x7f42, 0x7f43, 0x7f44, 0x7f45, 0x7f46, 0x7f47,
 0x7f48, 0x7f49, 0x7f4a, 0x7f4b, 0x7f4c, 0x7f4d, 0x7f4e, 0x7f4f,
 0x7f50, 0x7f51, 0x7f52, 0x7f53, 0x7f54, 0x7f55, 0x7f56, 0x7f57,
 0x7f58, 0x7f59, 0x7f5a, 0x7f5b, 0x7f5c, 0x7f5d, 0x7f5e, 0x7f5f,
 0x7f60, 0x7f61, 0x7f62, 0x7f63, 0x7f64, 0x7f65, 0x7f66, 0x7f67,
 0x7f68, 0x7f69, 0x7f6a, 0x7f6b, 0x7f6c, 0x7f6d, 0x7f6e, 0x7f6f,
 0x7f70, 0x7f71, 0x7f72, 0x7f73, 0x7f74, 0x7f75, 0x7f76, 0x7f77,
 0x7f78, 0x7f79, 0x7f7a, 0x7f7b, 0x7f7c, 0x7f7d, 0x7f7e, 0x7f7f,
 0x7f80, 0x7f81, 0x7f82, 0x7f83, 0x7f84, 0x7f85, 0x7f86, 0x7f87,
 0x7f88, 0x7f89, 0x7f8a, 0x7f8b, 0x7f8c, 0x7f8d, 0x7f8e, 0x7f8f,
 0x7f90, 0x7f91, 0x7f92, 0x7f93, 0x7f94, 0x7f95, 0x7f96, 0x7f97,
 0x7f98, 0x7f99, 0x7f9a, 0x7f9b, 0x7f9c, 0x7f9d, 0x7f9e, 0x7f9f,
 0x7fa0, 0x7fa1, 0x7fa2, 0x7fa3, 0x7fa4, 0x7fa5, 0x7fa6, 0x7fa7,
 0x7fa8, 0x7fa9, 0x7faa, 0x7fab, 0x7fac, 0x7fad, 0x7fae, 0x7faf,
 0x7fb0, 0x7fb1, 0x7fb2, 0x7fb3, 0x7fb4, 0x7fb5, 0x7fb6, 0x7fb7,
 0x7fb8, 0x7fb9, 0x7fba, 0x7fbb, 0x7fbc, 0x7fbd, 0x7fbe, 0x7fbf,
 0x7fc0, 0x7fc1, 0x7fc2, 0x7fc3, 0x7fc4, 0x7fc5, 0x7fc6, 0x7fc7,
 0x7fc8, 0x7fc9, 0x7fca, 0x7fcb, 0x7fcc, 0x7fcd, 0x7fce, 0x7fcf,
 0x7fd0, 0x7fd1, 0x7fd2, 0x7fd3, 0x7fd4, 0x7fd5, 0x7fd6, 0x7fd7,
 0x7fd8, 0x7fd9, 0x7fda, 0x7fdb, 0x7fdc, 0x7fdd, 0x7fde, 0x7fdf,
 0x7fe0, 0x7fe1, 0x7fe2, 0x7fe3, 0x7fe4, 0x7fe5, 0x7fe6, 0x7fe7,
 0x7fe8, 0x7fe9, 0x7fea, 0x7feb, 0x7fec, 0x7fed, 0x7fee, 0x7fef,
 0x7ff0, 0x7ff1, 0x7ff2, 0x7ff3, 0x7ff4, 0x7ff5, 0x7ff6, 0x7ff7,
 0x7ff8, 0x7ff9, 0x7ffa, 0x7ffb, 0x7ffc, 0x7ffd, 0x7ffe, 0x7fff,
 0x0006,
};

static const uint8_t dv_vlc_len[409] = {
  2,  3,  4,  4,  4,  5,  5,  5,
  5,  6,  6,  6,  6,  7,  7,  7,
  7,  7,  7,  7,  7,  8,  8,  8,
  8,  8,  8,  8,  8,  8,  8,  8,
  8,  8,  8,  8,  8,  9,  9,  9,
  9,  9,  9,  9,  9,  9,  9,  9,
  9,  9,  9,  9,  9, 10, 10, 10,
 10, 10, 10, 10, 11, 11, 11, 11,
 11, 11, 11, 11, 12, 12, 12, 12,
 12, 12, 12, 12, 12, 12, 12, 12,
 12, 12, 12, 12, 12, 12, 12, 12,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 13, 13, 13, 13, 13, 13, 13, 13,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
 15, 15, 15, 15, 15, 15, 15, 15,
  4,
};

static const uint8_t dv_vlc_run[409] = {
  0,  0,  1,  0,  0,  2,  1,  0,
  0,  3,  4,  0,  0,  5,  6,  2,
  1,  1,  0,  0,  0,  7,  8,  9,
 10,  3,  4,  2,  1,  1,  1,  0,
  0,  0,  0,  0,  0, 11, 12, 13,
 14,  5,  6,  3,  4,  2,  2,  1,
  0,  0,  0,  0,  0,  5,  3,  3,
  2,  1,  1,  1,  0,  1,  6,  4,
  3,  1,  1,  1,  2,  3,  4,  5,
  7,  8,  9, 10,  7,  8,  4,  3,
  2,  2,  2,  2,  2,  1,  1,  1,
  0,  1,  2,  3,  4,  5,  6,  7,
  8,  9, 10, 11, 12, 13, 14, 15,
 16, 17, 18, 19, 20, 21, 22, 23,
 24, 25, 26, 27, 28, 29, 30, 31,
 32, 33, 34, 35, 36, 37, 38, 39,
 40, 41, 42, 43, 44, 45, 46, 47,
 48, 49, 50, 51, 52, 53, 54, 55,
 56, 57, 58, 59, 60, 61, 62, 63,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,
127,
};

static const uint8_t dv_vlc_level[409] = {
   1,   2,   1,   3,   4,   1,   2,   5,
   6,   1,   1,   7,   8,   1,   1,   2,
   3,   4,   9,  10,  11,   1,   1,   1,
   1,   2,   2,   3,   5,   6,   7,  12,
  13,  14,  15,  16,  17,   1,   1,   1,
   1,   2,   2,   3,   3,   4,   5,   8,
  18,  19,  20,  21,  22,   3,   4,   5,
   6,   9,  10,  11,   0,   0,   3,   4,
   6,  12,  13,  14,   0,   0,   0,   0,
   2,   2,   2,   2,   3,   3,   5,   7,
   7,   8,   9,  10,  11,  15,  16,  17,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   1,   2,   3,   4,   5,   6,   7,
   8,   9,  10,  11,  12,  13,  14,  15,
  16,  17,  18,  19,  20,  21,  22,  23,
  24,  25,  26,  27,  28,  29,  30,  31,
  32,  33,  34,  35,  36,  37,  38,  39,
  40,  41,  42,  43,  44,  45,  46,  47,
  48,  49,  50,  51,  52,  53,  54,  55,
  56,  57,  58,  59,  60,  61,  62,  63,
  64,  65,  66,  67,  68,  69,  70,  71,
  72,  73,  74,  75,  76,  77,  78,  79,
  80,  81,  82,  83,  84,  85,  86,  87,
  88,  89,  90,  91,  92,  93,  94,  95,
  96,  97,  98,  99, 100, 101, 102, 103,
 104, 105, 106, 107, 108, 109, 110, 111,
 112, 113, 114, 115, 116, 117, 118, 119,
 120, 121, 122, 123, 124, 125, 126, 127,
 128, 129, 130, 131, 132, 133, 134, 135,
 136, 137, 138, 139, 140, 141, 142, 143,
 144, 145, 146, 147, 148, 149, 150, 151,
 152, 153, 154, 155, 156, 157, 158, 159,
 160, 161, 162, 163, 164, 165, 166, 167,
 168, 169, 170, 171, 172, 173, 174, 175,
 176, 177, 178, 179, 180, 181, 182, 183,
 184, 185, 186, 187, 188, 189, 190, 191,
 192, 193, 194, 195, 196, 197, 198, 199,
 200, 201, 202, 203, 204, 205, 206, 207,
 208, 209, 210, 211, 212, 213, 214, 215,
 216, 217, 218, 219, 220, 221, 222, 223,
 224, 225, 226, 227, 228, 229, 230, 231,
 232, 233, 234, 235, 236, 237, 238, 239,
 240, 241, 242, 243, 244, 245, 246, 247,
 248, 249, 250, 251, 252, 253, 254, 255,
   0,
};

/* unquant tables (not used directly) */
static const uint8_t dv_quant_shifts[22][4] = {
  { 3,3,4,4 },
  { 3,3,4,4 },
  { 2,3,3,4 },
  { 2,3,3,4 },
  { 2,2,3,3 },
  { 2,2,3,3 },
  { 1,2,2,3 },
  { 1,2,2,3 },
  { 1,1,2,2 },
  { 1,1,2,2 },
  { 0,1,1,2 },
  { 0,1,1,2 },
  { 0,0,1,1 },
  { 0,0,1,1 },
  { 0,0,0,1 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
};

static const uint8_t dv_quant_offset[4] = { 6,  3,  0,  1 };
static const uint8_t dv_quant_areas[4]  = { 6, 21, 43, 64 };

/* quantization quanta by QNO for DV100 */
static const uint8_t dv100_qstep[16] = {
    1, /* QNO = 0 and 1 both have no quantization */
    1,
    2, 3, 4, 5, 6, 7, 8, 16, 18, 20, 22, 24, 28, 52
};

/* DV25/50 DCT coefficient weights and inverse weights */
/* created by dvtables.py */
static const int dv_weight_bits = 18;
static const int dv_weight_88[64] = {
 131072, 257107, 257107, 242189, 252167, 242189, 235923, 237536,
 237536, 235923, 229376, 231390, 223754, 231390, 229376, 222935,
 224969, 217965, 217965, 224969, 222935, 200636, 218652, 211916,
 212325, 211916, 218652, 200636, 188995, 196781, 205965, 206433,
 206433, 205965, 196781, 188995, 185364, 185364, 200636, 200704,
 200636, 185364, 185364, 174609, 180568, 195068, 195068, 180568,
 174609, 170091, 175557, 189591, 175557, 170091, 165371, 170627,
 170627, 165371, 160727, 153560, 160727, 144651, 144651, 136258,
};
static const int dv_weight_248[64] = {
 131072, 242189, 257107, 237536, 229376, 200636, 242189, 223754,
 224969, 196781, 262144, 242189, 229376, 200636, 257107, 237536,
 211916, 185364, 235923, 217965, 229376, 211916, 206433, 180568,
 242189, 223754, 224969, 196781, 211916, 185364, 235923, 217965,
 200704, 175557, 222935, 205965, 200636, 185364, 195068, 170627,
 229376, 211916, 206433, 180568, 200704, 175557, 222935, 205965,
 175557, 153560, 188995, 174609, 165371, 144651, 200636, 185364,
 195068, 170627, 175557, 153560, 188995, 174609, 165371, 144651,
};
static const int dv_iweight_bits = 14;
static const int dv_iweight_88[64] = {
 32768, 16710, 16710, 17735, 17015, 17735, 18197, 18079,
 18079, 18197, 18725, 18559, 19196, 18559, 18725, 19284,
 19108, 19692, 19692, 19108, 19284, 21400, 19645, 20262,
 20214, 20262, 19645, 21400, 22733, 21845, 20867, 20815,
 20815, 20867, 21845, 22733, 23173, 23173, 21400, 21400,
 21400, 23173, 23173, 24600, 23764, 22017, 22017, 23764,
 24600, 25267, 24457, 22672, 24457, 25267, 25971, 25191,
 25191, 25971, 26715, 27962, 26715, 29642, 29642, 31536,
};
static const int dv_iweight_248[64] = {
 32768, 17735, 16710, 18079, 18725, 21400, 17735, 19196,
 19108, 21845, 16384, 17735, 18725, 21400, 16710, 18079,
 20262, 23173, 18197, 19692, 18725, 20262, 20815, 23764,
 17735, 19196, 19108, 21845, 20262, 23173, 18197, 19692,
 21400, 24457, 19284, 20867, 21400, 23173, 22017, 25191,
 18725, 20262, 20815, 23764, 21400, 24457, 19284, 20867,
 24457, 27962, 22733, 24600, 25971, 29642, 21400, 23173,
 22017, 25191, 24457, 27962, 22733, 24600, 25971, 29642,
};

/**
 * The "inverse" DV100 weights are actually just the spec weights (zig-zagged).
 */
static const int dv_iweight_1080_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  45,  45,  42,  42,
     42,  45,  45,  48,  46,  43,  43,  46,
     48,  49,  48,  44,  48,  49, 101,  98,
     98, 101, 104, 109, 104, 116, 116, 123,
};
static const int dv_iweight_1080_c[64] = {
    128,  16,  16,  17,  17,  17,  25,  25,
     25,  25,  26,  25,  26,  25,  26,  26,
     26,  27,  27,  26,  26,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  91,  91,  84,  84,
     84,  91,  91,  96,  93,  86,  86,  93,
     96, 197, 191, 177, 191, 197, 203, 197,
    197, 203, 209, 219, 209, 232, 232, 246,
};
static const int dv_iweight_720_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  68,  68,  63,  63,
     63,  68,  68,  96,  92,  86,  86,  92,
     96,  98,  96,  88,  96,  98, 202, 196,
    196, 202, 208, 218, 208, 232, 232, 246,
};
static const int dv_iweight_720_c[64] = {
    128,  24,  24,  26,  26,  26,  36,  36,
     36,  36,  36,  36,  38,  36,  36,  38,
     38,  38,  38,  38,  38,  84,  76,  80,
     80,  80,  76,  84,  88,  86,  82,  82,
     82,  82,  86,  88, 182, 182, 168, 168,
    168, 182, 182, 192, 186, 192, 172, 186,
    192, 394, 382, 354, 382, 394, 406, 394,
    394, 406, 418, 438, 418, 464, 464, 492,
};

static const uint8_t dv_audio_shuffle525[10][9] = {
  {  0, 30, 60, 20, 50, 80, 10, 40, 70 }, /* 1st channel */
  {  6, 36, 66, 26, 56, 86, 16, 46, 76 },
  { 12, 42, 72,  2, 32, 62, 22, 52, 82 },
  { 18, 48, 78,  8, 38, 68, 28, 58, 88 },
  { 24, 54, 84, 14, 44, 74,  4, 34, 64 },

  {  1, 31, 61, 21, 51, 81, 11, 41, 71 }, /* 2nd channel */
  {  7, 37, 67, 27, 57, 87, 17, 47, 77 },
  { 13, 43, 73,  3, 33, 63, 23, 53, 83 },
  { 19, 49, 79,  9, 39, 69, 29, 59, 89 },
  { 25, 55, 85, 15, 45, 75,  5, 35, 65 },
};

static const uint8_t dv_audio_shuffle625[12][9] = {
  {   0,  36,  72,  26,  62,  98,  16,  52,  88}, /* 1st channel */
  {   6,  42,  78,  32,  68, 104,  22,  58,  94},
  {  12,  48,  84,   2,  38,  74,  28,  64, 100},
  {  18,  54,  90,   8,  44,  80,  34,  70, 106},
  {  24,  60,  96,  14,  50,  86,   4,  40,  76},
  {  30,  66, 102,  20,  56,  92,  10,  46,  82},

  {   1,  37,  73,  27,  63,  99,  17,  53,  89}, /* 2nd channel */
  {   7,  43,  79,  33,  69, 105,  23,  59,  95},
  {  13,  49,  85,   3,  39,  75,  29,  65, 101},
  {  19,  55,  91,   9,  45,  81,  35,  71, 107},
  {  25,  61,  97,  15,  51,  87,   5,  41,  77},
  {  31,  67, 103,  21,  57,  93,  11,  47,  83},
};

static const av_unused int dv_audio_frequency[3] = {
    48000, 44100, 32000,
};

/* macroblock bit budgets */
static const uint8_t block_sizes_dv2550[8] = {
    112, 112, 112, 112, 80, 80, 0, 0,
};

static const uint8_t block_sizes_dv100[8] = {
    80, 80, 80, 80, 80, 80, 64, 64,
};

static DVwork_chunk work_chunks_dv25pal   [1*12*27];
static DVwork_chunk work_chunks_dv25pal411[1*12*27];
static DVwork_chunk work_chunks_dv25ntsc  [1*10*27];
static DVwork_chunk work_chunks_dv50pal   [2*12*27];
static DVwork_chunk work_chunks_dv50ntsc  [2*10*27];
static DVwork_chunk work_chunks_dv100palp [2*12*27];
static DVwork_chunk work_chunks_dv100ntscp[2*10*27];
static DVwork_chunk work_chunks_dv100pali [4*12*27];
static DVwork_chunk work_chunks_dv100ntsci[4*10*27];

static uint32_t dv_idct_factor_sd    [2*2*22*64];
static uint32_t dv_idct_factor_hd1080[2*4*16*64];
static uint32_t dv_idct_factor_hd720 [2*4*16*64];

static const DVprofile dv_profiles[] = {
    { .dsf = 0,
      .video_stype = 0x0,
      .frame_size = 120000,        /* IEC 61834, SMPTE-314M - 525/60 (NTSC) */
      .difseg_size = 10,
      .n_difchan = 1,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 480,
      .width = 720,
      .sar = {{10, 11}, {40, 33}},
      .work_chunks = &work_chunks_dv25ntsc[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV411P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x0,
      .frame_size = 144000,        /* IEC 61834 - 625/50 (PAL) */
      .difseg_size = 12,
      .n_difchan = 1,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{59, 54}, {118, 81}},
      .work_chunks = &work_chunks_dv25pal[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV420P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 1,
      .video_stype = 0x0,
      .frame_size = 144000,        /* SMPTE-314M - 625/50 (PAL) */
      .difseg_size = 12,
      .n_difchan = 1,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{59, 54}, {118, 81}},
      .work_chunks = &work_chunks_dv25pal411[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV411P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x4,
      .frame_size = 240000,        /* SMPTE-314M - 525/60 (NTSC) 50 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO50" */
      .n_difchan = 2,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 480,
      .width = 720,
      .sar = {{10, 11}, {40, 33}},
      .work_chunks = &work_chunks_dv50ntsc[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x4,
      .frame_size = 288000,        /* SMPTE-314M - 625/50 (PAL) 50 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO50" */
      .n_difchan = 2,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{59, 54}, {118, 81}},
      .work_chunks = &work_chunks_dv50pal[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x14,
      .frame_size = 480000,        /* SMPTE-370M - 1080i60 100 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO HD" */
      .n_difchan = 4,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 1080,
      .width = 1280,
      .sar = {{1, 1}, {1, 1}},
      .work_chunks = &work_chunks_dv100ntsci[0],
      .idct_factor = &dv_idct_factor_hd1080[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x14,
      .frame_size = 576000,        /* SMPTE-370M - 1080i50 100 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO HD" */
      .n_difchan = 4,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 1080,
      .width = 1440,
      .sar = {{1, 1}, {1, 1}},
      .work_chunks = &work_chunks_dv100pali[0],
      .idct_factor = &dv_idct_factor_hd1080[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x18,
      .frame_size = 240000,        /* SMPTE-370M - 720p60 100 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO HD" */
      .n_difchan = 2,
      .time_base = { 1001, 60000 },
      .ltc_divisor = 60,
      .height = 720,
      .width = 960,
      .sar = {{1, 1}, {1, 1}},
      .work_chunks = &work_chunks_dv100ntscp[0],
      .idct_factor = &dv_idct_factor_hd720[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x18,
      .frame_size = 288000,        /* SMPTE-370M - 720p50 100 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO HD" */
      .n_difchan = 2,
      .time_base = { 1, 50 },
      .ltc_divisor = 50,
      .height = 720,
      .width = 960,
      .sar = {{1, 1}, {1, 1}},
      .work_chunks = &work_chunks_dv100palp[0],
      .idct_factor = &dv_idct_factor_hd720[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    }
};

enum dv_section_type {
     dv_sect_header  = 0x1f,
     dv_sect_subcode = 0x3f,
     dv_sect_vaux    = 0x56,
     dv_sect_audio   = 0x76,
     dv_sect_video   = 0x96,
};

enum dv_pack_type {
     dv_header525     = 0x3f, /* see dv_write_pack for important details on */
     dv_header625     = 0xbf, /* these two packs */
     dv_timecode      = 0x13,
     dv_audio_source  = 0x50,
     dv_audio_control = 0x51,
     dv_audio_recdate = 0x52,
     dv_audio_rectime = 0x53,
     dv_video_source  = 0x60,
     dv_video_control = 0x61,
     dv_video_recdate = 0x62,
     dv_video_rectime = 0x63,
     dv_unknown_pack  = 0xff,
};

#define DV_PROFILE_IS_HD(p) ((p)->video_stype & 0x10)
#define DV_PROFILE_IS_1080i50(p) (((p)->video_stype == 0x14) && ((p)->dsf == 1))
#define DV_PROFILE_IS_720p50(p)  (((p)->video_stype == 0x18) && ((p)->dsf == 1))

/* minimum number of bytes to read from a DV stream in order to
   determine the profile */
#define DV_PROFILE_BYTES (6*80) /* 6 DIF blocks */

/**
 * largest possible DV frame, in bytes (1080i50)
 */
#define DV_MAX_FRAME_SIZE 576000

/**
 * maximum number of blocks per macroblock in any DV format
 */
#define DV_MAX_BPM 8

static inline const DVprofile* dv_frame_profile(const uint8_t* frame)
{
   int i;

   int dsf = (frame[3] & 0x80) >> 7;

   int stype = frame[80*5 + 48 + 3] & 0x1f;

   /* 576i50 25Mbps 4:1:1 is a special case */
   if (dsf == 1 && stype == 0 && frame[5] & 0x07) {
       return &dv_profiles[2];
   }

   for (i=0; i<FF_ARRAY_ELEMS(dv_profiles); i++)
       if (dsf == dv_profiles[i].dsf && stype == dv_profiles[i].video_stype)
           return &dv_profiles[i];

   return NULL;
}

static const DVprofile* dv_codec_profile(AVCodecContext* codec)
{
    int i;

    for (i=0; i<FF_ARRAY_ELEMS(dv_profiles); i++)
       if (codec->height  == dv_profiles[i].height  &&
           codec->pix_fmt == dv_profiles[i].pix_fmt &&
           codec->width   == dv_profiles[i].width)
               return &dv_profiles[i];

    return NULL;
}

static inline int dv_write_dif_id(enum dv_section_type t, uint8_t chan_num,
                                  uint8_t seq_num, uint8_t dif_num,
                                  uint8_t* buf)
{
    buf[0] = (uint8_t)t;       /* Section type */
    buf[1] = (seq_num  << 4) | /* DIF seq number 0-9 for 525/60; 0-11 for 625/50 */
             (chan_num << 3) | /* FSC: for 50Mb/s 0 - first channel; 1 - second */
             7;                /* reserved -- always 1 */
    buf[2] = dif_num;          /* DIF block number Video: 0-134, Audio: 0-8 */
    return 3;
}


static inline int dv_write_ssyb_id(uint8_t syb_num, uint8_t fr, uint8_t* buf)
{
    if (syb_num == 0 || syb_num == 6) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* AP3 (Subcode application ID) */
                 0x0f;       /* reserved -- always 1 */
    }
    else if (syb_num == 11) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 0x7f;       /* reserved -- always 1 */
    }
    else {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* APT (Track application ID) */
                 0x0f;       /* reserved -- always 1 */
    }
    buf[1] = 0xf0 |            /* reserved -- always 1 */
             (syb_num & 0x0f); /* SSYB number 0 - 11   */
    buf[2] = 0xff;             /* reserved -- always 1 */
    return 3;
}

#endif /* AVCODEC_DVDATA_H */
