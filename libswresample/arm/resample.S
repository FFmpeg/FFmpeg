/*
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include "libavutil/arm/asm.S"

function ff_resample_common_apply_filter_x4_float_neon, export=1
    vmov.f32            q0, #0.0                                       @ accumulator
1:  vld1.32             {q1}, [r1]!                                    @ src
    vld1.32             {q2}, [r2]!                                    @ filter
    vmla.f32            q0, q1, q2                                     @ accumulator += src + {0..3} * filter + {0..3}
    subs                r3, #4                                         @ filter_length -= 4
    bgt                 1b                                             @ loop until filter_length
    vpadd.f32           d0, d0, d1                                     @ pair adding of the 4x32-bit accumulated values
    vpadd.f32           d0, d0, d0                                     @ pair adding of the 4x32-bit accumulator values
    vst1.32             {d0[0]}, [r0]                                  @ write accumulator
    mov pc, lr
endfunc

function ff_resample_common_apply_filter_x8_float_neon, export=1
    vmov.f32            q0, #0.0                                       @ accumulator
1:  vld1.32             {q1}, [r1]!                                    @ src
    vld1.32             {q2}, [r2]!                                    @ filter
    vld1.32             {q8}, [r1]!                                    @ src
    vld1.32             {q9}, [r2]!                                    @ filter
    vmla.f32            q0, q1, q2                                     @ accumulator += src + {0..3} * filter + {0..3}
    vmla.f32            q0, q8, q9                                     @ accumulator += src + {4..7} * filter + {4..7}
    subs                r3, #8                                         @ filter_length -= 8
    bgt                 1b                                             @ loop until filter_length
    vpadd.f32           d0, d0, d1                                     @ pair adding of the 4x32-bit accumulated values
    vpadd.f32           d0, d0, d0                                     @ pair adding of the 4x32-bit accumulator values
    vst1.32             {d0[0]}, [r0]                                  @ write accumulator
    mov pc, lr
endfunc

function ff_resample_common_apply_filter_x4_s16_neon, export=1
    vmov.s32            q0, #0                                         @ accumulator
1:  vld1.16             {d2}, [r1]!                                    @ src
    vld1.16             {d4}, [r2]!                                    @ filter
    vmlal.s16           q0, d2, d4                                     @ accumulator += src + {0..3} * filter + {0..3}
    subs                r3, #4                                         @ filter_length -= 4
    bgt                 1b                                             @ loop until filter_length
    vpadd.s32           d0, d0, d1                                     @ pair adding of the 4x32-bit accumulated values
    vpadd.s32           d0, d0, d0                                     @ pair adding of the 4x32-bit accumulator values
    vst1.32             {d0[0]}, [r0]                                  @ write accumulator
    mov pc, lr
endfunc

function ff_resample_common_apply_filter_x8_s16_neon, export=1
    vmov.s32            q0, #0                                         @ accumulator
1:  vld1.16             {q1}, [r1]!                                    @ src
    vld1.16             {q2}, [r2]!                                    @ filter
    vmlal.s16           q0, d2, d4                                     @ accumulator += src + {0..3} * filter + {0..3}
    vmlal.s16           q0, d3, d5                                     @ accumulator += src + {4..7} * filter + {4..7}
    subs                r3, #8                                         @ filter_length -= 8
    bgt                 1b                                             @ loop until filter_length
    vpadd.s32           d0, d0, d1                                     @ pair adding of the 4x32-bit accumulated values
    vpadd.s32           d0, d0, d0                                     @ pair adding of the 4x32-bit accumulator values
    vst1.32             {d0[0]}, [r0]                                  @ write accumulator
    mov pc, lr
endfunc
