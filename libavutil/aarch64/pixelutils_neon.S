/*
 * Copyright (c) 2022 Jonathan Swinney <jswinney@amazon.com>
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

#include "asm.S"

function ff_pixelutils_sad16_neon, export=1
        // x0           uint8_t *pix1
        // x1           ptrdiff_t stride1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride2
        movi            v16.8h, #0                  // clear result accumulator
        movi            v17.8h, #0                  // clear result accumulator
        mov             w4, 16
1:
        ld1             {v0.16b}, [x0], x1          // load pix1
        ld1             {v4.16b}, [x2], x3          // load pix2
        ld1             {v1.16b}, [x0], x1          // load pix1
        ld1             {v5.16b}, [x2], x3          // load pix2
        uabal           v16.8h, v0.8b, v4.8b        // absolute difference accumulate
        uabal2          v17.8h, v0.16b, v4.16b
        ld1             {v2.16b}, [x0], x1          // load pix1
        ld1             {v6.16b}, [x2], x3          // load pix2
        uabal           v16.8h, v1.8b, v5.8b        // absolute difference accumulate
        uabal2          v17.8h, v1.16b, v5.16b
        ld1             {v3.16b}, [x0], x1
        ld1             {v7.16b}, [x2], x3
        uabal           v16.8h, v2.8b, v6.8b
        uabal2          v17.8h, v2.16b, v6.16b
        subs            w4, w4, #4                  // h -= 4
        uabal           v16.8h, v3.8b, v7.8b
        uabal2          v17.8h, v3.16b, v7.16b

        b.gt            1b                          // if h > 0, loop

        add             v16.8h, v16.8h, v17.8h
        uaddlv          s16, v16.8h                 // add up everything in v16 accumulator
        fmov            w0, s16                     // copy result to general purpose register
        ret
endfunc

function ff_pixelutils_sad8_neon, export=1
        // x0           uint8_t *pix1
        // x1           ptrdiff_t stride1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride2

        movi            v30.8h, #0
        mov             w4, 8

// make 4 iterations at once
1:
        ld1             {v0.8b}, [x0], x1               // Load pix1 for first iteration
        ld1             {v1.8b}, [x2], x3               // Load pix2 for first iteration
        ld1             {v2.8b}, [x0], x1               // Load pix1 for second iteration
        uabal           v30.8h, v0.8b, v1.8b            // Absolute difference, first iteration
        ld1             {v3.8b}, [x2], x3               // Load pix2 for second iteration
        ld1             {v4.8b}, [x0], x1               // Load pix1 for third iteration
        uabal           v30.8h, v2.8b, v3.8b            // Absolute difference, second iteration
        ld1             {v5.8b}, [x2], x3               // Load pix2 for third iteration
        subs            w4, w4, #4                      // h -= 4
        ld1             {v6.8b}, [x0], x1               // Load pix1 for fourth iteration
        ld1             {v7.8b}, [x2], x3               // Load pix2 for fourth iteration
        uabal           v30.8h, v4.8b, v5.8b            // Absolute difference, third iteration
        uabal           v30.8h, v6.8b, v7.8b            // Absolute difference, fourth iteration
        b.gt            1b

        uaddlv          s20, v30.8h                     // Add up vector
        fmov            w0, s20

        ret
endfunc
