/*
 *
 * Copyright (C) 2002 the xine project
 * Copyright (C) 2002 the ffmpeg project
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
 *
 * (SVQ1 Decoder)
 * Ported to mplayer by Arpi <arpi@thot.banki.hu>
 * Ported to libavcodec by Nick Kurshev <nickols_k@mail.ru>
 *
 * SVQ1 Encoder (c) 2004 Mike Melanson <melanson@pcisys.net>
 */

/**
 * @file svq1.c
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */


//#define DEBUG_SVQ1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "bswap.h"

#undef NDEBUG
#include <assert.h>

extern const uint8_t mvtab[33][2];

static VLC svq1_block_type;
static VLC svq1_motion_component;
static VLC svq1_intra_multistage[6];
static VLC svq1_inter_multistage[6];
static VLC svq1_intra_mean;
static VLC svq1_inter_mean;

#define SVQ1_BLOCK_SKIP         0
#define SVQ1_BLOCK_INTER        1
#define SVQ1_BLOCK_INTER_4V     2
#define SVQ1_BLOCK_INTRA        3

typedef struct SVQ1Context {
    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independent of MpegEncContext, so this will be removed then (FIXME/XXX)
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame picture;
    AVFrame current_picture;
    AVFrame last_picture;
    PutBitContext pb;
    GetBitContext gb;

    PutBitContext reorder_pb[6]; //why ooh why this sick breadth first order, everything is slower and more complex

    int frame_width;
    int frame_height;

    /* Y plane block dimensions */
    int y_block_width;
    int y_block_height;

    /* U & V plane (C planes) block dimensions */
    int c_block_width;
    int c_block_height;

    uint16_t *mb_type;
    uint32_t *dummy;
    int16_t (*motion_val8[3])[2];
    int16_t (*motion_val16[3])[2];

    int64_t rd_total;
} SVQ1Context;

/* motion vector (prediction) */
typedef struct svq1_pmv_s {
  int           x;
  int           y;
} svq1_pmv_t;

#include "svq1_cb.h"
#include "svq1_vlc.h"

