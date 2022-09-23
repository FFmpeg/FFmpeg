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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/videodsp.h"

void ff_prefetch_rv_zicbop(const uint8_t *mem, ptrdiff_t stride, int h);

av_cold void ff_videodsp_init_riscv(VideoDSPContext *ctx, int bpc)
{
#if HAVE_RV_ZICBOP
    /* TODO: Since we pay for the indirect function call anyway, we should
     * only set this if Cache-Block Operation Prefetch (Zicbop) is actually
     * supported and otherwise save a few cycles of NOPs.
     * But so far there are no means to detect Zicbop (in user mode).
     */
    ctx->prefetch = ff_prefetch_rv_zicbop;
#endif
}
