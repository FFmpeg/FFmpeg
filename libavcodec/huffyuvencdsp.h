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

#ifndef AVCODEC_HUFFYUVENCDSP_H
#define AVCODEC_HUFFYUVENCDSP_H

#include <stdint.h>

typedef struct HuffYUVEncDSPContext {
    void (*diff_bytes)(uint8_t *dst /* align 16 */,
                       const uint8_t *src1 /* align 16 */,
                       const uint8_t *src2 /* align 1 */,
                       intptr_t w);
    /**
     * Subtract HuffYUV's variant of median prediction.
     * Note, this might read from src1[-1], src2[-1].
     */
    void (*sub_hfyu_median_pred)(uint8_t *dst, const uint8_t *src1,
                                 const uint8_t *src2, intptr_t w,
                                 int *left, int *left_top);
} HuffYUVEncDSPContext;

void ff_huffyuvencdsp_init(HuffYUVEncDSPContext *c);
void ff_huffyuvencdsp_init_x86(HuffYUVEncDSPContext *c);

#endif /* AVCODEC_HUFFYUVENCDSP_H */
