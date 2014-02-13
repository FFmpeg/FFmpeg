/*
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/bswapdsp.h"

void ff_bswap32_buf_sse2(uint32_t *dst, const uint32_t *src, int w);
void ff_bswap32_buf_ssse3(uint32_t *dst, const uint32_t *src, int w);

av_cold void ff_bswapdsp_init_x86(BswapDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags))
        c->bswap_buf = ff_bswap32_buf_sse2;
    if (EXTERNAL_SSSE3(cpu_flags))
        c->bswap_buf = ff_bswap32_buf_ssse3;
}
