/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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
#include "asm.S"

/*
 * VFP is a floating point coprocessor used in some ARM cores. VFP11 has 1 cycle
 * throughput for almost all the instructions (except for double precision
 * arithmetics), but rather high latency. Latency is 4 cycles for loads and 8 cycles
 * for arithmetic operations. Scheduling code to avoid pipeline stalls is very
 * important for performance. One more interesting feature is that VFP has
 * independent load/store and arithmetics pipelines, so it is possible to make
 * them work simultaneously and get more than 1 operation per cycle. Load/store
 * pipeline can process 2 single precision floating point values per cycle and
 * supports bulk loads and stores for large sets of registers. Arithmetic operations
 * can be done on vectors, which allows to keep the arithmetics pipeline busy,
 * while the processor may issue and execute other instructions. Detailed
 * optimization manuals can be found at http://www.arm.com
 */

/**
 * ARM VFP optimized implementation of 'vector_fmul_c' function.
 * Assume that len is a positive number and is multiple of 8
 */
@ void ff_vector_fmul_vfp(float *dst, const float *src, int len)
function ff_vector_fmul_vfp, export=1
        vpush           {d8-d15}
        mov             r3,  r0
        fmrx            r12, fpscr
        orr             r12, r12, #(3 << 16) /* set vector size to 4 */
        fmxr            fpscr, r12

        fldmias         r3!, {s0-s3}
        fldmias         r1!, {s8-s11}
        fldmias         r3!, {s4-s7}
        fldmias         r1!, {s12-s15}
        fmuls           s8,  s0,  s8
1:
        subs            r2,  r2,  #16
        fmuls           s12, s4,  s12
        fldmiasge       r3!, {s16-s19}
        fldmiasge       r1!, {s24-s27}
        fldmiasge       r3!, {s20-s23}
        fldmiasge       r1!, {s28-s31}
        fmulsge         s24, s16, s24
        fstmias         r0!, {s8-s11}
        fstmias         r0!, {s12-s15}
        fmulsge         s28, s20, s28
        fldmiasgt       r3!, {s0-s3}
        fldmiasgt       r1!, {s8-s11}
        fldmiasgt       r3!, {s4-s7}
        fldmiasgt       r1!, {s12-s15}
        fmulsge         s8,  s0,  s8
        fstmiasge       r0!, {s24-s27}
        fstmiasge       r0!, {s28-s31}
        bgt             1b

        bic             r12, r12, #(7 << 16) /* set vector size back to 1 */
        fmxr            fpscr, r12
        vpop            {d8-d15}
        bx              lr
        .endfunc

/**
 * ARM VFP optimized implementation of 'vector_fmul_reverse_c' function.
 * Assume that len is a positive number and is multiple of 8
 */
@ void ff_vector_fmul_reverse_vfp(float *dst, const float *src0,
@                                 const float *src1, int len)
function ff_vector_fmul_reverse_vfp, export=1
        vpush           {d8-d15}
        add             r2,  r2,  r3, lsl #2
        fldmdbs         r2!, {s0-s3}
        fldmias         r1!, {s8-s11}
        fldmdbs         r2!, {s4-s7}
        fldmias         r1!, {s12-s15}
        fmuls           s8,  s3,  s8
        fmuls           s9,  s2,  s9
        fmuls           s10, s1,  s10
        fmuls           s11, s0,  s11
1:
        subs            r3,  r3,  #16
        fldmdbsge       r2!, {s16-s19}
        fmuls           s12, s7,  s12
        fldmiasge       r1!, {s24-s27}
        fmuls           s13, s6,  s13
        fldmdbsge       r2!, {s20-s23}
        fmuls           s14, s5,  s14
        fldmiasge       r1!, {s28-s31}
        fmuls           s15, s4,  s15
        fmulsge         s24, s19, s24
        fldmdbsgt       r2!, {s0-s3}
        fmulsge         s25, s18, s25
        fstmias         r0!, {s8-s13}
        fmulsge         s26, s17, s26
        fldmiasgt       r1!, {s8-s11}
        fmulsge         s27, s16, s27
        fmulsge         s28, s23, s28
        fldmdbsgt       r2!, {s4-s7}
        fmulsge         s29, s22, s29
        fstmias         r0!, {s14-s15}
        fmulsge         s30, s21, s30
        fmulsge         s31, s20, s31
        fmulsge         s8,  s3,  s8
        fldmiasgt       r1!, {s12-s15}
        fmulsge         s9,  s2,  s9
        fmulsge         s10, s1,  s10
        fstmiasge       r0!, {s24-s27}
        fmulsge         s11, s0,  s11
        fstmiasge       r0!, {s28-s31}
        bgt             1b

        vpop            {d8-d15}
        bx              lr
        .endfunc

#ifdef HAVE_ARMV6
/**
 * ARM VFP optimized float to int16 conversion.
 * Assume that len is a positive number and is multiple of 8, destination
 * buffer is at least 4 bytes aligned (8 bytes alignment is better for
 * performance), little endian byte sex
 */
@ void ff_float_to_int16_vfp(int16_t *dst, const float *src, int len)
function ff_float_to_int16_vfp, export=1
        push            {r4-r8,lr}
        vpush           {d8-d11}
        fldmias         r1!, {s16-s23}
        ftosis          s0,  s16
        ftosis          s1,  s17
        ftosis          s2,  s18
        ftosis          s3,  s19
        ftosis          s4,  s20
        ftosis          s5,  s21
        ftosis          s6,  s22
        ftosis          s7,  s23
1:
        subs            r2,  r2,  #8
        fmrrs           r3,  r4,  {s0, s1}
        fmrrs           r5,  r6,  {s2, s3}
        fmrrs           r7,  r8,  {s4, s5}
        fmrrs           ip,  lr,  {s6, s7}
        fldmiasgt       r1!, {s16-s23}
        ssat            r4,  #16, r4
        ssat            r3,  #16, r3
        ssat            r6,  #16, r6
        ssat            r5,  #16, r5
        pkhbt           r3,  r3,  r4, lsl #16
        pkhbt           r4,  r5,  r6, lsl #16
        ftosisgt        s0,  s16
        ftosisgt        s1,  s17
        ftosisgt        s2,  s18
        ftosisgt        s3,  s19
        ftosisgt        s4,  s20
        ftosisgt        s5,  s21
        ftosisgt        s6,  s22
        ftosisgt        s7,  s23
        ssat            r8,  #16, r8
        ssat            r7,  #16, r7
        ssat            lr,  #16, lr
        ssat            ip,  #16, ip
        pkhbt           r5,  r7,  r8, lsl #16
        pkhbt           r6,  ip,  lr, lsl #16
        stmia           r0!, {r3-r6}
        bgt             1b

        vpop            {d8-d11}
        pop             {r4-r8,pc}
        .endfunc
#endif
