/*
 * Copyright (c) 2017 Matthieu Bouron <matthieu.bouron gmail.com>
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

#include "libavutil/aarch64/asm.S"

function ff_resample_common_apply_filter_x4_float_neon, export=1
        movi            v0.4s, #0                                      // accumulator
1:      ld1             {v1.4s}, [x1], #16                             // src[0..3]
        ld1             {v2.4s}, [x2], #16                             // filter[0..3]
        fmla            v0.4s, v1.4s, v2.4s                            // accumulator += src[0..3] * filter[0..3]
        subs            w3, w3, #4                                     // filter_length -= 4
        b.gt            1b                                             // loop until filter_length
        faddp           v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        faddp           v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        st1             {v0.s}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x8_float_neon, export=1
        movi            v0.4s, #0                                      // accumulator
1:      ld1             {v1.4s}, [x1], #16                             // src[0..3]
        ld1             {v2.4s}, [x2], #16                             // filter[0..3]
        ld1             {v3.4s}, [x1], #16                             // src[4..7]
        ld1             {v4.4s}, [x2], #16                             // filter[4..7]
        fmla            v0.4s, v1.4s, v2.4s                            // accumulator += src[0..3] * filter[0..3]
        fmla            v0.4s, v3.4s, v4.4s                            // accumulator += src[4..7] * filter[4..7]
        subs            w3, w3, #8                                     // filter_length -= 8
        b.gt            1b                                             // loop until filter_length
        faddp           v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        faddp           v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        st1             {v0.s}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x4_s16_neon, export=1
        movi            v0.4s, #0                                      // accumulator
1:      ld1             {v1.4h}, [x1], #8                              // src[0..3]
        ld1             {v2.4h}, [x2], #8                              // filter[0..3]
        smlal           v0.4s, v1.4h, v2.4h                            // accumulator += src[0..3] * filter[0..3]
        subs            w3, w3, #4                                     // filter_length -= 4
        b.gt            1b                                             // loop until filter_length
        addp            v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        addp            v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        st1             {v0.s}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x8_s16_neon, export=1
        movi            v0.4s, #0                                      // accumulator
1:      ld1             {v1.8h}, [x1], #16                             // src[0..7]
        ld1             {v2.8h}, [x2], #16                             // filter[0..7]
        smlal           v0.4s, v1.4h, v2.4h                            // accumulator += src[0..3] * filter[0..3]
        smlal2          v0.4s, v1.8h, v2.8h                            // accumulator += src[4..7] * filter[4..7]
        subs            w3, w3, #8                                     // filter_length -= 8
        b.gt            1b                                             // loop until filter_length
        addp            v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        addp            v0.4s, v0.4s, v0.4s                            // pair adding of the 4x32-bit accumulated values
        st1             {v0.s}[0], [x0], #4                            // write accumulator
    ret
endfunc
