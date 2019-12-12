/*
 * Copyright (c) 2017 Meng Wang <wangmeng.kids@bytedance.com>
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
#include "neon.S"

function ff_hevc_sao_band_filter_neon_8, export=1
        push    {r4-r10}
        ldr     r5,  [sp, #28]   // width
        ldr     r4,  [sp, #32]   // height
        ldr     r8,  [sp, #36]   // offset_table
        vpush   {d8-d15}
        mov     r12,  r4         // r12 = height
        mov     r6,   r0         // r6 = r0 = dst
        mov     r7,   r1         // r7 = r1 = src
        vldm    r8,   {q0-q3}
        vmov.u16    q15,  #1
        vmov.u8     q14,  #32
0:      pld      [r1]
        vld1.8   {d16},  [r1], r3
        cmp      r5,    #4
        beq      4f
8:      subs     r4,    #1
        vshr.u8  d17,   d16,  #3   // index = [src>>3]
        vshll.u8 q9,    d17,  #1   // lowIndex = 2*index
        vadd.u16 q11,   q9,   q15  // highIndex = (2*index+1) << 8
        vshl.u16 q10,   q11,  #8   // q10: highIndex;  q9: lowIndex;
        vadd.u16 q10,   q9         // combine high and low index;
        // Look-up Table Round 1; index range: 0-15
        vtbx.8   d24,   {q0-q1},   d20
        vtbx.8   d25,   {q0-q1},   d21
        // Look-up Table Round 2; index range: 16-31
        vsub.u8  q10,   q14        // Look-up with 8bit
        vtbx.8   d24,   {q2-q3},   d20
        vtbx.8   d25,   {q2-q3},   d21
        vaddw.u8 q13,   q12,       d16
        vqmovun.s16      d8,         q13
        vst1.8    d8,   [r0],      r2
        vld1.8   {d16}, [r1],      r3
        bne      8b
        subs     r5,    #8
        beq      99f
        mov      r4,    r12
        add r6, #8
        mov r0, r6
        add r7, #8
        mov r1, r7
        b        0b
4:      subs     r4,    #1
        vshr.u8  d17,   d16,  #3  // src>>3
        vshll.u8 q9,    d17,  #1   // lowIndex = 2*index
        vadd.u16 q11,   q9,   q15  // highIndex = (2*index+1) << 8
        vshl.u16 q10,   q11,  #8   // q10: highIndex;  q9: lowIndex;
        vadd.u16 q10,   q9         // combine high and low index;
        // Look-up Table Round 1; index range: 0-15
        vtbx.8   d24,   {q0-q1},   d20
        vtbx.8   d25,   {q0-q1},   d21
        // Look-up Table Round 2; index range: 16-32
        vsub.u8  q10,   q14        // Look-up with 8bit
        vtbx.8   d24,   {q2-q3},   d20
        vtbx.8   d25,   {q2-q3},   d21
        vaddw.u8 q13,   q12,       d16
        vqmovun.s16     d14,       q13
        vst1.32   d14[0],    [r0],     r2
        vld1.32   {d16[0]},  [r1],     r3
        bne      4b
        b        99f
99:
        vpop {d8-d15}
        pop  {r4-r10}
        bx   lr
endfunc

function ff_hevc_sao_edge_filter_neon_8, export=1
        push    {r4-r11}
        ldr     r5,  [sp, #32]   // width
        ldr     r4,  [sp, #36]   // height
        ldr     r8,  [sp, #40]   // a_stride
        ldr     r9,  [sp, #44]   // b_stride
        ldr     r10, [sp, #48]   // sao_offset_val
        ldr     r11, [sp, #52]   // edge_idx
        vpush   {d8-d15}
        mov     r12,  r4         // r12 = height
        mov     r6,   r0         // r6 = r0 = dst
        mov     r7,   r1         // r7 = r1 = src
        vld1.8  {d0}, [r11]      // edge_idx tabel load in d0 5x8bit
        vld1.16 {q1}, [r10]      // sao_offset_val table load in q1, 5x16bit
        vmov.u8  d1,  #2
        vmov.u16 q2,  #1
0:      mov      r10,    r1
        add      r10,    r8           // src[x + a_stride]
        mov      r11,    r1
        add      r11,    r9           // src[x + b_stride]
        pld      [r1]
        vld1.8   {d16},  [r1],  r3    // src[x]  8x8bit
        vld1.8   {d17},  [r10], r3    // src[x + a_stride]
        vld1.8   {d18},  [r11], r3    // src[x + b_stride]
        cmp      r5,     #4
        beq      4f
8:      subs     r4,     #1
        vcgt.u8  d8,     d16,   d17
        vshr.u8  d9,     d8,    #7
        vclt.u8  d8,     d16,   d17
        vadd.u8  d8,     d9           // diff0
        vcgt.u8  d10,    d16,   d18
        vshr.u8  d11,    d10,   #7
        vclt.u8  d10,    d16,   d18
        vadd.u8  d10,    d11          // diff1
        vadd.s8  d8,     d10
        vadd.s8  d8,     d1
        vtbx.8   d9,     {d0},  d8    // offset_val
        vshll.u8 q6,     d9,    #1    // lowIndex
        vadd.u16 q7,     q6,    q2
        vshl.u16 q10,    q7,    #8    // highIndex
        vadd.u16 q10,    q6           // combine lowIndex and highIndex, offset_val
        vtbx.8   d22,    {q1},  d20
        vtbx.8   d23,    {q1},  d21
        vaddw.u8 q12,    q11,   d16
        vqmovun.s16      d26,   q12
        vst1.8   d26,    [r0],  r2
        vld1.8   {d16},  [r1],  r3    // src[x]  8x8bit
        vld1.8   {d17},  [r10], r3    // src[x + a_stride]
        vld1.8   {d18},  [r11], r3    // src[x + b_stride]
        bne      8b
        subs     r5,     #8
        beq      99f
        mov      r4,     r12
        add      r6,     #8
        mov      r0,     r6
        add      r7,     #8
        mov      r1,     r7
        b        0b
4:      subs     r4,    #1
        vcgt.u8  d8,     d16,   d17
        vshr.u8  d9,     d8,    #7
        vclt.u8  d8,     d16,   d17
        vadd.u8  d8,     d9           // diff0
        vcgt.u8  d10,    d16,   d18
        vshr.u8  d11,    d10,   #7
        vclt.u8  d10,    d16,   d18
        vadd.u8  d10,    d11          // diff1
        vadd.s8  d8,     d10
        vadd.s8  d8,     d1
        vtbx.8   d9,     {d0},  d8    // offset_val
        vshll.u8 q6,     d9,    #1    // lowIndex
        vadd.u16 q7,     q6,    q2
        vshl.u16 q10,    q7,    #8    // highIndex
        vadd.u16 q10,    q6           // combine lowIndex and highIndex, offset_val
        vtbx.8   d22,    {q1},  d20
        vtbx.8   d23,    {q1},  d21
        vaddw.u8 q12,    q11,   d16
        vqmovun.s16      d26,   q12
        vst1.32  d26[0], [r0],  r2
        vld1.32   {d16[0]},  [r1],  r3
        vld1.32   {d17[0]},  [r10], r3    // src[x + a_stride]
        vld1.32   {d18[0]},  [r11], r3    // src[x + b_stride]
        bne      4b
        b        99f
99:
        vpop {d8-d15}
        pop  {r4-r11}
        bx   lr
endfunc
