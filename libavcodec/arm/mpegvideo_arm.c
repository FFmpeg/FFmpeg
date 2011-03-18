/*
 * Copyright (c) 2002 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "mpegvideo_arm.h"
#include "asm-offsets.h"

#if HAVE_NEON
CHK_OFFS(MpegEncContext, y_dc_scale,       Y_DC_SCALE);
CHK_OFFS(MpegEncContext, c_dc_scale,       C_DC_SCALE);
CHK_OFFS(MpegEncContext, ac_pred,          AC_PRED);
CHK_OFFS(MpegEncContext, block_last_index, BLOCK_LAST_INDEX);
CHK_OFFS(MpegEncContext, inter_scantable.raster_end, INTER_SCANTAB_RASTER_END);
CHK_OFFS(MpegEncContext, h263_aic,         H263_AIC);
#endif

void ff_dct_unquantize_h263_inter_neon(MpegEncContext *s, DCTELEM *block,
                                       int n, int qscale);
void ff_dct_unquantize_h263_intra_neon(MpegEncContext *s, DCTELEM *block,
                                       int n, int qscale);

void MPV_common_init_arm(MpegEncContext *s)
{
    /* IWMMXT support is a superset of armv5te, so
     * allow optimized functions for armv5te unless
     * a better iwmmxt function exists
     */
#if HAVE_ARMV5TE
    MPV_common_init_armv5te(s);
#endif
#if HAVE_IWMMXT
    MPV_common_init_iwmmxt(s);
#endif

    if (HAVE_NEON) {
        s->dct_unquantize_h263_intra = ff_dct_unquantize_h263_intra_neon;
        s->dct_unquantize_h263_inter = ff_dct_unquantize_h263_inter_neon;
    }
}
