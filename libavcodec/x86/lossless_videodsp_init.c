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

#include "../lossless_videodsp.h"
#include "libavutil/pixdesc.h"
#include "libavutil/x86/cpu.h"

void ff_add_int16_mmx(uint16_t *dst, const uint16_t *src, unsigned mask, int w);
void ff_add_int16_sse2(uint16_t *dst, const uint16_t *src, unsigned mask, int w);
void ff_diff_int16_mmx (uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w);
void ff_diff_int16_sse2(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w);
int ff_add_hfyu_left_pred_int16_ssse3(uint16_t *dst, const uint16_t *src, unsigned mask, int w, unsigned acc);
int ff_add_hfyu_left_pred_int16_sse4(uint16_t *dst, const uint16_t *src, unsigned mask, int w, unsigned acc);
void ff_add_hfyu_median_pred_int16_mmxext(uint16_t *dst, const uint16_t *top, const uint16_t *diff, unsigned mask, int w, int *left, int *left_top);
void ff_sub_hfyu_median_pred_int16_mmxext(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w, int *left, int *left_top);


void ff_llviddsp_init_x86(LLVidDSPContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if (EXTERNAL_MMX(cpu_flags)) {
        c->add_int16 = ff_add_int16_mmx;
        c->diff_int16 = ff_diff_int16_mmx;
    }

    if (EXTERNAL_MMXEXT(cpu_flags) && pix_desc->comp[0].depth_minus1<15) {
        c->add_hfyu_median_pred_int16 = ff_add_hfyu_median_pred_int16_mmxext;
        c->sub_hfyu_median_pred_int16 = ff_sub_hfyu_median_pred_int16_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_int16 = ff_add_int16_sse2;
        c->diff_int16 = ff_diff_int16_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->add_hfyu_left_pred_int16 = ff_add_hfyu_left_pred_int16_ssse3;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        c->add_hfyu_left_pred_int16 = ff_add_hfyu_left_pred_int16_sse4;
    }
}
