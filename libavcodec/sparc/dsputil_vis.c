/*
 * Copyright (C) 2003 David S. Miller <davem@redhat.com>
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

#include "libavutil/attributes.h"
#include "libavcodec/dsputil.h"
#include "dsputil_vis.h"
#include "vis.h"

av_cold void ff_dsputil_init_vis(DSPContext *c, AVCodecContext *avctx)
{
  /* VIS-specific optimizations */
  int accel = vis_level ();
  const int high_bit_depth = avctx->bits_per_raw_sample > 8;

  if (accel & ACCEL_SPARC_VIS && !high_bit_depth) {
      if (avctx->idct_algo == FF_IDCT_SIMPLEVIS) {
          c->idct_put = ff_simple_idct_put_vis;
          c->idct_add = ff_simple_idct_add_vis;
          c->idct     = ff_simple_idct_vis;
          c->idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
      }
  }
}
