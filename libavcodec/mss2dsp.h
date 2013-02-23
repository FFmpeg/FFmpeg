/*
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder
 *
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

/**
 * @file
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder DSP routines
 */

#ifndef AVCODEC_MSS2DSP_H
#define AVCODEC_MSS2DSP_H

#include <stdint.h>

typedef struct MSS2DSPContext {
    void (*mss2_blit_wmv9)(uint8_t *dst, int dst_stride,
                           const uint8_t *srcy, int srcy_stride,
                           const uint8_t *srcu, const uint8_t *srcv,
                           int srcuv_stride, int w, int h);
    void (*mss2_blit_wmv9_masked)(uint8_t *dst, int dst_stride,
                                  int maskcolor, const uint8_t *mask,
                                  int mask_stride,
                                  const uint8_t *srcy, int srcy_stride,
                                  const uint8_t *srcu, const uint8_t *srcv,
                                  int srcuv_stride, int w, int h);
    void (*mss2_gray_fill_masked)(uint8_t *dst, int dst_stride,
                                  int maskcolor, const uint8_t *mask,
                                  int mask_stride, int w, int h);
    void (*upsample_plane)(uint8_t *plane, int plane_stride, int w, int h);
} MSS2DSPContext;

void ff_mss2dsp_init(MSS2DSPContext *dsp);

#endif /* AVCODEC_MSS2DSP_H */
