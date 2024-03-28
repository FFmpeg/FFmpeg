/*
 * Copyright (c) 2002 Michael Niedermayer
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

#include <assert.h>

#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideo.h"
#include "mpegvideo_arm.h"
#include "asm-offsets.h"

#if HAVE_NEON
#define CHECK_OFFSET(s, m, o)          \
    static_assert(offsetof(s, m) == o, \
                  "Hardcoded ASM offset of " #s " field " #o " needs to be updated.");
CHECK_OFFSET(MpegEncContext, y_dc_scale,       Y_DC_SCALE);
CHECK_OFFSET(MpegEncContext, c_dc_scale,       C_DC_SCALE);
CHECK_OFFSET(MpegEncContext, ac_pred,          AC_PRED);
CHECK_OFFSET(MpegEncContext, block_last_index, BLOCK_LAST_INDEX);
CHECK_OFFSET(MpegEncContext, inter_scantable.raster_end,
             INTER_SCANTAB_RASTER_END);
CHECK_OFFSET(MpegEncContext, h263_aic,         H263_AIC);
#endif

void ff_dct_unquantize_h263_inter_neon(MpegEncContext *s, int16_t *block,
                                       int n, int qscale);
void ff_dct_unquantize_h263_intra_neon(MpegEncContext *s, int16_t *block,
                                       int n, int qscale);

av_cold void ff_mpv_common_init_arm(MpegEncContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv5te(cpu_flags))
        ff_mpv_common_init_armv5te(s);

    if (have_neon(cpu_flags)) {
        s->dct_unquantize_h263_intra = ff_dct_unquantize_h263_intra_neon;
        s->dct_unquantize_h263_inter = ff_dct_unquantize_h263_inter_neon;
    }
}
