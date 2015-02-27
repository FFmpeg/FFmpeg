/*
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

#include <stddef.h>

#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavutil/internal.h"

#include "libavcodec/imdct15.h"

#include "asm-offsets.h"

AV_CHECK_OFFSET(IMDCT15Context, exptab,         CELT_EXPTAB);
AV_CHECK_OFFSET(IMDCT15Context, fft_n,          CELT_FFT_N);
AV_CHECK_OFFSET(IMDCT15Context, len2,           CELT_LEN2);
AV_CHECK_OFFSET(IMDCT15Context, len4,           CELT_LEN4);
AV_CHECK_OFFSET(IMDCT15Context, tmp,            CELT_TMP);
AV_CHECK_OFFSET(IMDCT15Context, twiddle_exptab, CELT_TWIDDLE);

void ff_celt_imdct_half_neon(IMDCT15Context *s, float *dst, const float *src,
                             ptrdiff_t stride, float scale);

void ff_imdct15_init_aarch64(IMDCT15Context *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->imdct_half = ff_celt_imdct_half_neon;
    }
}
