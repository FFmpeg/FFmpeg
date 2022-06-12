/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"

#include "vp9dsp.h"

const DECLARE_ALIGNED(16, int16_t, ff_vp9_subpel_filters)[3][16][8] = {
    [FILTER_8TAP_REGULAR] = {
        {  0,  0,   0, 128,   0,   0,  0,  0 },
        {  0,  1,  -5, 126,   8,  -3,  1,  0 },
        { -1,  3, -10, 122,  18,  -6,  2,  0 },
        { -1,  4, -13, 118,  27,  -9,  3, -1 },
        { -1,  4, -16, 112,  37, -11,  4, -1 },
        { -1,  5, -18, 105,  48, -14,  4, -1 },
        { -1,  5, -19,  97,  58, -16,  5, -1 },
        { -1,  6, -19,  88,  68, -18,  5, -1 },
        { -1,  6, -19,  78,  78, -19,  6, -1 },
        { -1,  5, -18,  68,  88, -19,  6, -1 },
        { -1,  5, -16,  58,  97, -19,  5, -1 },
        { -1,  4, -14,  48, 105, -18,  5, -1 },
        { -1,  4, -11,  37, 112, -16,  4, -1 },
        { -1,  3,  -9,  27, 118, -13,  4, -1 },
        {  0,  2,  -6,  18, 122, -10,  3, -1 },
        {  0,  1,  -3,   8, 126,  -5,  1,  0 },
    }, [FILTER_8TAP_SHARP] = {
        {  0,  0,   0, 128,   0,   0,  0,  0 },
        { -1,  3,  -7, 127,   8,  -3,  1,  0 },
        { -2,  5, -13, 125,  17,  -6,  3, -1 },
        { -3,  7, -17, 121,  27, -10,  5, -2 },
        { -4,  9, -20, 115,  37, -13,  6, -2 },
        { -4, 10, -23, 108,  48, -16,  8, -3 },
        { -4, 10, -24, 100,  59, -19,  9, -3 },
        { -4, 11, -24,  90,  70, -21, 10, -4 },
        { -4, 11, -23,  80,  80, -23, 11, -4 },
        { -4, 10, -21,  70,  90, -24, 11, -4 },
        { -3,  9, -19,  59, 100, -24, 10, -4 },
        { -3,  8, -16,  48, 108, -23, 10, -4 },
        { -2,  6, -13,  37, 115, -20,  9, -4 },
        { -2,  5, -10,  27, 121, -17,  7, -3 },
        { -1,  3,  -6,  17, 125, -13,  5, -2 },
        {  0,  1,  -3,   8, 127,  -7,  3, -1 },
    }, [FILTER_8TAP_SMOOTH] = {
        {  0,  0,   0, 128,   0,   0,  0,  0 },
        { -3, -1,  32,  64,  38,   1, -3,  0 },
        { -2, -2,  29,  63,  41,   2, -3,  0 },
        { -2, -2,  26,  63,  43,   4, -4,  0 },
        { -2, -3,  24,  62,  46,   5, -4,  0 },
        { -2, -3,  21,  60,  49,   7, -4,  0 },
        { -1, -4,  18,  59,  51,   9, -4,  0 },
        { -1, -4,  16,  57,  53,  12, -4, -1 },
        { -1, -4,  14,  55,  55,  14, -4, -1 },
        { -1, -4,  12,  53,  57,  16, -4, -1 },
        {  0, -4,   9,  51,  59,  18, -4, -1 },
        {  0, -4,   7,  49,  60,  21, -3, -2 },
        {  0, -4,   5,  46,  62,  24, -3, -2 },
        {  0, -4,   4,  43,  63,  26, -2, -2 },
        {  0, -3,   2,  41,  63,  29, -2, -2 },
        {  0, -3,   1,  38,  64,  32, -1, -3 },
    }
};


av_cold void ff_vp9dsp_init(VP9DSPContext *dsp, int bpp, int bitexact)
{
    if (bpp == 8) {
        ff_vp9dsp_init_8(dsp);
    } else if (bpp == 10) {
        ff_vp9dsp_init_10(dsp);
    } else {
        av_assert0(bpp == 12);
        ff_vp9dsp_init_12(dsp);
    }

#if ARCH_AARCH64
    ff_vp9dsp_init_aarch64(dsp, bpp);
#elif ARCH_ARM
    ff_vp9dsp_init_arm(dsp, bpp);
#elif ARCH_X86
    ff_vp9dsp_init_x86(dsp, bpp, bitexact);
#elif ARCH_MIPS
    ff_vp9dsp_init_mips(dsp, bpp);
#elif ARCH_LOONGARCH
    ff_vp9dsp_init_loongarch(dsp, bpp);
#endif
}
