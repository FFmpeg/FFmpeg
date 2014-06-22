/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_altivec.h"

av_cold void ff_dsputil_init_ppc(DSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
    int mm_flags = av_get_cpu_flags();
    if (PPC_ALTIVEC(mm_flags)) {
        ff_dsputil_init_altivec(c, avctx, high_bit_depth);

        c->gmc1 = ff_gmc1_altivec;

        if (!high_bit_depth) {
#if CONFIG_ENCODERS
            if (avctx->dct_algo == FF_DCT_AUTO ||
                avctx->dct_algo == FF_DCT_ALTIVEC) {
                c->fdct = ff_fdct_altivec;
            }
#endif //CONFIG_ENCODERS
          if (avctx->lowres == 0) {
            if ((avctx->idct_algo == FF_IDCT_AUTO) ||
                (avctx->idct_algo == FF_IDCT_ALTIVEC)) {
                c->idct                  = ff_idct_altivec;
                c->idct_put              = ff_idct_put_altivec;
                c->idct_add              = ff_idct_add_altivec;
                c->idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
            }
          }
        }
    }
}
