/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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
#include "libavutil/common.h"
#include "libavcodec/vp9dsp.h"
#include "vp9dsp_mips.h"

#if HAVE_MSA

static av_cold void vp9dsp_mc_init_msa(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
#define init_fpel(idx1, idx2, sz, type)                                    \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_##type##sz##_msa

#define init_copy_avg(idx, sz)    \
    init_fpel(idx, 0, sz, copy);  \
    init_fpel(idx, 1, sz, avg)

#define init_avg(idx, sz)  \
    init_fpel(idx, 1, sz, avg)

    init_copy_avg(0, 64);
    init_copy_avg(1, 32);
    init_copy_avg(2, 16);
    init_copy_avg(3,  8);
    init_avg(4,  4);

#undef init_copy_avg
#undef init_avg
#undef init_fpel

#define init_subpel1(idx1, idx2, idxh, idxv, sz, dir, type)  \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_smooth_##sz##dir##_msa;             \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][idxh][idxv] =   \
        ff_##type##_8tap_regular_##sz##dir##_msa;            \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_sharp_##sz##dir##_msa;

#define init_subpel2(idx, idxh, idxv, dir, type)      \
    init_subpel1(0, idx, idxh, idxv, 64, dir, type);  \
    init_subpel1(1, idx, idxh, idxv, 32, dir, type);  \
    init_subpel1(2, idx, idxh, idxv, 16, dir, type);  \
    init_subpel1(3, idx, idxh, idxv,  8, dir, type);  \
    init_subpel1(4, idx, idxh, idxv,  4, dir, type)

#define init_subpel3(idx, type)         \
    init_subpel2(idx, 1, 1, hv, type);  \
    init_subpel2(idx, 0, 1, v, type);   \
    init_subpel2(idx, 1, 0, h, type)

    init_subpel3(0, put);
    init_subpel3(1, avg);

#undef init_subpel1
#undef init_subpel2
#undef init_subpel3
    }
}

static av_cold void vp9dsp_init_msa(VP9DSPContext *dsp, int bpp)
{
    vp9dsp_mc_init_msa(dsp, bpp);
}
#endif  // #if HAVE_MSA

av_cold void ff_vp9dsp_init_mips(VP9DSPContext *dsp, int bpp)
{
#if HAVE_MSA
    vp9dsp_init_msa(dsp, bpp);
#endif  // #if HAVE_MSA
}