static const uint16_t checksum_table[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
  0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
  0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
  0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
  0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
  0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
  0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
  0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
  0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
  0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
  0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
  0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
  0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
  0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
  0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
  0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
  0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
  0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
  0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
  0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
  0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
  0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
  0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
  0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

static const uint8_t string_table[256] = {
  0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54,
  0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
  0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06,
  0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
  0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0,
  0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
  0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2,
  0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
  0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9,
  0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
  0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B,
  0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
  0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D,
  0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
  0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F,
  0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
  0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB,
  0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
  0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9,
  0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
  0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F,
  0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
  0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D,
  0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
  0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26,
  0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
  0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74,
  0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
  0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82,
  0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
  0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0,
  0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

#define SVQ1_PROCESS_VECTOR()\
    for (; level > 0; i++) {\
      /* process next depth */\
      if (i == m) {\
        m = n;\
        if (--level == 0)\
          break;\
      }\
      /* divide block if next bit set */\
      if (get_bits (bitbuf, 1) == 0)\
        break;\
      /* add child nodes */\
      list[n++] = list[i];\
      list[n++] = list[i] + (((level & 1) ? pitch : 1) << ((level / 2) + 1));\
    }

#define SVQ1_ADD_CODEBOOK()\
          /* add codebook entries to vector */\
          for (j=0; j < stages; j++) {\
            n3  = codebook[entries[j]] ^ 0x80808080;\
            n1 += ((n3 & 0xFF00FF00) >> 8);\
            n2 +=  (n3 & 0x00FF00FF);\
          }\
\
          /* clip to [0..255] */\
          if (n1 & 0xFF00FF00) {\
            n3  = ((( n1 >> 15) & 0x00010001) | 0x01000100) - 0x00010001;\
            n1 += 0x7F007F00;\
            n1 |= (((~n1 >> 15) & 0x00010001) | 0x01000100) - 0x00010001;\
            n1 &= (n3 & 0x00FF00FF);\
          }\
\
          if (n2 & 0xFF00FF00) {\
            n3  = ((( n2 >> 15) & 0x00010001) | 0x01000100) - 0x00010001;\
            n2 += 0x7F007F00;\
            n2 |= (((~n2 >> 15) & 0x00010001) | 0x01000100) - 0x00010001;\
            n2 &= (n3 & 0x00FF00FF);\
          }

#define SVQ1_DO_CODEBOOK_INTRA()\
      for (y=0; y < height; y++) {\
        for (x=0; x < (width / 4); x++, codebook++) {\
        n1 = n4;\
        n2 = n4;\
        SVQ1_ADD_CODEBOOK()\
        /* store result */\
        dst[x] = (n1 << 8) | n2;\
        }\
        dst += (pitch / 4);\
      }

#define SVQ1_DO_CODEBOOK_NONINTRA()\
      for (y=0; y < height; y++) {\
        for (x=0; x < (width / 4); x++, codebook++) {\
        n3 = dst[x];\
        /* add mean value to vector */\
        n1 = ((n3 & 0xFF00FF00) >> 8) + n4;\
        n2 =  (n3 & 0x00FF00FF)          + n4;\
        SVQ1_ADD_CODEBOOK()\
        /* store result */\
        dst[x] = (n1 << 8) | n2;\
        }\
        dst += (pitch / 4);\
      }

#define SVQ1_CALC_CODEBOOK_ENTRIES(cbook)\
      codebook = (const uint32_t *) cbook[level];\
      bit_cache = get_bits (bitbuf, 4*stages);\
      /* calculate codebook entries for this vector */\
      for (j=0; j < stages; j++) {\
        entries[j] = (((bit_cache >> (4*(stages - j - 1))) & 0xF) + 16*j) << (level + 1);\
      }\
      mean -= (stages * 128);\
      n4    = ((mean + (mean >> 31)) << 16) | (mean & 0xFFFF);

static int svq1_decode_block_intra (GetBitContext *bitbuf, uint8_t *pixels, int pitch ) {
  uint32_t    bit_cache;
  uint8_t    *list[63];
  uint32_t   *dst;
  const uint32_t *codebook;
  int         entries[6];
  int         i, j, m, n;
  int         mean, stages;
  unsigned    x, y, width, height, level;
  uint32_t    n1, n2, n3, n4;

  /* initialize list for breadth first processing of vectors */
  list[0] = pixels;

  /* recursively process vector */
  for (i=0, m=1, n=1, level=5; i < n; i++) {
    SVQ1_PROCESS_VECTOR();

    /* destination address and vector size */
    dst = (uint32_t *) list[i];
    width = 1 << ((4 + level) /2);
    height = 1 << ((3 + level) /2);

    /* get number of stages (-1 skips vector, 0 for mean only) */
    stages = get_vlc2(bitbuf, svq1_intra_multistage[level].table, 3, 3) - 1;

    if (stages == -1) {
        for (y=0; y < height; y++) {
          memset (&dst[y*(pitch / 4)], 0, width);
        }
      continue;                 /* skip vector */
    }

    if ((stages > 0) && (level >= 4)) {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error (svq1_decode_block_intra): invalid vector: stages=%i level=%i\n",stages,level);
#endif
      return -1;        /* invalid vector */
    }

    mean = get_vlc2(bitbuf, svq1_intra_mean.table, 8, 3);

    if (stages == 0) {
      for (y=0; y < height; y++) {
        memset (&dst[y*(pitch / 4)], mean, width);
      }
    } else {
      SVQ1_CALC_CODEBOOK_ENTRIES(svq1_intra_codebooks);
      SVQ1_DO_CODEBOOK_INTRA()
    }
  }

  return 0;
}

static int svq1_decode_block_non_intra (GetBitContext *bitbuf, uint8_t *pixels, int pitch ) {
  uint32_t    bit_cache;
  uint8_t    *list[63];
  uint32_t   *dst;
  const uint32_t *codebook;
  int         entries[6];
  int         i, j, m, n;
  int         mean, stages;
  int         x, y, width, height, level;
  uint32_t    n1, n2, n3, n4;

  /* initialize list for breadth first processing of vectors */
  list[0] = pixels;

  /* recursively process vector */
  for (i=0, m=1, n=1, level=5; i < n; i++) {
    SVQ1_PROCESS_VECTOR();

    /* destination address and vector size */
    dst = (uint32_t *) list[i];
    width = 1 << ((4 + level) /2);
    height = 1 << ((3 + level) /2);

    /* get number of stages (-1 skips vector, 0 for mean only) */
    stages = get_vlc2(bitbuf, svq1_inter_multistage[level].table, 3, 2) - 1;

    if (stages == -1) continue; /* skip vector */

    if ((stages > 0) && (level >= 4)) {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error (svq1_decode_block_non_intra): invalid vector: stages=%i level=%i\n",stages,level);
#endif
      return -1;        /* invalid vector */
    }

    mean = get_vlc2(bitbuf, svq1_inter_mean.table, 9, 3) - 256;

    SVQ1_CALC_CODEBOOK_ENTRIES(svq1_inter_codebooks);
    SVQ1_DO_CODEBOOK_NONINTRA()
  }
  return 0;
}

static int svq1_decode_motion_vector (GetBitContext *bitbuf, svq1_pmv_t *mv, svq1_pmv_t **pmv) {
  int        diff;
  int        i;

  for (i=0; i < 2; i++) {

    /* get motion code */
    diff = get_vlc2(bitbuf, svq1_motion_component.table, 7, 2);
    if(diff<0)
        return -1;
    else if(diff){
        if(get_bits1(bitbuf)) diff= -diff;
    }

    /* add median of motion vector predictors and clip result */
    if (i == 1)
      mv->y = ((diff + mid_pred(pmv[0]->y, pmv[1]->y, pmv[2]->y)) << 26) >> 26;
    else
      mv->x = ((diff + mid_pred(pmv[0]->x, pmv[1]->x, pmv[2]->x)) << 26) >> 26;
  }

  return 0;
}

static void svq1_skip_block (uint8_t *current, uint8_t *previous, int pitch, int x, int y) {
  uint8_t *src;
  uint8_t *dst;
  int      i;

  src = &previous[x + y*pitch];
  dst = current;

  for (i=0; i < 16; i++) {
    memcpy (dst, src, 16);
    src += pitch;
    dst += pitch;
  }
}

static int svq1_motion_inter_block (MpegEncContext *s, GetBitContext *bitbuf,
                               uint8_t *current, uint8_t *previous, int pitch,
                               svq1_pmv_t *motion, int x, int y) {
  uint8_t    *src;
  uint8_t    *dst;
  svq1_pmv_t  mv;
  svq1_pmv_t *pmv[3];
  int         result;

  /* predict and decode motion vector */
  pmv[0] = &motion[0];
  if (y == 0) {
    pmv[1] =
    pmv[2] = pmv[0];
  }
  else {
    pmv[1] = &motion[(x / 8) + 2];
    pmv[2] = &motion[(x / 8) + 4];
  }

  result = svq1_decode_motion_vector (bitbuf, &mv, pmv);

  if (result != 0)
    return result;

  motion[0].x                =
  motion[(x / 8) + 2].x      =
  motion[(x / 8) + 3].x      = mv.x;
  motion[0].y                =
  motion[(x / 8) + 2].y      =
  motion[(x / 8) + 3].y      = mv.y;

  if(y + (mv.y >> 1)<0)
     mv.y= 0;
  if(x + (mv.x >> 1)<0)
     mv.x= 0;

#if 0
  int w= (s->width+15)&~15;
  int h= (s->height+15)&~15;
  if(x + (mv.x >> 1)<0 || y + (mv.y >> 1)<0 || x + (mv.x >> 1) + 16 > w || y + (mv.y >> 1) + 16> h)
      av_log(s->avctx, AV_LOG_INFO, "%d %d %d %d\n", x, y, x + (mv.x >> 1), y + (mv.y >> 1));
#endif

  src = &previous[(x + (mv.x >> 1)) + (y + (mv.y >> 1))*pitch];
  dst = current;

  s->dsp.put_pixels_tab[0][((mv.y & 1) << 1) | (mv.x & 1)](dst,src,pitch,16);

  return 0;
}

static int svq1_motion_inter_4v_block (MpegEncContext *s, GetBitContext *bitbuf,
                                  uint8_t *current, uint8_t *previous, int pitch,
                                  svq1_pmv_t *motion,int x, int y) {
  uint8_t    *src;
  uint8_t    *dst;
  svq1_pmv_t  mv;
  svq1_pmv_t *pmv[4];
  int         i, result;

  /* predict and decode motion vector (0) */
  pmv[0] = &motion[0];
  if (y == 0) {
    pmv[1] =
    pmv[2] = pmv[0];
  }
  else {
    pmv[1] = &motion[(x / 8) + 2];
    pmv[2] = &motion[(x / 8) + 4];
  }

  result = svq1_decode_motion_vector (bitbuf, &mv, pmv);

  if (result != 0)
    return result;

  /* predict and decode motion vector (1) */
  pmv[0] = &mv;
  if (y == 0) {
    pmv[1] =
    pmv[2] = pmv[0];
  }
  else {
    pmv[1] = &motion[(x / 8) + 3];
  }
  result = svq1_decode_motion_vector (bitbuf, &motion[0], pmv);

  if (result != 0)
    return result;

  /* predict and decode motion vector (2) */
  pmv[1] = &motion[0];
  pmv[2] = &motion[(x / 8) + 1];

  result = svq1_decode_motion_vector (bitbuf, &motion[(x / 8) + 2], pmv);

  if (result != 0)
    return result;

  /* predict and decode motion vector (3) */
  pmv[2] = &motion[(x / 8) + 2];
  pmv[3] = &motion[(x / 8) + 3];

  result = svq1_decode_motion_vector (bitbuf, pmv[3], pmv);

  if (result != 0)
    return result;

  /* form predictions */
  for (i=0; i < 4; i++) {
    int mvx= pmv[i]->x + (i&1)*16;
    int mvy= pmv[i]->y + (i>>1)*16;

    ///XXX /FIXME cliping or padding?
    if(y + (mvy >> 1)<0)
       mvy= 0;
    if(x + (mvx >> 1)<0)
       mvx= 0;

#if 0
  int w= (s->width+15)&~15;
  int h= (s->height+15)&~15;
  if(x + (mvx >> 1)<0 || y + (mvy >> 1)<0 || x + (mvx >> 1) + 8 > w || y + (mvy >> 1) + 8> h)
      av_log(s->avctx, AV_LOG_INFO, "%d %d %d %d\n", x, y, x + (mvx >> 1), y + (mvy >> 1));
#endif
    src = &previous[(x + (mvx >> 1)) + (y + (mvy >> 1))*pitch];
    dst = current;

    s->dsp.put_pixels_tab[1][((mvy & 1) << 1) | (mvx & 1)](dst,src,pitch,8);

    /* select next block */
    if (i & 1) {
      current  += 8*(pitch - 1);
    } else {
      current  += 8;
    }
  }

  return 0;
}

static int svq1_decode_delta_block (MpegEncContext *s, GetBitContext *bitbuf,
                        uint8_t *current, uint8_t *previous, int pitch,
                        svq1_pmv_t *motion, int x, int y) {
  uint32_t block_type;
  int      result = 0;

  /* get block type */
  block_type = get_vlc2(bitbuf, svq1_block_type.table, 2, 2);

  /* reset motion vectors */
  if (block_type == SVQ1_BLOCK_SKIP || block_type == SVQ1_BLOCK_INTRA) {
    motion[0].x                 =
    motion[0].y                 =
    motion[(x / 8) + 2].x =
    motion[(x / 8) + 2].y =
    motion[(x / 8) + 3].x =
    motion[(x / 8) + 3].y = 0;
  }

  switch (block_type) {
  case SVQ1_BLOCK_SKIP:
    svq1_skip_block (current, previous, pitch, x, y);
    break;

  case SVQ1_BLOCK_INTER:
    result = svq1_motion_inter_block (s, bitbuf, current, previous, pitch, motion, x, y);

    if (result != 0)
    {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error in svq1_motion_inter_block %i\n",result);
#endif
      break;
    }
    result = svq1_decode_block_non_intra (bitbuf, current, pitch);
    break;

  case SVQ1_BLOCK_INTER_4V:
    result = svq1_motion_inter_4v_block (s, bitbuf, current, previous, pitch, motion, x, y);

    if (result != 0)
    {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error in svq1_motion_inter_4v_block %i\n",result);
#endif
      break;
    }
    result = svq1_decode_block_non_intra (bitbuf, current, pitch);
    break;

  case SVQ1_BLOCK_INTRA:
    result = svq1_decode_block_intra (bitbuf, current, pitch);
    break;
  }

  return result;
}

/* standard video sizes */
static struct { int width; int height; } svq1_frame_size_table[8] = {
  { 160, 120 }, { 128,  96 }, { 176, 144 }, { 352, 288 },
  { 704, 576 }, { 240, 180 }, { 320, 240 }, {  -1,  -1 }
};

static uint16_t svq1_packet_checksum (uint8_t *data, int length, int value) {
  int i;

  for (i=0; i < length; i++) {
    value = checksum_table[data[i] ^ (value >> 8)] ^ ((value & 0xFF) << 8);
  }

  return value;
}

#if 0 /* unused, remove? */
static uint16_t svq1_component_checksum (uint16_t *pixels, int pitch,
                                         int width, int height, int value) {
  int x, y;

  for (y=0; y < height; y++) {
    for (x=0; x < width; x++) {
      value = checksum_table[pixels[x] ^ (value >> 8)] ^ ((value & 0xFF) << 8);
    }

    pixels += pitch;
  }

  return value;
}
#endif

#ifdef CONFIG_DECODERS
static void svq1_parse_string (GetBitContext *bitbuf, uint8_t *out) {
  uint8_t seed;
  int     i;

  out[0] = get_bits (bitbuf, 8);

  seed = string_table[out[0]];

  for (i=1; i <= out[0]; i++) {
    out[i] = get_bits (bitbuf, 8) ^ seed;
    seed   = string_table[out[i] ^ seed];
  }
}

static int svq1_decode_frame_header (GetBitContext *bitbuf,MpegEncContext *s) {
  int frame_size_code;
  int temporal_reference;

  temporal_reference = get_bits (bitbuf, 8);

  /* frame type */
  s->pict_type= get_bits (bitbuf, 2)+1;
  if(s->pict_type==4)
      return -1;

  if (s->pict_type == I_TYPE) {

    /* unknown fields */
    if (s->f_code == 0x50 || s->f_code == 0x60) {
      int csum = get_bits (bitbuf, 16);

      csum = svq1_packet_checksum ((uint8_t *)bitbuf->buffer, bitbuf->size_in_bits>>3, csum);

//      av_log(s->avctx, AV_LOG_INFO, "%s checksum (%02x) for packet data\n",
//              (csum == 0) ? "correct" : "incorrect", csum);
    }

    if ((s->f_code ^ 0x10) >= 0x50) {
      uint8_t msg[256];

      svq1_parse_string (bitbuf, msg);

      av_log(s->avctx, AV_LOG_INFO, "embedded message: \"%s\"\n", (char *) msg);
    }

    skip_bits (bitbuf, 2);
    skip_bits (bitbuf, 2);
    skip_bits1 (bitbuf);

    /* load frame size */
    frame_size_code = get_bits (bitbuf, 3);

    if (frame_size_code == 7) {
      /* load width, height (12 bits each) */
      s->width = get_bits (bitbuf, 12);
      s->height = get_bits (bitbuf, 12);

      if (!s->width || !s->height)
        return -1;
    } else {
      /* get width, height from table */
      s->width = svq1_frame_size_table[frame_size_code].width;
      s->height = svq1_frame_size_table[frame_size_code].height;
    }
  }

  /* unknown fields */
  if (get_bits (bitbuf, 1) == 1) {
    skip_bits1 (bitbuf);       /* use packet checksum if (1) */
    skip_bits1 (bitbuf);       /* component checksums after image data if (1) */

    if (get_bits (bitbuf, 2) != 0)
      return -1;
  }

  if (get_bits (bitbuf, 1) == 1) {
    skip_bits1 (bitbuf);
    skip_bits (bitbuf, 4);
    skip_bits1 (bitbuf);
    skip_bits (bitbuf, 2);

    while (get_bits (bitbuf, 1) == 1) {
      skip_bits (bitbuf, 8);
    }
  }

  return 0;
}

static int svq1_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
  MpegEncContext *s=avctx->priv_data;
  uint8_t        *current, *previous;
  int             result, i, x, y, width, height;
  AVFrame *pict = data;

  /* initialize bit buffer */
  init_get_bits(&s->gb,buf,buf_size*8);

  /* decode frame header */
  s->f_code = get_bits (&s->gb, 22);

  if ((s->f_code & ~0x70) || !(s->f_code & 0x60))
    return -1;

  /* swap some header bytes (why?) */
  if (s->f_code != 0x20) {
    uint32_t *src = (uint32_t *) (buf + 4);

    for (i=0; i < 4; i++) {
      src[i] = ((src[i] << 16) | (src[i] >> 16)) ^ src[7 - i];
    }
  }

  result = svq1_decode_frame_header (&s->gb, s);

  if (result != 0)
  {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error in svq1_decode_frame_header %i\n",result);
#endif
    return result;
  }

  //FIXME this avoids some confusion for "B frames" without 2 references
  //this should be removed after libavcodec can handle more flexible picture types & ordering
  if(s->pict_type==B_TYPE && s->last_picture_ptr==NULL) return buf_size;

  if(avctx->hurry_up && s->pict_type==B_TYPE) return buf_size;
  if(  (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type==B_TYPE)
     ||(avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type!=I_TYPE)
     || avctx->skip_frame >= AVDISCARD_ALL)
      return buf_size;

  if(MPV_frame_start(s, avctx) < 0)
      return -1;

  /* decode y, u and v components */
  for (i=0; i < 3; i++) {
    int linesize;
    if (i == 0) {
      width  = (s->width+15)&~15;
      height = (s->height+15)&~15;
      linesize= s->linesize;
    } else {
      if(s->flags&CODEC_FLAG_GRAY) break;
      width  = (s->width/4+15)&~15;
      height = (s->height/4+15)&~15;
      linesize= s->uvlinesize;
    }

    current  = s->current_picture.data[i];

    if(s->pict_type==B_TYPE){
        previous = s->next_picture.data[i];
    }else{
        previous = s->last_picture.data[i];
    }

    if (s->pict_type == I_TYPE) {
      /* keyframe */
      for (y=0; y < height; y+=16) {
        for (x=0; x < width; x+=16) {
          result = svq1_decode_block_intra (&s->gb, &current[x], linesize);
          if (result != 0)
          {
//#ifdef DEBUG_SVQ1
            av_log(s->avctx, AV_LOG_INFO, "Error in svq1_decode_block %i (keyframe)\n",result);
//#endif
            return result;
          }
        }
        current += 16*linesize;
      }
    } else {
      svq1_pmv_t pmv[width/8+3];
      /* delta frame */
      memset (pmv, 0, ((width / 8) + 3) * sizeof(svq1_pmv_t));

      for (y=0; y < height; y+=16) {
        for (x=0; x < width; x+=16) {
          result = svq1_decode_delta_block (s, &s->gb, &current[x], previous,
                                            linesize, pmv, x, y);
          if (result != 0)
          {
#ifdef DEBUG_SVQ1
    av_log(s->avctx, AV_LOG_INFO, "Error in svq1_decode_delta_block %i\n",result);
#endif
            return result;
          }
        }

        pmv[0].x =
        pmv[0].y = 0;

        current += 16*linesize;
      }
    }
  }

  *pict = *(AVFrame*)&s->current_picture;


  MPV_frame_end(s);

  *data_size=sizeof(AVFrame);
  return buf_size;
}

static int svq1_decode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    MPV_decode_defaults(s);

    s->avctx = avctx;
    s->width = (avctx->width+3)&~3;
    s->height = (avctx->height+3)&~3;
    s->codec_id= avctx->codec->id;
    avctx->pix_fmt = PIX_FMT_YUV410P;
    avctx->has_b_frames= 1; // not true, but DP frames and these behave like unidirectional b frames
    s->flags= avctx->flags;
    if (MPV_common_init(s) < 0) return -1;

    init_vlc(&svq1_block_type, 2, 4,
        &svq1_block_type_vlc[0][1], 2, 1,
        &svq1_block_type_vlc[0][0], 2, 1, 1);

    init_vlc(&svq1_motion_component, 7, 33,
        &mvtab[0][1], 2, 1,
        &mvtab[0][0], 2, 1, 1);

    for (i = 0; i < 6; i++) {
        init_vlc(&svq1_intra_multistage[i], 3, 8,
            &svq1_intra_multistage_vlc[i][0][1], 2, 1,
            &svq1_intra_multistage_vlc[i][0][0], 2, 1, 1);
        init_vlc(&svq1_inter_multistage[i], 3, 8,
            &svq1_inter_multistage_vlc[i][0][1], 2, 1,
            &svq1_inter_multistage_vlc[i][0][0], 2, 1, 1);
    }

    init_vlc(&svq1_intra_mean, 8, 256,
        &svq1_intra_mean_vlc[0][1], 4, 2,
        &svq1_intra_mean_vlc[0][0], 4, 2, 1);

    init_vlc(&svq1_inter_mean, 9, 512,
        &svq1_inter_mean_vlc[0][1], 4, 2,
        &svq1_inter_mean_vlc[0][0], 4, 2, 1);

    return 0;
}

