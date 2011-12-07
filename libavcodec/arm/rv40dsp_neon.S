/*
 * Copyright (c) 2011 Janne Grunau <janne-libav@jannau.net>
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "asm.S"
#include "neon.S"

.macro  qpel_lowpass    r0,  r1,  rc1, rc2, shift
        vext.8          d25, \r0, \r1, #1       @ src[-1]
        vext.8          d26, \r0, \r1, #4       @ src[ 2]
        vext.8          d24, \r0, \r1, #5       @ src[ 3]
        vaddl.u8        q9,  d25, d26
        vaddl.u8        q8,  \r0, d24
        vext.8          d27, \r0, \r1, #2       @ src[ 0]
        vshl.s16        q12, q9,  #2
        vsub.s16        q8,  q8,  q9
        vext.8          d28, \r0, \r1, #3       @ src[ 1]
        vsub.s16        q8,  q8,  q12
        vmlal.u8        q8,  d27, \rc1
        vmlal.u8        q8,  d28, \rc2
        vqrshrun.s16    \r0, q8,  #\shift
.endm

.macro  qpel_lowpass_x2 r0,  r1,  r2,  r3,  rc1, rc2, shift
        vext.8          d25, \r0, \r1, #1       @ src[-1]
        vext.8          d26, \r0, \r1, #4       @ src[ 2]
        vext.8          d24, \r0, \r1, #5       @ src[ 3]
        vaddl.u8        q9,  d25, d26
        vaddl.u8        q8,  \r0, d24
        vext.8          d29, \r0, \r1, #2       @ src[ 0]
        vext.8          d28, \r0, \r1, #3       @ src[ 1]
        vshl.s16        q10, q9,  #2
        vext.8          \r1, \r2, \r3, #1       @ src[-1]
        vsub.s16        q8,  q8,  q9
        vext.8          d22, \r2, \r3, #4       @ src[ 2]
        vext.8          \r0, \r2, \r3, #5       @ src[ 3]
        vaddl.u8        q13, \r1, d22
        vaddl.u8        q12, \r2, \r0
        vsub.s16        q8,  q8,  q10
        vshl.s16        q9,  q13, #2
        vsub.s16        q12, q12, q13
        vmlal.u8        q8,  d29, \rc1
        vmlal.u8        q8,  d28, \rc2
        vsub.s16        q12, q12, q9
        vext.8          d26, \r2, \r3, #2       @ src[ 0]
        vext.8          d27, \r2, \r3, #3       @ src[ 1]
        vmlal.u8        q12, d26, \rc1
        vmlal.u8        q12, d27, \rc2
        vqrshrun.s16    \r0, q8,  #\shift
        vqrshrun.s16    \r2, q12, #\shift
.endm

.macro  rv40_qpel8_h    shift
function put_rv40_qpel8_h_lp_packed_s\shift\()_neon
1:
        vld1.8          {q2},     [r1], r2
        vld1.8          {q3},     [r1], r2
        qpel_lowpass_x2 d4,  d5,  d6,  d7,  d0,  d1,  \shift
        vst1.8          {d4},     [r12,:64]!
        vst1.8          {d6},     [r12,:64]!
        subs            r3,  r3,  #2
        bgt             1b
        vld1.8          {q2},     [r1]
        qpel_lowpass    d4,  d5,  d0,  d1,  \shift
        vst1.8          {d4},     [r12,:64]!
        bx              lr
endfunc
.endm

.macro  rv40_qpel8_v    shift, type
function \type\()_rv40_qpel8_v_lp_packed_s\shift\()_neon
        vld1.64         {d2},     [r1,:64]!
        vld1.64         {d3},     [r1,:64]!
        vld1.64         {d4},     [r1,:64]!
        vld1.64         {d5},     [r1,:64]!
        vld1.64         {d6},     [r1,:64]!
        vld1.64         {d7},     [r1,:64]!
        vld1.64         {d8},     [r1,:64]!
        vld1.64         {d9},     [r1,:64]!
        vld1.64         {d10},    [r1,:64]!
        vld1.64         {d11},    [r1,:64]!
        vld1.64         {d12},    [r1,:64]!
        vld1.64         {d13},    [r1,:64]!
        vld1.64         {d14},    [r1,:64]!
        transpose_8x8   d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9
        transpose_8x8   d10, d11, d12, d13, d14, d15, d30, d31
        qpel_lowpass_x2 d2,  d10, d3,  d11, d0,  d1,  \shift
        qpel_lowpass_x2 d4,  d12, d5,  d13, d0,  d1,  \shift
        qpel_lowpass_x2 d6,  d14, d7,  d15, d0,  d1,  \shift
        qpel_lowpass_x2 d8,  d30, d9,  d31, d0,  d1,  \shift
        transpose_8x8   d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9
  .ifc \type,avg
        vld1.64         d12,      [r0,:64], r2
        vld1.64         d13,      [r0,:64], r2
        vld1.64         d14,      [r0,:64], r2
        vld1.64         d15,      [r0,:64], r2
        vld1.64         d16,      [r0,:64], r2
        vld1.64         d17,      [r0,:64], r2
        vld1.64         d18,      [r0,:64], r2
        vld1.64         d19,      [r0,:64], r2
        sub             r0,  r0,  r2,  lsl #3
        vrhadd.u8       q1,  q1,  q6
        vrhadd.u8       q2,  q2,  q7
        vrhadd.u8       q3,  q3,  q8
        vrhadd.u8       q4,  q4,  q9
  .endif
        vst1.64         d2,       [r0,:64], r2
        vst1.64         d3,       [r0,:64], r2
        vst1.64         d4,       [r0,:64], r2
        vst1.64         d5,       [r0,:64], r2
        vst1.64         d6,       [r0,:64], r2
        vst1.64         d7,       [r0,:64], r2
        vst1.64         d8,       [r0,:64], r2
        vst1.64         d9,       [r0,:64], r2
        bx              lr
endfunc
.endm

        rv40_qpel8_h    5
        rv40_qpel8_h    6

.macro  rv40_qpel       type
function \type\()_rv40_qpel8_h_lowpass_neon
  .ifc \type,avg
        mov             r12, r0
  .endif
1:
        vld1.8          {q2},     [r1], r2
        vld1.8          {q3},     [r1], r2
        qpel_lowpass_x2 d4,  d5,  d6,  d7,  d0,  d1,  6
  .ifc \type,avg
        vld1.8          {d3},     [r12,:64], r2
        vld1.8          {d16},    [r12,:64], r2
        vrhadd.u8       d4,  d4,  d3
        vrhadd.u8       d6,  d6,  d16
  .endif
        vst1.8          {d4},     [r0,:64], r2
        vst1.8          {d6},     [r0,:64], r2
        subs            r3,  r3,  #2
        bgt             1b
        bx              lr
endfunc

function \type\()_rv40_qpel8_v_lowpass_neon
        vld1.64         {d2},     [r1], r2
        vld1.64         {d3},     [r1], r2
        vld1.64         {d4},     [r1], r2
        vld1.64         {d5},     [r1], r2
        vld1.64         {d6},     [r1], r2
        vld1.64         {d7},     [r1], r2
        vld1.64         {d8},     [r1], r2
        vld1.64         {d9},     [r1], r2
        vld1.64         {d10},    [r1], r2
        vld1.64         {d11},    [r1], r2
        vld1.64         {d12},    [r1], r2
        vld1.64         {d13},    [r1], r2
        vld1.64         {d14},    [r1]
        transpose_8x8   d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9
        transpose_8x8   d10, d11, d12, d13, d14, d15, d30, d31
        qpel_lowpass_x2 d2,  d10, d3,  d11, d0,  d1,  6
        qpel_lowpass_x2 d4,  d12, d5,  d13, d0,  d1,  6
        qpel_lowpass_x2 d6,  d14, d7,  d15, d0,  d1,  6
        qpel_lowpass_x2 d8,  d30, d9,  d31, d0,  d1,  6
        transpose_8x8   d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9
  .ifc \type,avg
        vld1.64         d12,      [r0,:64], r2
        vld1.64         d13,      [r0,:64], r2
        vld1.64         d14,      [r0,:64], r2
        vld1.64         d15,      [r0,:64], r2
        vld1.64         d16,      [r0,:64], r2
        vld1.64         d17,      [r0,:64], r2
        vld1.64         d18,      [r0,:64], r2
        vld1.64         d19,      [r0,:64], r2
        sub             r0,  r0,  r2,  lsl #3
        vrhadd.u8       q1,  q1,  q6
        vrhadd.u8       q2,  q2,  q7
        vrhadd.u8       q3,  q3,  q8
        vrhadd.u8       q4,  q4,  q9
  .endif
        vst1.64         d2,       [r0,:64], r2
        vst1.64         d3,       [r0,:64], r2
        vst1.64         d4,       [r0,:64], r2
        vst1.64         d5,       [r0,:64], r2
        vst1.64         d6,       [r0,:64], r2
        vst1.64         d7,       [r0,:64], r2
        vst1.64         d8,       [r0,:64], r2
        vst1.64         d9,       [r0,:64], r2
        bx              lr
endfunc

        rv40_qpel8_v    5, \type
        rv40_qpel8_v    6, \type

function ff_\type\()_rv40_qpel8_mc10_neon, export=1
        sub             r1,  r1,  #2
        mov             r3,  #8
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        b               \type\()_rv40_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_rv40_qpel8_mc30_neon, export=1
        sub             r1,  r1,  #2
        mov             r3,  #8
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        b               \type\()_rv40_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_rv40_qpel8_mc01_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             r1,  r1,  r2,  lsl #1
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc11_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc21_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vmov.i8         d0,  #52
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc31_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vswp            d0,  d1
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc12_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vmov.i8         d0,  #20
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc22_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc32_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vmov.i8         d1,  #20
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc03_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             r1,  r1,  r2,  lsl #1
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc33_neon, export=1
        mov             r3,  #8
        b               ff_\type\()_pixels8_xy2_neon
endfunc

function ff_\type\()_rv40_qpel8_mc13_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vswp            d0,  d1
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel8_mc23_neon, export=1
        push            {r4, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #14*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        mov             r3,  #12
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        vmov.i8         d1,  #52
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #14*8
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_rv40_qpel16_mc10_neon, export=1
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
.L\type\()_rv40_qpel16_h:
        push            {r1, lr}
        sub             r1,  r1,  #2
        mov             r3,  #16
        bl              \type\()_rv40_qpel8_h_lowpass_neon
        pop             {r1, lr}
        sub             r0,  r0,  r2,  lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #6
        mov             r3,  #16
        b               \type\()_rv40_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_rv40_qpel16_mc30_neon, export=1
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        b               .L\type\()_rv40_qpel16_h
endfunc

function ff_\type\()_rv40_qpel16_mc01_neon, export=1
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
.L\type\()_rv40_qpel16_v:
        sub             r1,  r1,  r2,  lsl #1
        push            {r1, lr}
        vpush           {d8-d15}
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        sub             r1,  r1,  r2,  lsl #2
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        ldr             r1,  [sp, #64]
        sub             r0,  r0,  r2,  lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        sub             r1,  r1,  r2,  lsl #2
        bl              \type\()_rv40_qpel8_v_lowpass_neon
        vpop            {d8-d15}
        pop             {r1, pc}
endfunc

function ff_\type\()_rv40_qpel16_mc11_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
.L\type\()_rv40_qpel16_v_s6:
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        sub             r1,  r1,  #40
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        sub             r0,  r0,  r2,  lsl #4
        add             r0,  r0,  #8
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        sub             r1,  r1,  #40
        bl              \type\()_rv40_qpel8_v_lp_packed_s6_neon
        add             sp,  sp,  #44*8
        vpop            {d8-d15}
        pop             {r1, pc}
endfunc

function ff_\type\()_rv40_qpel16_mc21_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        vmov.i8         d0,  #52
        b               .L\type\()_rv40_qpel16_v_s6
endfunc

function ff_\type\()_rv40_qpel16_mc31_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        vswp            d0,  d1
        b               .L\type\()_rv40_qpel16_v_s6
endfunc

function ff_\type\()_rv40_qpel16_mc12_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        vmov.i8         d0,  #20
.L\type\()_rv40_qpel16_v_s5:
        add             r1,  sp,  #7
        bic             r1,  r1,  #7
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        sub             r1,  r1,  #40
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        sub             r0,  r0,  r2,  lsl #4
        add             r0,  r0,  #8
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        sub             r1,  r1,  #40
        bl              \type\()_rv40_qpel8_v_lp_packed_s5_neon
        add             sp,  sp,  #44*8
        vpop            {d8-d15}
        pop             {r1, pc}
endfunc

function ff_\type\()_rv40_qpel16_mc22_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        b               .L\type\()_rv40_qpel16_v_s5
endfunc

function ff_\type\()_rv40_qpel16_mc32_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        vmov.i8         d1,  #20
        b               .L\type\()_rv40_qpel16_v_s5
endfunc

function ff_\type\()_rv40_qpel16_mc03_neon, export=1
        vmov.i8         d0,  #20
        vmov.i8         d1,  #52
        b               .L\type\()_rv40_qpel16_v
endfunc

function ff_\type\()_rv40_qpel16_mc13_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #52
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s6_neon
        vswp            d0,  d1
        b               .L\type\()_rv40_qpel16_v_s6
endfunc

function ff_\type\()_rv40_qpel16_mc23_neon, export=1
        sub             r1,  r1,  r2,  lsl #1
        sub             r1,  r1,  #2
        push            {r1, lr}
        vpush           {d8-d15}
        sub             sp,  sp,  #44*8
        add             r12, sp,  #7
        bic             r12, r12, #7
        mov             r3,  #20
        vmov.i8         d0,  #20
        vmov.i8         d1,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        ldr             r1,  [sp, #416]
        add             r1,  r1,  #8
        mov             r3,  #20
        bl              put_rv40_qpel8_h_lp_packed_s5_neon
        vmov.i8         d1,  #52
        b               .L\type\()_rv40_qpel16_v_s6
endfunc

function ff_\type\()_rv40_qpel16_mc33_neon, export=1
        mov             r3,  #16
        b               ff_\type\()_pixels16_xy2_neon
endfunc
.endm

        rv40_qpel       put
        rv40_qpel       avg

.macro  rv40_weight
        vmovl.u8        q8,  d2
        vmovl.u8        q9,  d3
        vmovl.u8        q10, d4
        vmovl.u8        q11, d5
        vmull.u16       q2,  d16, d0[2]
        vmull.u16       q3,  d17, d0[2]
        vmull.u16       q8,  d18, d0[2]
        vmull.u16       q9,  d19, d0[2]
        vmull.u16       q12, d20, d0[0]
        vmull.u16       q13, d21, d0[0]
        vmull.u16       q14, d22, d0[0]
        vmull.u16       q15, d23, d0[0]
        vshrn.i32       d4,  q2,  #9
        vshrn.i32       d5,  q3,  #9
        vshrn.i32       d6,  q8,  #9
        vshrn.i32       d7,  q9,  #9
        vshrn.i32       d16, q12, #9
        vshrn.i32       d17, q13, #9
        vshrn.i32       d18, q14, #9
        vshrn.i32       d19, q15, #9
        vadd.u16        q2,  q2,  q8
        vadd.u16        q3,  q3,  q9
        vrshrn.i16      d2,  q2,  #5
        vrshrn.i16      d3,  q3,  #5
.endm

/* void ff_rv40_weight_func_16_neon(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                    int w1, int w2, int stride) */
function ff_rv40_weight_func_16_neon, export=1
        ldr             r12, [sp]
        vmov            d0,  r3,  r12
        ldr             r12, [sp, #4]
        mov             r3,  #16
1:
        vld1.8          {q1},     [r1,:128], r12
        vld1.8          {q2},     [r2,:128], r12
        rv40_weight
        vst1.8          {q1},     [r0,:128], r12
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

/* void ff_rv40_weight_func_8_neon(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                   int w1, int w2, int stride) */
function ff_rv40_weight_func_8_neon, export=1
        ldr             r12, [sp]
        vmov            d0,  r3,  r12
        ldr             r12, [sp, #4]
        mov             r3,  #8
1:
        vld1.8          {d2},     [r1,:64], r12
        vld1.8          {d3},     [r1,:64], r12
        vld1.8          {d4},     [r2,:64], r12
        vld1.8          {d5},     [r2,:64], r12
        rv40_weight
        vst1.8          {d2},     [r0,:64], r12
        vst1.8          {d3},     [r0,:64], r12
        subs            r3,  r3,  #2
        bne             1b
        bx              lr
endfunc
