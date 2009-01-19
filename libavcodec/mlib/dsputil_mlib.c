/*
 * Sun mediaLib optimized DSP utils
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"

#include <mlib_types.h>
#include <mlib_status.h>
#include <mlib_sys.h>
#include <mlib_algebra.h>
#include <mlib_video.h>

/* misc */

static void get_pixels_mlib(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
{
  int i;

  for (i=0;i<8;i++) {
    mlib_VectorConvert_S16_U8_Mod((mlib_s16 *)block, (mlib_u8 *)pixels, 8);

    pixels += line_size;
    block += 8;
  }
}

static void diff_pixels_mlib(DCTELEM *restrict block, const uint8_t *s1, const uint8_t *s2, int line_size)
{
  int i;

  for (i=0;i<8;i++) {
    mlib_VectorSub_S16_U8_Mod((mlib_s16 *)block, (mlib_u8 *)s1, (mlib_u8 *)s2, 8);

    s1 += line_size;
    s2 += line_size;
    block += 8;
  }
}

static void add_pixels_clamped_mlib(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    mlib_VideoAddBlock_U8_S16(pixels, (mlib_s16 *)block, line_size);
}

/* put block, width 16 pixel, height 8/16 */

static void put_pixels16_mlib (uint8_t * dest, const uint8_t * ref,
                               int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoCopyRef_U8_U8_16x8(dest, (uint8_t *)ref, stride);
    break;

    case 16:
      mlib_VideoCopyRef_U8_U8_16x16(dest, (uint8_t *)ref, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels16_x2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpX_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpX_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels16_y2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpY_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels16_xy2_mlib(uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpXY_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpXY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

/* put block, width 8 pixel, height 4/8/16 */

static void put_pixels8_mlib (uint8_t * dest, const uint8_t * ref,
                               int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoCopyRef_U8_U8_8x4(dest, (uint8_t *)ref, stride);
    break;

    case 8:
      mlib_VideoCopyRef_U8_U8_8x8(dest, (uint8_t *)ref, stride);
    break;

    case 16:
      mlib_VideoCopyRef_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels8_x2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpX_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpX_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels8_y2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpY_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpY_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void put_pixels8_xy2_mlib(uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpXY_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpXY_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

/* average block, width 16 pixel, height 8/16 */

static void avg_pixels16_mlib (uint8_t * dest, const uint8_t * ref,
                               int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoCopyRefAve_U8_U8_16x8(dest, (uint8_t *)ref, stride);
    break;

    case 16:
      mlib_VideoCopyRefAve_U8_U8_16x16(dest, (uint8_t *)ref, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels16_x2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpAveX_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveX_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels16_y2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpAveY_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels16_xy2_mlib(uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 8:
      mlib_VideoInterpAveXY_U8_U8_16x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveXY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

/* average block, width 8 pixel, height 4/8/16 */

static void avg_pixels8_mlib (uint8_t * dest, const uint8_t * ref,
                               int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoCopyRefAve_U8_U8_8x4(dest, (uint8_t *)ref, stride);
    break;

    case 8:
      mlib_VideoCopyRefAve_U8_U8_8x8(dest, (uint8_t *)ref, stride);
    break;

    case 16:
      mlib_VideoCopyRefAve_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels8_x2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpAveX_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpAveX_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels8_y2_mlib (uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpAveY_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpAveY_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

static void avg_pixels8_xy2_mlib(uint8_t * dest, const uint8_t * ref,
                                  int stride, int height)
{
  switch (height) {
    case 4:
      mlib_VideoInterpAveXY_U8_U8_8x4(dest, (uint8_t *)ref, stride, stride);
    break;

    case 8:
      mlib_VideoInterpAveXY_U8_U8_8x8(dest, (uint8_t *)ref, stride, stride);
    break;

    case 16:
      mlib_VideoInterpAveXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    break;

    default:
      assert(0);
  }
}

/* swap byte order of a buffer */

static void bswap_buf_mlib(uint32_t *dst, const uint32_t *src, int w)
{
  mlib_VectorReverseByteOrder_U32_U32(dst, src, w);
}

/* transformations */

static void ff_idct_put_mlib(uint8_t *dest, int line_size, DCTELEM *data)
{
    int i;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    mlib_VideoIDCT8x8_S16_S16 (data, data);

    for(i=0;i<8;i++) {
        dest[0] = cm[data[0]];
        dest[1] = cm[data[1]];
        dest[2] = cm[data[2]];
        dest[3] = cm[data[3]];
        dest[4] = cm[data[4]];
        dest[5] = cm[data[5]];
        dest[6] = cm[data[6]];
        dest[7] = cm[data[7]];

        dest += line_size;
        data += 8;
    }
}

static void ff_idct_add_mlib(uint8_t *dest, int line_size, DCTELEM *data)
{
    mlib_VideoIDCT8x8_S16_S16 (data, data);
    mlib_VideoAddBlock_U8_S16(dest, (mlib_s16 *)data, line_size);
}

static void ff_idct_mlib(DCTELEM *data)
{
    mlib_VideoIDCT8x8_S16_S16 (data, data);
}

static void ff_fdct_mlib(DCTELEM *data)
{
    mlib_VideoDCT8x8_S16_S16 (data, data);
}

void dsputil_init_mlib(DSPContext* c, AVCodecContext *avctx)
{
    c->get_pixels  = get_pixels_mlib;
    c->diff_pixels = diff_pixels_mlib;
    c->add_pixels_clamped = add_pixels_clamped_mlib;

    c->put_pixels_tab[0][0] = put_pixels16_mlib;
    c->put_pixels_tab[0][1] = put_pixels16_x2_mlib;
    c->put_pixels_tab[0][2] = put_pixels16_y2_mlib;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_mlib;
    c->put_pixels_tab[1][0] = put_pixels8_mlib;
    c->put_pixels_tab[1][1] = put_pixels8_x2_mlib;
    c->put_pixels_tab[1][2] = put_pixels8_y2_mlib;
    c->put_pixels_tab[1][3] = put_pixels8_xy2_mlib;

    c->avg_pixels_tab[0][0] = avg_pixels16_mlib;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_mlib;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_mlib;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mlib;
    c->avg_pixels_tab[1][0] = avg_pixels8_mlib;
    c->avg_pixels_tab[1][1] = avg_pixels8_x2_mlib;
    c->avg_pixels_tab[1][2] = avg_pixels8_y2_mlib;
    c->avg_pixels_tab[1][3] = avg_pixels8_xy2_mlib;

    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_mlib;
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_mlib;

    c->bswap_buf = bswap_buf_mlib;
}

void MPV_common_init_mlib(MpegEncContext *s)
{
    if(s->avctx->dct_algo==FF_DCT_AUTO || s->avctx->dct_algo==FF_DCT_MLIB){
        s->dsp.fdct = ff_fdct_mlib;
    }

    if(s->avctx->idct_algo==FF_IDCT_MLIB){
        s->dsp.idct_put= ff_idct_put_mlib;
        s->dsp.idct_add= ff_idct_add_mlib;
        s->dsp.idct    = ff_idct_mlib;
        s->dsp.idct_permutation_type= FF_NO_IDCT_PERM;
    }
}
