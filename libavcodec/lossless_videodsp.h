/*
 * Lossless video DSP utils
 *
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


#ifndef AVCODEC_LOSSLESS_VIDEODSP_H
#define AVCODEC_LOSSLESS_VIDEODSP_H

#include "avcodec.h"
#include "libavutil/cpu.h"

typedef struct LLVidDSPContext {
    void (*add_int16)(uint16_t *dst/*align 16*/, const uint16_t *src/*align 16*/, unsigned mask, int w);
    void (*diff_int16)(uint16_t *dst/*align 16*/, const uint16_t *src1/*align 16*/, const uint16_t *src2/*align 1*/, unsigned mask, int w);

    void (*sub_hfyu_median_prediction_int16)(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w, int *left, int *left_top);
    void (*add_hfyu_median_prediction_int16)(uint16_t *dst, const uint16_t *top, const uint16_t *diff, unsigned mask, int w, int *left, int *left_top);
    int  (*add_hfyu_left_prediction_int16)(uint16_t *dst, const uint16_t *src, unsigned mask, int w, int left);
} LLVidDSPContext;

void ff_llviddsp_init(LLVidDSPContext *llviddsp, AVCodecContext *avctx);
void ff_llviddsp_init_x86(LLVidDSPContext *llviddsp, AVCodecContext *avctx);

#endif //AVCODEC_LOSSLESS_VIDEODSP_H
