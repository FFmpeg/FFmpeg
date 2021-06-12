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

#include "config.h"
#include "lossless_videodsp.h"
#include "libavcodec/mathops.h"

// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~0UL / 255 * 0x7f)
#define pb_80 (~0UL / 255 * 0x80)

static void add_bytes_c(uint8_t *dst, uint8_t *src, ptrdiff_t w)
{
    long i;

    for (i = 0; i <= w - (int) sizeof(long); i += sizeof(long)) {
        long a = *(long *) (src + i);
        long b = *(long *) (dst + i);
        *(long *) (dst + i) = ((a & pb_7f) + (b & pb_7f)) ^ ((a ^ b) & pb_80);
    }
    for (; i < w; i++)
        dst[i + 0] += src[i + 0];
}

static void add_median_pred_c(uint8_t *dst, const uint8_t *src1,
                              const uint8_t *diff, ptrdiff_t w,
                              int *left, int *left_top)
{
    int i;
    uint8_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        l      = mid_pred(l, src1[i], (l + src1[i] - lt) & 0xFF) + diff[i];
        lt     = src1[i];
        dst[i] = l;
    }

    *left     = l;
    *left_top = lt;
}

static int add_left_pred_c(uint8_t *dst, const uint8_t *src, ptrdiff_t w,
                           int acc)
{
    int i;

    for (i = 0; i < w - 1; i++) {
        acc   += src[i];
        dst[i] = acc;
        i++;
        acc   += src[i];
        dst[i] = acc;
    }

    for (; i < w; i++) {
        acc   += src[i];
        dst[i] = acc;
    }

    return acc;
}

static int add_left_pred_int16_c(uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc){
    int i;

    for(i=0; i<w-1; i++){
        acc+= src[i];
        dst[i]= acc &= mask;
        i++;
        acc+= src[i];
        dst[i]= acc &= mask;
    }

    for(; i<w; i++){
        acc+= src[i];
        dst[i]= acc &= mask;
    }

    return acc;
}

static void add_gradient_pred_c(uint8_t *src, const ptrdiff_t stride, const ptrdiff_t width){
    int A, B, C, i;

    for (i = 0; i < width; i++) {
        A = src[i - stride];
        B = src[i - (stride + 1)];
        C = src[i - 1];
        src[i] = (A - B + C + src[i]) & 0xFF;
    }
}

void ff_llviddsp_init(LLVidDSPContext *c)
{
    c->add_bytes                  = add_bytes_c;
    c->add_median_pred            = add_median_pred_c;
    c->add_left_pred              = add_left_pred_c;

    c->add_left_pred_int16        = add_left_pred_int16_c;
    c->add_gradient_pred          = add_gradient_pred_c;

    if (ARCH_PPC)
        ff_llviddsp_init_ppc(c);
    if (ARCH_X86)
        ff_llviddsp_init_x86(c);
}