static int svq1_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    MPV_common_end(s);
    return 0;
}
#endif /* CONFIG_DECODERS */

#ifdef CONFIG_ENCODERS
static void svq1_write_header(SVQ1Context *s, int frame_type)
{
    int i;

    /* frame code */
    put_bits(&s->pb, 22, 0x20);

    /* temporal reference (sure hope this is a "don't care") */
    put_bits(&s->pb, 8, 0x00);

    /* frame type */
    put_bits(&s->pb, 2, frame_type - 1);

    if (frame_type == I_TYPE) {

        /* no checksum since frame code is 0x20 */

        /* no embedded string either */

        /* output 5 unknown bits (2 + 2 + 1) */
        put_bits(&s->pb, 5, 2); /* 2 needed by quicktime decoder */

        for (i = 0; i < 7; i++)
        {
            if ((svq1_frame_size_table[i].width == s->frame_width) &&
                (svq1_frame_size_table[i].height == s->frame_height))
            {
                put_bits(&s->pb, 3, i);
                break;
            }
        }

        if (i == 7)
        {
            put_bits(&s->pb, 3, 7);
                put_bits(&s->pb, 12, s->frame_width);
                put_bits(&s->pb, 12, s->frame_height);
        }
    }

    /* no checksum or extra data (next 2 bits get 0) */
    put_bits(&s->pb, 2, 0);
}


