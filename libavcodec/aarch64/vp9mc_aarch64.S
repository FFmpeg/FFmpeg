/*
 * Copyright (c) 2016 Google Inc.
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

// All public functions in this file have the following signature:
// typedef void (*vp9_mc_func)(uint8_t *dst, ptrdiff_t dst_stride,
//                            const uint8_t *ref, ptrdiff_t ref_stride,
//                            int h, int mx, int my);

function ff_vp9_copy128_aarch64, export=1
1:
        ldp             x5,  x6,  [x2]
        ldp             x7,  x8,  [x2, #16]
        stp             x5,  x6,  [x0]
        ldp             x9,  x10, [x2, #32]
        stp             x7,  x8,  [x0, #16]
        subs            w4,  w4,  #1
        ldp             x11, x12, [x2, #48]
        stp             x9,  x10, [x0, #32]
        stp             x11, x12, [x0, #48]
        ldp             x5,  x6,  [x2, #64]
        ldp             x7,  x8,  [x2, #80]
        stp             x5,  x6,  [x0, #64]
        ldp             x9,  x10, [x2, #96]
        stp             x7,  x8,  [x0, #80]
        ldp             x11, x12, [x2, #112]
        stp             x9,  x10, [x0, #96]
        stp             x11, x12, [x0, #112]
        add             x2,  x2,  x3
        add             x0,  x0,  x1
        b.ne            1b
        ret
endfunc

function ff_vp9_copy64_aarch64, export=1
1:
        ldp             x5,  x6,  [x2]
        ldp             x7,  x8,  [x2, #16]
        stp             x5,  x6,  [x0]
        ldp             x9,  x10, [x2, #32]
        stp             x7,  x8,  [x0, #16]
        subs            w4,  w4,  #1
        ldp             x11, x12, [x2, #48]
        stp             x9,  x10, [x0, #32]
        stp             x11, x12, [x0, #48]
        add             x2,  x2,  x3
        add             x0,  x0,  x1
        b.ne            1b
        ret
endfunc

function ff_vp9_copy32_aarch64, export=1
1:
        ldp             x5,  x6,  [x2]
        ldp             x7,  x8,  [x2, #16]
        stp             x5,  x6,  [x0]
        subs            w4,  w4,  #1
        stp             x7,  x8,  [x0, #16]
        add             x2,  x2,  x3
        add             x0,  x0,  x1
        b.ne            1b
        ret
endfunc
