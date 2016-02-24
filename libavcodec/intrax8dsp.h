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

#ifndef AVCODEC_INTRAX8DSP_H
#define AVCODEC_INTRAX8DSP_H

#include <stdint.h>

typedef struct IntraX8DSPContext {
    void (*v_loop_filter)(uint8_t *src, int stride, int qscale);
    void (*h_loop_filter)(uint8_t *src, int stride, int qscale);

    void (*spatial_compensation[12])(uint8_t *src, uint8_t *dst, int linesize);
    void (*setup_spatial_compensation)(uint8_t *src, uint8_t *dst, int linesize,
                                       int *range, int *sum, int edges);
} IntraX8DSPContext;

void ff_intrax8dsp_init(IntraX8DSPContext *dsp);

#endif /* AVCODEC_INTRAX8DSP_H */