#define QUALITY_THRESHOLD 100
#define THRESHOLD_MULTIPLIER 0.6

#if defined(HAVE_ALTIVEC)
#undef vector
#endif

static int encode_block(SVQ1Context *s, uint8_t *src, uint8_t *ref, uint8_t *decoded, int stride, int level, int threshold, int lambda, int intra){
    int count, y, x, i, j, split, best_mean, best_score, best_count;
    int best_vector[6];
    int block_sum[7]= {0, 0, 0, 0, 0, 0};
    int w= 2<<((level+2)>>1);
    int h= 2<<((level+1)>>1);
    int size=w*h;
    int16_t block[7][256];
    const int8_t *codebook_sum, *codebook;
    const uint16_t (*mean_vlc)[2];
    const uint8_t (*multistage_vlc)[2];

    best_score=0;
    //FIXME optimize, this doenst need to be done multiple times
    if(intra){
        codebook_sum= svq1_intra_codebook_sum[level];
        codebook= svq1_intra_codebooks[level];
        mean_vlc= svq1_intra_mean_vlc;
        multistage_vlc= svq1_intra_multistage_vlc[level];
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= src[x + y*stride];
                block[0][x + w*y]= v;
                best_score += v*v;
                block_sum[0] += v;
            }
        }
    }else{
        codebook_sum= svq1_inter_codebook_sum[level];
        codebook= svq1_inter_codebooks[level];
        mean_vlc= svq1_inter_mean_vlc + 256;
        multistage_vlc= svq1_inter_multistage_vlc[level];
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= src[x + y*stride] - ref[x + y*stride];
                block[0][x + w*y]= v;
                best_score += v*v;
                block_sum[0] += v;
            }
        }
    }

    best_count=0;
    best_score -= ((block_sum[0]*block_sum[0])>>(level+3));
    best_mean= (block_sum[0] + (size>>1)) >> (level+3);

    if(level<4){
        for(count=1; count<7; count++){
            int best_vector_score= INT_MAX;
            int best_vector_sum=-999, best_vector_mean=-999;
            const int stage= count-1;
            const int8_t *vector;

            for(i=0; i<16; i++){
                int sum= codebook_sum[stage*16 + i];
                int sqr, diff, score;

                vector = codebook + stage*size*16 + i*size;
                sqr = s->dsp.ssd_int8_vs_int16(vector, block[stage], size);
                diff= block_sum[stage] - sum;
                score= sqr - ((diff*(int64_t)diff)>>(level+3)); //FIXME 64bit slooow
                if(score < best_vector_score){
                    int mean= (diff + (size>>1)) >> (level+3);
                    assert(mean >-300 && mean<300);
                    mean= av_clip(mean, intra?0:-256, 255);
                    best_vector_score= score;
                    best_vector[stage]= i;
                    best_vector_sum= sum;
                    best_vector_mean= mean;
                }
            }
            assert(best_vector_mean != -999);
            vector= codebook + stage*size*16 + best_vector[stage]*size;
            for(j=0; j<size; j++){
                block[stage+1][j] = block[stage][j] - vector[j];
            }
            block_sum[stage+1]= block_sum[stage] - best_vector_sum;
            best_vector_score +=
                lambda*(+ 1 + 4*count
                        + multistage_vlc[1+count][1]
                        + mean_vlc[best_vector_mean][1]);

            if(best_vector_score < best_score){
                best_score= best_vector_score;
                best_count= count;
                best_mean= best_vector_mean;
            }
        }
    }

    split=0;
    if(best_score > threshold && level){
        int score=0;
        int offset= (level&1) ? stride*h/2 : w/2;
        PutBitContext backup[6];

        for(i=level-1; i>=0; i--){
            backup[i]= s->reorder_pb[i];
        }
        score += encode_block(s, src         , ref         , decoded         , stride, level-1, threshold>>1, lambda, intra);
        score += encode_block(s, src + offset, ref + offset, decoded + offset, stride, level-1, threshold>>1, lambda, intra);
        score += lambda;

        if(score < best_score){
            best_score= score;
            split=1;
        }else{
            for(i=level-1; i>=0; i--){
                s->reorder_pb[i]= backup[i];
            }
        }
    }
    if (level > 0)
        put_bits(&s->reorder_pb[level], 1, split);

    if(!split){
        assert((best_mean >= 0 && best_mean<256) || !intra);
        assert(best_mean >= -256 && best_mean<256);
        assert(best_count >=0 && best_count<7);
        assert(level<4 || best_count==0);

        /* output the encoding */
        put_bits(&s->reorder_pb[level],
            multistage_vlc[1 + best_count][1],
            multistage_vlc[1 + best_count][0]);
        put_bits(&s->reorder_pb[level], mean_vlc[best_mean][1],
            mean_vlc[best_mean][0]);

        for (i = 0; i < best_count; i++){
            assert(best_vector[i]>=0 && best_vector[i]<16);
            put_bits(&s->reorder_pb[level], 4, best_vector[i]);
        }

        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                decoded[x + y*stride]= src[x + y*stride] - block[best_count][x + w*y] + best_mean;
            }
        }
    }

    return best_score;
}


