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

#ifndef AVCODEC_MPEGVIDEOENCDSP_H
#define AVCODEC_MPEGVIDEOENCDSP_H

#include <stdint.h>

#include "avcodec.h"

#define BASIS_SHIFT 16
#define RECON_SHIFT 6

typedef struct MpegvideoEncDSPContext {
    int (*try_8x8basis)(int16_t rem[64], int16_t weight[64],
                        int16_t basis[64], int scale);
    void (*add_8x8basis)(int16_t rem[64], int16_t basis[64], int scale);

} MpegvideoEncDSPContext;

void ff_mpegvideoencdsp_init(MpegvideoEncDSPContext *c,
                             AVCodecContext *avctx);
void ff_mpegvideoencdsp_init_x86(MpegvideoEncDSPContext *c,
                                 AVCodecContext *avctx);

#endif /* AVCODEC_MPEGVIDEOENCDSP_H */
