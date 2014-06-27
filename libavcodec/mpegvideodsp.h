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

#ifndef AVCODEC_MPEGVIDEODSP_H
#define AVCODEC_MPEGVIDEODSP_H

#include <stdint.h>

void ff_gmc_c(uint8_t *dst, uint8_t *src, int stride, int h, int ox, int oy,
              int dxx, int dxy, int dyx, int dyy, int shift, int r,
              int width, int height);

typedef struct MpegVideoDSPContext {
    /**
     * translational global motion compensation.
     */
    void (*gmc1)(uint8_t *dst /* align 8 */, uint8_t *src /* align 1 */,
                 int srcStride, int h, int x16, int y16, int rounder);
    /**
     * global motion compensation.
     */
    void (*gmc)(uint8_t *dst /* align 8 */, uint8_t *src /* align 1 */,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height);
} MpegVideoDSPContext;

void ff_mpegvideodsp_init(MpegVideoDSPContext *c);
void ff_mpegvideodsp_init_ppc(MpegVideoDSPContext *c);
void ff_mpegvideodsp_init_x86(MpegVideoDSPContext *c);

#endif /* AVCODEC_MPEGVIDEODSP_H */