static int svq1_encode_plane(SVQ1Context *s, int plane, unsigned char *src_plane, unsigned char *ref_plane, unsigned char *decoded_plane,
    int width, int height, int src_stride, int stride)
{
    int x, y;
    int i;
    int block_width, block_height;
    int level;
    int threshold[6];
    const int lambda= (s->picture.quality*s->picture.quality) >> (2*FF_LAMBDA_SHIFT);

    /* figure out the acceptable level thresholds in advance */
    threshold[5] = QUALITY_THRESHOLD;
    for (level = 4; level >= 0; level--)
        threshold[level] = threshold[level + 1] * THRESHOLD_MULTIPLIER;

    block_width = (width + 15) / 16;
    block_height = (height + 15) / 16;

    if(s->picture.pict_type == P_TYPE){
        s->m.avctx= s->avctx;
        s->m.current_picture_ptr= &s->m.current_picture;
        s->m.last_picture_ptr   = &s->m.last_picture;
        s->m.last_picture.data[0]= ref_plane;
        s->m.linesize=
        s->m.last_picture.linesize[0]=
        s->m.new_picture.linesize[0]=
        s->m.current_picture.linesize[0]= stride;
        s->m.width= width;
        s->m.height= height;
        s->m.mb_width= block_width;
        s->m.mb_height= block_height;
        s->m.mb_stride= s->m.mb_width+1;
        s->m.b8_stride= 2*s->m.mb_width+1;
        s->m.f_code=1;
        s->m.pict_type= s->picture.pict_type;
        s->m.me_method= s->avctx->me_method;
        s->m.me.scene_change_score=0;
        s->m.flags= s->avctx->flags;
//        s->m.out_format = FMT_H263;
//        s->m.unrestricted_mv= 1;

        s->m.lambda= s->picture.quality;
        s->m.qscale= (s->m.lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        s->m.lambda2= (s->m.lambda*s->m.lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;

        if(!s->motion_val8[plane]){
            s->motion_val8 [plane]= av_mallocz((s->m.b8_stride*block_height*2 + 2)*2*sizeof(int16_t));
            s->motion_val16[plane]= av_mallocz((s->m.mb_stride*(block_height + 2) + 1)*2*sizeof(int16_t));
        }

        s->m.mb_type= s->mb_type;

        //dummies, to avoid segfaults
        s->m.current_picture.mb_mean=   (uint8_t *)s->dummy;
        s->m.current_picture.mb_var=    (uint16_t*)s->dummy;
        s->m.current_picture.mc_mb_var= (uint16_t*)s->dummy;
        s->m.current_picture.mb_type= s->dummy;

        s->m.current_picture.motion_val[0]= s->motion_val8[plane] + 2;
        s->m.p_mv_table= s->motion_val16[plane] + s->m.mb_stride + 1;
        s->m.dsp= s->dsp; //move
        ff_init_me(&s->m);

        s->m.me.dia_size= s->avctx->dia_size;
        s->m.first_slice_line=1;
        for (y = 0; y < block_height; y++) {
            uint8_t src[stride*16];

            s->m.new_picture.data[0]= src - y*16*stride; //ugly
            s->m.mb_y= y;

            for(i=0; i<16 && i + 16*y<height; i++){
                memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
                for(x=width; x<16*block_width; x++)
                    src[i*stride+x]= src[i*stride+x-1];
            }
            for(; i<16 && i + 16*y<16*block_height; i++)
                memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);

            for (x = 0; x < block_width; x++) {
                s->m.mb_x= x;
                ff_init_block_index(&s->m);
                ff_update_block_index(&s->m);

                ff_estimate_p_frame_motion(&s->m, x, y);
            }
            s->m.first_slice_line=0;
        }

        ff_fix_long_p_mvs(&s->m);
        ff_fix_long_mvs(&s->m, NULL, 0, s->m.p_mv_table, s->m.f_code, CANDIDATE_MB_TYPE_INTER, 0);
    }

    s->m.first_slice_line=1;
    for (y = 0; y < block_height; y++) {
        uint8_t src[stride*16];

        for(i=0; i<16 && i + 16*y<height; i++){
            memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
            for(x=width; x<16*block_width; x++)
                src[i*stride+x]= src[i*stride+x-1];
        }
        for(; i<16 && i + 16*y<16*block_height; i++)
            memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);

        s->m.mb_y= y;
        for (x = 0; x < block_width; x++) {
            uint8_t reorder_buffer[3][6][7*32];
            int count[3][6];
            int offset = y * 16 * stride + x * 16;
            uint8_t *decoded= decoded_plane + offset;
            uint8_t *ref= ref_plane + offset;
            int score[4]={0,0,0,0}, best;
            uint8_t temp[16*stride];

            if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 3000){ //FIXME check size
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }

            s->m.mb_x= x;
            ff_init_block_index(&s->m);
            ff_update_block_index(&s->m);

            if(s->picture.pict_type == I_TYPE || (s->m.mb_type[x + y*s->m.mb_stride]&CANDIDATE_MB_TYPE_INTRA)){
                for(i=0; i<6; i++){
                    init_put_bits(&s->reorder_pb[i], reorder_buffer[0][i], 7*32);
                }
                if(s->picture.pict_type == P_TYPE){
                    const uint8_t *vlc= svq1_block_type_vlc[SVQ1_BLOCK_INTRA];
                    put_bits(&s->reorder_pb[5], vlc[1], vlc[0]);
                    score[0]= vlc[1]*lambda;
                }
                score[0]+= encode_block(s, src+16*x, NULL, temp, stride, 5, 64, lambda, 1);
                for(i=0; i<6; i++){
                    count[0][i]= put_bits_count(&s->reorder_pb[i]);
                    flush_put_bits(&s->reorder_pb[i]);
                }
            }else
                score[0]= INT_MAX;

            best=0;

            if(s->picture.pict_type == P_TYPE){
                const uint8_t *vlc= svq1_block_type_vlc[SVQ1_BLOCK_INTER];
                int mx, my, pred_x, pred_y, dxy;
                int16_t *motion_ptr;

                motion_ptr= h263_pred_motion(&s->m, 0, 0, &pred_x, &pred_y);
                if(s->m.mb_type[x + y*s->m.mb_stride]&CANDIDATE_MB_TYPE_INTER){
                    for(i=0; i<6; i++)
                        init_put_bits(&s->reorder_pb[i], reorder_buffer[1][i], 7*32);

                    put_bits(&s->reorder_pb[5], vlc[1], vlc[0]);

                    s->m.pb= s->reorder_pb[5];
                    mx= motion_ptr[0];
                    my= motion_ptr[1];
                    assert(mx>=-32 && mx<=31);
                    assert(my>=-32 && my<=31);
                    assert(pred_x>=-32 && pred_x<=31);
                    assert(pred_y>=-32 && pred_y<=31);
                    ff_h263_encode_motion(&s->m, mx - pred_x, 1);
                    ff_h263_encode_motion(&s->m, my - pred_y, 1);
                    s->reorder_pb[5]= s->m.pb;
                    score[1] += lambda*put_bits_count(&s->reorder_pb[5]);

                    dxy= (mx&1) + 2*(my&1);

                    s->dsp.put_pixels_tab[0][dxy](temp+16, ref + (mx>>1) + stride*(my>>1), stride, 16);

                    score[1]+= encode_block(s, src+16*x, temp+16, decoded, stride, 5, 64, lambda, 0);
                    best= score[1] <= score[0];

                    vlc= svq1_block_type_vlc[SVQ1_BLOCK_SKIP];
                    score[2]= s->dsp.sse[0](NULL, src+16*x, ref, stride, 16);
                    score[2]+= vlc[1]*lambda;
                    if(score[2] < score[best] && mx==0 && my==0){
                        best=2;
                        s->dsp.put_pixels_tab[0][0](decoded, ref, stride, 16);
                        for(i=0; i<6; i++){
                            count[2][i]=0;
                        }
                        put_bits(&s->pb, vlc[1], vlc[0]);
                    }
                }

                if(best==1){
                    for(i=0; i<6; i++){
                        count[1][i]= put_bits_count(&s->reorder_pb[i]);
                        flush_put_bits(&s->reorder_pb[i]);
                    }
                }else{
                    motion_ptr[0                 ] = motion_ptr[1                 ]=
                    motion_ptr[2                 ] = motion_ptr[3                 ]=
                    motion_ptr[0+2*s->m.b8_stride] = motion_ptr[1+2*s->m.b8_stride]=
                    motion_ptr[2+2*s->m.b8_stride] = motion_ptr[3+2*s->m.b8_stride]=0;
                }
            }

            s->rd_total += score[best];

            for(i=5; i>=0; i--){
                ff_copy_bits(&s->pb, reorder_buffer[best][i], count[best][i]);
            }
            if(best==0){
                s->dsp.put_pixels_tab[0][0](decoded, temp, stride, 16);
            }
        }
        s->m.first_slice_line=0;
    }
    return 0;
}

