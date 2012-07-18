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

#ifndef AVCODEC_VP3DSP_H
#define AVCODEC_VP3DSP_H

#include <stdint.h>
#include "dsputil.h"

typedef struct VP3DSPContext {
    void (*idct_put)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*idct_add)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*idct_dc_add)(uint8_t *dest, int line_size, const DCTELEM *block);
    void (*v_loop_filter)(uint8_t *src, int stride, int *bounding_values);
    void (*h_loop_filter)(uint8_t *src, int stride, int *bounding_values);

    int idct_perm;
} VP3DSPContext;

void ff_vp3dsp_init(VP3DSPContext *c, int flags);
void ff_vp3dsp_init_arm(VP3DSPContext *c, int flags);
void ff_vp3dsp_init_ppc(VP3DSPContext *c, int flags);
void ff_vp3dsp_init_x86(VP3DSPContext *c, int flags);

#endif /* AVCODEC_VP3DSP_H */
