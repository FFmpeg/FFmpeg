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
    movi                v0.4S, #0                                      // accumulator
1:  ld1                 {v1.4S}, [x1], #16                             // src[0..3]
    ld1                 {v2.4S}, [x2], #16                             // filter[0..3]
    fmla                v0.4S, v1.4S, v2.4S                            // accumulator += src[0..3] * filter[0..3]
    subs                w3, w3, #4                                     // filter_length -= 4
    b.gt                1b                                             // loop until filter_length
    faddp               v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    faddp               v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    st1                 {v0.S}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x8_float_neon, export=1
    movi                v0.4S, #0                                      // accumulator
1:  ld1                 {v1.4S}, [x1], #16                             // src[0..3]
    ld1                 {v2.4S}, [x2], #16                             // filter[0..3]
    ld1                 {v3.4S}, [x1], #16                             // src[4..7]
    ld1                 {v4.4S}, [x2], #16                             // filter[4..7]
    fmla                v0.4S, v1.4S, v2.4S                            // accumulator += src[0..3] * filter[0..3]
    fmla                v0.4S, v3.4S, v4.4S                            // accumulator += src[4..7] * filter[4..7]
    subs                w3, w3, #8                                     // filter_length -= 8
    b.gt                1b                                             // loop until filter_length
    faddp               v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    faddp               v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    st1                 {v0.S}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x4_s16_neon, export=1
    movi                v0.4S, #0                                      // accumulator
1:  ld1                 {v1.4H}, [x1], #8                              // src[0..3]
    ld1                 {v2.4H}, [x2], #8                              // filter[0..3]
    smlal               v0.4S, v1.4H, v2.4H                            // accumulator += src[0..3] * filter[0..3]
    subs                w3, w3, #4                                     // filter_length -= 4
    b.gt                1b                                             // loop until filter_length
    addp                v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    addp                v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    st1                 {v0.S}[0], [x0], #4                            // write accumulator
    ret
endfunc

function ff_resample_common_apply_filter_x8_s16_neon, export=1
    movi                v0.4S, #0                                      // accumulator
1:  ld1                 {v1.8H}, [x1], #16                             // src[0..7]
    ld1                 {v2.8H}, [x2], #16                             // filter[0..7]
    smlal               v0.4S, v1.4H, v2.4H                            // accumulator += src[0..3] * filter[0..3]
    smlal2              v0.4S, v1.8H, v2.8H                            // accumulator += src[4..7] * filter[4..7]
    subs                w3, w3, #8                                     // filter_length -= 8
    b.gt                1b                                             // loop until filter_length
    addp                v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    addp                v0.4S, v0.4S, v0.4S                            // pair adding of the 4x32-bit accumulated values
    st1                 {v0.S}[0], [x0], #4                            // write accumulator
    ret
endfunc