static int svq1_encode_init(AVCodecContext *avctx)
{
    SVQ1Context * const s = avctx->priv_data;

    dsputil_init(&s->dsp, avctx);
    avctx->coded_frame= (AVFrame*)&s->picture;

    s->frame_width = avctx->width;
    s->frame_height = avctx->height;

    s->y_block_width = (s->frame_width + 15) / 16;
    s->y_block_height = (s->frame_height + 15) / 16;

    s->c_block_width = (s->frame_width / 4 + 15) / 16;
    s->c_block_height = (s->frame_height / 4 + 15) / 16;

    s->avctx= avctx;
    s->m.avctx= avctx;
    s->m.me.scratchpad= av_mallocz((avctx->width+64)*2*16*2*sizeof(uint8_t));
    s->m.me.map       = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->m.me.score_map = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->mb_type        = av_mallocz((s->y_block_width+1)*s->y_block_height*sizeof(int16_t));
    s->dummy          = av_mallocz((s->y_block_width+1)*s->y_block_height*sizeof(int32_t));
    h263_encode_init(&s->m); //mv_penalty

    return 0;
}

static int svq1_encode_frame(AVCodecContext *avctx, unsigned char *buf,
    int buf_size, void *data)
{
    SVQ1Context * const s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    AVFrame temp;
    int i;

    if(avctx->pix_fmt != PIX_FMT_YUV410P){
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel format\n");
        return -1;
    }

    if(!s->current_picture.data[0]){
        avctx->get_buffer(avctx, &s->current_picture);
        avctx->get_buffer(avctx, &s->last_picture);
    }

    temp= s->current_picture;
    s->current_picture= s->last_picture;
    s->last_picture= temp;

    init_put_bits(&s->pb, buf, buf_size);

    *p = *pict;
    p->pict_type = avctx->gop_size && avctx->frame_number % avctx->gop_size ? P_TYPE : I_TYPE;
    p->key_frame = p->pict_type == I_TYPE;

    svq1_write_header(s, p->pict_type);
    for(i=0; i<3; i++){
        if(svq1_encode_plane(s, i,
            s->picture.data[i], s->last_picture.data[i], s->current_picture.data[i],
            s->frame_width / (i?4:1), s->frame_height / (i?4:1),
            s->picture.linesize[i], s->current_picture.linesize[i]) < 0)
                return -1;
    }

//    align_put_bits(&s->pb);
    while(put_bits_count(&s->pb) & 31)
        put_bits(&s->pb, 1, 0);

    flush_put_bits(&s->pb);

    return (put_bits_count(&s->pb) / 8);
}

