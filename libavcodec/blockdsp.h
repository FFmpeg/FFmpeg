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

#ifndef AVCODEC_BLOCKDSP_H
#define AVCODEC_BLOCKDSP_H

#include <stdint.h>

#include "avcodec.h"
#include "version.h"

/* add and put pixel (decoding)
 * Block sizes for op_pixels_func are 8x4,8x8 16x8 16x16.
 * h for op_pixels_func is limited to { width / 2, width },
 * but never larger than 16 and never smaller than 4. */
typedef void (*op_fill_func)(uint8_t *block /* align width (8 or 16) */,
                             uint8_t value, int line_size, int h);

typedef struct BlockDSPContext {
    void (*clear_block)(int16_t *block /* align 16 */);
    void (*clear_blocks)(int16_t *blocks /* align 16 */);

    op_fill_func fill_block_tab[2];
} BlockDSPContext;

void ff_blockdsp_init(BlockDSPContext *c, AVCodecContext *avctx);

void ff_blockdsp_init_arm(BlockDSPContext *c, unsigned high_bit_depth);
void ff_blockdsp_init_ppc(BlockDSPContext *c, unsigned high_bit_depth);
#if FF_API_XVMC
void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth,
                          AVCodecContext *avctx);
#else
void ff_blockdsp_init_x86(BlockDSPContext *c, unsigned high_bit_depth);
#endif /* FF_API_XVMC */

#endif /* AVCODEC_BLOCKDSP_H */
