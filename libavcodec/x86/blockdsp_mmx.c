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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/blockdsp.h"
#include "libavcodec/version.h"

void ff_clear_block_mmx(int16_t *block);
void ff_clear_block_sse(int16_t *block);
void ff_clear_blocks_mmx(int16_t *blocks);
void ff_clear_blocks_sse(int16_t *blocks);

#if FF_API_XVMC
av_cold void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth,
                                  AVCodecContext *avctx)
#else
av_cold void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth)
#endif /* FF_API_XVMC */
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (!high_bit_depth) {
        if (EXTERNAL_MMX(cpu_flags)) {
            c->clear_block  = ff_clear_block_mmx;
            c->clear_blocks = ff_clear_blocks_mmx;
        }

    /* XvMCCreateBlocks() may not allocate 16-byte aligned blocks */
    if (CONFIG_XVMC && avctx->hwaccel && avctx->hwaccel->decode_mb)
        return;

        if (EXTERNAL_SSE(cpu_flags)) {
            c->clear_block  = ff_clear_block_sse;
            c->clear_blocks = ff_clear_blocks_sse;
        }
    }
#endif /* HAVE_YASM */
}
