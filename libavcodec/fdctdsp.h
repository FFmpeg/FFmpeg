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

#ifndef AVCODEC_FDCTDSP_H
#define AVCODEC_FDCTDSP_H

#include <stdint.h>

#include "avcodec.h"

typedef struct FDCTDSPContext {
    void (*fdct)(int16_t *block /* align 16 */);
    void (*fdct248)(int16_t *block /* align 16 */);
} FDCTDSPContext;

void ff_fdctdsp_init(FDCTDSPContext *c, AVCodecContext *avctx);
void ff_fdctdsp_init_ppc(FDCTDSPContext *c, AVCodecContext *avctx,
                         unsigned high_bit_depth);
void ff_fdctdsp_init_x86(FDCTDSPContext *c, AVCodecContext *avctx,
                         unsigned high_bit_depth);

#endif /* AVCODEC_FDCTDSP_H */
