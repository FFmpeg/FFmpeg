/*
 * ARM optimized Format Conversion Utils
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

#include <stdint.h>

#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/fmtconvert.h"

void ff_int32_to_float_fmul_scalar_neon(float *dst, const int *src,
                                        float mul, int len);

void ff_float_to_int16_neon(int16_t *dst, const float *src, long len);
void ff_float_to_int16_interleave_neon(int16_t *, const float **, long, int);

void ff_float_to_int16_vfp(int16_t *dst, const float *src, long len);

void ff_fmt_convert_init_arm(FmtConvertContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_vfp(cpu_flags) && have_armv6(cpu_flags)) {
        c->float_to_int16 = ff_float_to_int16_vfp;
    }

    if (have_neon(cpu_flags)) {
        c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_neon;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->float_to_int16            = ff_float_to_int16_neon;
            c->float_to_int16_interleave = ff_float_to_int16_interleave_neon;
        }
    }
}
