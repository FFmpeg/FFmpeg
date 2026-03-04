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

#ifndef AVUTIL_RISCV_PIXELUTILS_H
#define AVUTIL_RISCV_PIXELUTILS_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#include "cpu.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/pixelutils.h"

int ff_pixelutils_sad16_rvv(const uint8_t *src1, ptrdiff_t stride1,
                            const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad8_rvv (const uint8_t *src1, ptrdiff_t stride1,
                            const uint8_t *src2, ptrdiff_t stride2);

static inline av_cold void ff_pixelutils_init_riscv(av_pixelutils_sad_fn *sad, int aligned)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32 && ff_rv_vlen_least(128)) {
        sad[3] = ff_pixelutils_sad16_rvv;
        sad[2] = ff_pixelutils_sad8_rvv;
    }
#endif
}
#endif
