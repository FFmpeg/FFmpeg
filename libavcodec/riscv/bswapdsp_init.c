/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/bswapdsp.h"

void ff_bswap32_buf_rvb(uint32_t *dst, const uint32_t *src, int len);
void ff_bswap16_buf_rvv(uint16_t *dst, const uint16_t *src, int len);
void ff_bswap32_buf_rvvb(uint32_t *dst, const uint32_t *src, int len);
void ff_bswap16_buf_rvvb(uint16_t *dst, const uint16_t *src, int len);

av_cold void ff_bswapdsp_init_riscv(BswapDSPContext *c)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

#if (__riscv_xlen >= 64)
    if (flags & AV_CPU_FLAG_RVB_BASIC)
        c->bswap_buf = ff_bswap32_buf_rvb;
#endif
#if HAVE_RVV
    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        c->bswap16_buf = ff_bswap16_buf_rvv;
#if HAVE_RV_ZVBB
        if (flags & AV_CPU_FLAG_RV_ZVBB) {
            c->bswap_buf = ff_bswap32_buf_rvvb;
            c->bswap16_buf = ff_bswap16_buf_rvvb;
        }
#endif
    }
#endif
#endif
}