static int svq1_encode_end(AVCodecContext *avctx)
{
    SVQ1Context * const s = avctx->priv_data;
    int i;

    av_log(avctx, AV_LOG_DEBUG, "RD: %f\n", s->rd_total/(double)(avctx->width*avctx->height*avctx->frame_number));

    av_freep(&s->m.me.scratchpad);
    av_freep(&s->m.me.map);
    av_freep(&s->m.me.score_map);
    av_freep(&s->mb_type);
    av_freep(&s->dummy);

    for(i=0; i<3; i++){
        av_freep(&s->motion_val8[i]);
        av_freep(&s->motion_val16[i]);
    }

    return 0;
}

#endif //CONFIG_ENCODERS

#ifdef CONFIG_DECODERS
AVCodec svq1_decoder = {
    "svq1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SVQ1,
    sizeof(MpegEncContext),
    svq1_decode_init,
    NULL,
    svq1_decode_end,
    svq1_decode_frame,
    CODEC_CAP_DR1,
    .flush= ff_mpeg_flush,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_YUV410P, -1},
};
#endif

#ifdef CONFIG_ENCODERS

AVCodec svq1_encoder = {
    "svq1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SVQ1,
    sizeof(SVQ1Context),
    svq1_encode_init,
    svq1_encode_frame,
    svq1_encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_YUV410P, -1},
};

#endif //CONFIG_ENCODERS
