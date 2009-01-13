/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

void ff_vector_fmul_vfp(float *dst, const float *src, int len);
void ff_vector_fmul_reverse_vfp(float *dst, const float *src0,
                                const float *src1, int len);
void ff_float_to_int16_vfp(int16_t *dst, const float *src, long len);

void ff_float_init_arm_vfp(DSPContext* c, AVCodecContext *avctx)
{
    c->vector_fmul = ff_vector_fmul_vfp;
    c->vector_fmul_reverse = ff_vector_fmul_reverse_vfp;
#if HAVE_ARMV6
    c->float_to_int16 = ff_float_to_int16_vfp;
#endif
}
