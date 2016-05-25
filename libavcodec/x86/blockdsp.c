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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/blockdsp.h"
#include "libavcodec/version.h"

#if HAVE_INLINE_ASM

#define CLEAR_BLOCKS(name, n)                           \
static void name(int16_t *blocks)                       \
{                                                       \
    __asm__ volatile (                                  \
        "pxor %%mm7, %%mm7                 \n\t"        \
        "mov     %1,        %%"FF_REG_a"   \n\t"        \
        "1:                                \n\t"        \
        "movq %%mm7,   (%0, %%"FF_REG_a")  \n\t"        \
        "movq %%mm7,  8(%0, %%"FF_REG_a")  \n\t"        \
        "movq %%mm7, 16(%0, %%"FF_REG_a")  \n\t"        \
        "movq %%mm7, 24(%0, %%"FF_REG_a")  \n\t"        \
        "add    $32, %%"FF_REG_a"          \n\t"        \
        "js      1b                        \n\t"        \
        :: "r"(((uint8_t *) blocks) + 128 * n),         \
           "i"(-128 * n)                                \
        : "%"FF_REG_a);                                 \
}
CLEAR_BLOCKS(clear_blocks_mmx, 6)
CLEAR_BLOCKS(clear_block_mmx, 1)

static void clear_block_sse(int16_t *block)
{
    __asm__ volatile (
        "xorps  %%xmm0, %%xmm0          \n"
        "movaps %%xmm0,    (%0)         \n"
        "movaps %%xmm0,  16(%0)         \n"
        "movaps %%xmm0,  32(%0)         \n"
        "movaps %%xmm0,  48(%0)         \n"
        "movaps %%xmm0,  64(%0)         \n"
        "movaps %%xmm0,  80(%0)         \n"
        "movaps %%xmm0,  96(%0)         \n"
        "movaps %%xmm0, 112(%0)         \n"
        :: "r" (block)
        : "memory");
}

static void clear_blocks_sse(int16_t *blocks)
{
    __asm__ volatile (
        "xorps  %%xmm0, %%xmm0                 \n"
        "mov        %1,         %%"FF_REG_a"   \n"
        "1:                                    \n"
        "movaps %%xmm0,    (%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  16(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  32(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  48(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  64(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  80(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0,  96(%0, %%"FF_REG_a")  \n"
        "movaps %%xmm0, 112(%0, %%"FF_REG_a")  \n"
        "add      $128,         %%"FF_REG_a"   \n"
        "js         1b                         \n"
        :: "r"(((uint8_t *) blocks) + 128 * 6), "i"(-128 * 6)
        : "%"FF_REG_a);
}

#endif /* HAVE_INLINE_ASM */

#if FF_API_XVMC
av_cold void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth,
                                  AVCodecContext *avctx)
#else
av_cold void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth)
#endif /* FF_API_XVMC */
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (!high_bit_depth) {
        if (INLINE_MMX(cpu_flags)) {
            c->clear_block  = clear_block_mmx;
            c->clear_blocks = clear_blocks_mmx;
        }

#if FF_API_XVMC
FF_DISABLE_DEPRECATION_WARNINGS
    /* XvMCCreateBlocks() may not allocate 16-byte aligned blocks */
    if (CONFIG_MPEG_XVMC_DECODER && avctx->xvmc_acceleration > 1)
        return;
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_XVMC */

        if (INLINE_SSE(cpu_flags)) {
            c->clear_block  = clear_block_sse;
            c->clear_blocks = clear_blocks_sse;
        }
    }
#endif /* HAVE_INLINE_ASM */
}
