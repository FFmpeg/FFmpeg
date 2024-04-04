/*
 * VVC DSP
 *
 * Copyright (C) 2021 Nuo Mi
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

#include "dsp.h"
#include "ctu.h"
#include "itx_1d.h"

#define VVC_SIGN(v) (v < 0 ? -1 : !!v)

static void av_always_inline pad_int16(int16_t *_dst, const ptrdiff_t dst_stride, const int width, const int height)
{
    const int padded_width = width + 2;
    int16_t *dst;
    for (int y = 0; y < height; y++) {
        dst = _dst + y * dst_stride;
        for (int x = 0; x < width; x++) {
            dst[-1] = dst[0];
            dst[width] = dst[width - 1];
        }
    }

    _dst--;
    //top
    memcpy(_dst - dst_stride, _dst, padded_width * sizeof(int16_t));
    //bottom
    _dst += dst_stride * height;
    memcpy(_dst, _dst - dst_stride, padded_width * sizeof(int16_t));
}

static int vvc_sad(const int16_t *src0, const int16_t *src1, int dx, int dy,
    const int block_w, const int block_h)
{
    int sad = 0;
    dx -= 2;
    dy -= 2;
    src0 += (2 + dy) * MAX_PB_SIZE + 2 + dx;
    src1 += (2 - dy) * MAX_PB_SIZE + 2 - dx;
    for (int y = 0; y < block_h; y += 2) {
        for (int x = 0; x < block_w; x++) {
            sad += FFABS(src0[x] - src1[x]);
        }
        src0 += 2 * MAX_PB_SIZE;
        src1 += 2 * MAX_PB_SIZE;
    }
    return sad;
}

typedef struct IntraEdgeParams {
    uint8_t* top;
    uint8_t* left;
    int filter_flag;

    uint16_t left_array[6 * MAX_TB_SIZE + 5];
    uint16_t filtered_left_array[6 * MAX_TB_SIZE + 5];
    uint16_t top_array[6 * MAX_TB_SIZE + 5];
    uint16_t filtered_top_array[6 * MAX_TB_SIZE + 5];
} IntraEdgeParams;

#define PROF_BORDER_EXT         1
#define PROF_BLOCK_SIZE         (AFFINE_MIN_BLOCK_SIZE + PROF_BORDER_EXT * 2)
#define BDOF_BORDER_EXT         1

#define BDOF_PADDED_SIZE        (16 + BDOF_BORDER_EXT * 2)
#define BDOF_BLOCK_SIZE         4
#define BDOF_GRADIENT_SIZE      (BDOF_BLOCK_SIZE + BDOF_BORDER_EXT * 2)

#define BIT_DEPTH 8
#include "dsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "dsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "dsp_template.c"
#undef BIT_DEPTH

void ff_vvc_dsp_init(VVCDSPContext *vvcdsp, int bit_depth)
{
#undef FUNC
#define FUNC(a, depth) a ## _ ## depth

#define VVC_DSP(depth)                                                          \
    FUNC(ff_vvc_inter_dsp_init, depth)(&vvcdsp->inter);                         \
    FUNC(ff_vvc_intra_dsp_init, depth)(&vvcdsp->intra);                         \
    FUNC(ff_vvc_itx_dsp_init, depth)(&vvcdsp->itx);                             \
    FUNC(ff_vvc_lmcs_dsp_init, depth)(&vvcdsp->lmcs);                           \
    FUNC(ff_vvc_lf_dsp_init, depth)(&vvcdsp->lf);                               \
    FUNC(ff_vvc_sao_dsp_init, depth)(&vvcdsp->sao);                             \
    FUNC(ff_vvc_alf_dsp_init, depth)(&vvcdsp->alf);                             \

    switch (bit_depth) {
    case 12:
        VVC_DSP(12);
        break;
    case 10:
        VVC_DSP(10);
        break;
    default:
        VVC_DSP(8);
        break;
    }

#if ARCH_X86
    ff_vvc_dsp_init_x86(vvcdsp, bit_depth);
#endif
}
