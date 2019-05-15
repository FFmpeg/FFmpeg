;*****************************************************************************
;* x86-optimized functions for gblur filter
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

; void ff_horiz_slice_sse4(float *ptr, int width, int height, int steps,
;                          float nu, float bscale)

%macro HORIZ_SLICE 0
%if UNIX64
cglobal horiz_slice, 4, 9, 9, ptr, width, height, steps, x, y, step, stride, remain
%else
cglobal horiz_slice, 4, 9, 9, ptr, width, height, steps, nu, bscale, x, y, step, stride, remain
%endif
%if WIN64
    movss m0, num
    movss m1, bscalem
    DEFINE_ARGS ptr, width, height, steps, x, y, step, stride, remain
%endif
    movsxdifnidn widthq, widthd

    mulss m2, m0, m0 ; nu ^ 2
    mulss m3, m2, m0 ; nu ^ 3
    mulss m4, m3, m0 ; nu ^ 4
    xor   xq, xq
    xor   yd, yd
    mov   strideq, widthq
    ; stride = width * 4
    shl   strideq, 2
    ; w = w - ((w - 1) & 3)
    mov   remainq, widthq
    sub   remainq, 1
    and   remainq, 3
    sub   widthq, remainq

    shufps m0, m0, 0
    shufps m2, m2, 0
    shufps m3, m3, 0
    shufps m4, m4, 0

.loop_y:
    xor   stepd, stepd

    .loop_step:
        ; p0 *= bscale
        mulss m5, m1, [ptrq + xq * 4]
        movss [ptrq + xq * 4], m5
        inc xq
        ; filter rightwards
        ; Here we are vectorizing the c version by 4
        ;    for (x = 1; x < width; x++)
        ;       ptr[x] += nu * ptr[x - 1];
        ;   let p0 stands for ptr[x-1], the data from last loop
        ;   and [p1,p2,p3,p4] be the vector data for this loop.
        ; Unrolling the loop, we get:
        ;   p1' = p1 + p0*nu
        ;   p2' = p2 + p1*nu + p0*nu^2
        ;   p3' = p3 + p2*nu + p1*nu^2 + p0*nu^3
        ;   p4' = p4 + p3*nu + p2*nu^2 + p1*nu^3 + p0*nu^4
        ; so we can do it in simd:
        ; [p1',p2',p3',p4'] = [p1,p2,p3,p4] + [p0,p1,p2,p3]*nu +
        ;                     [0,p0,p1,p2]*nu^2 + [0,0,p0,p1]*nu^3 +
        ;                     [0,0,0,p0]*nu^4

        .loop_x:
            movu m6, [ptrq + xq * 4]         ; s  = [p1,p2,p3,p4]
            pslldq m7, m6, 4                 ;      [0, p1,p2,p3]
            movss  m7, m5                    ;      [p0,p1,p2,p3]
            FMULADD_PS  m6, m7, m0, m6, m8   ; s += [p0,p1,p2,p3] * nu
            pslldq m7, 4                     ;      [0,p0,p1,p2]
            FMULADD_PS  m6, m7, m2, m6, m8   ; s += [0,p0,p1,p2]  * nu^2
            pslldq m7, 4
            FMULADD_PS  m6, m7, m3, m6, m8   ; s += [0,0,p0,p1]   * nu^3
            pslldq m7, 4
            FMULADD_PS  m6, m7, m4, m6, m8   ; s += [0,0,0,p0]    * nu^4
            movu [ptrq + xq * 4], m6
            shufps m5, m6, m6, q3333
            add xq, 4
            cmp xq, widthq
            jl .loop_x

        add widthq, remainq
        cmp xq, widthq
        je .end_scalar

        .loop_scalar:
            ; ptr[x] += nu * ptr[x-1]
            movss m5, [ptrq + 4*xq - 4]
            mulss m5, m0
            addss m5, [ptrq + 4*xq]
            movss [ptrq + 4*xq], m5
            inc xq
            cmp xq, widthq
            jl .loop_scalar
        .end_scalar:
            ; ptr[width - 1] *= bscale
            dec xq
            mulss m5, m1, [ptrq + 4*xq]
            movss [ptrq + 4*xq], m5
            shufps m5, m5, 0

        ; filter leftwards
        ;    for (; x > 0; x--)
        ;        ptr[x - 1] += nu * ptr[x];
        ; The idea here is basically the same as filter rightwards.
        ; But we need to take care as the data layout is different.
        ; Let p0 stands for the ptr[x], which is the data from last loop.
        ; The way we do it in simd as below:
        ; [p-4', p-3', p-2', p-1'] = [p-4, p-3, p-2, p-1]
        ;                          + [p-3, p-2, p-1, p0] * nu
        ;                          + [p-2, p-1, p0,  0]  * nu^2
        ;                          + [p-1, p0,  0,   0]  * nu^3
        ;                          + [p0,  0,   0,   0]  * nu^4
        .loop_x_back:
            sub xq, 4
            movu m6, [ptrq + xq * 4]      ; s = [p-4, p-3, p-2, p-1]
            psrldq m7, m6, 4              ;     [p-3, p-2, p-1, 0  ]
            blendps m7, m5, 0x8           ;     [p-3, p-2, p-1, p0 ]
            FMULADD_PS m6, m7, m0, m6, m8 ; s+= [p-3, p-2, p-1, p0 ] * nu
            psrldq m7, 4                  ;
            FMULADD_PS m6, m7, m2, m6, m8 ; s+= [p-2, p-1, p0,  0] * nu^2
            psrldq m7, 4
            FMULADD_PS m6, m7, m3, m6, m8 ; s+= [p-1, p0,   0,  0] * nu^3
            psrldq m7, 4
            FMULADD_PS m6, m7, m4, m6, m8 ; s+= [p0,  0,    0,  0] * nu^4
            movu [ptrq + xq * 4], m6
            shufps m5, m6, m6, 0          ; m5 = [p-4', p-4', p-4', p-4']
            cmp xq, remainq
            jg .loop_x_back

        cmp xq, 0
        je .end_scalar_back

        .loop_scalar_back:
            ; ptr[x-1] += nu * ptr[x]
            movss m5, [ptrq + 4*xq]
            mulss m5, m0
            addss m5, [ptrq + 4*xq - 4]
            movss [ptrq + 4*xq - 4], m5
            dec xq
            cmp xq, 0
            jg .loop_scalar_back
        .end_scalar_back:

        ; reset aligned width for next line
        sub widthq, remainq

        inc stepd
        cmp stepd, stepsd
        jl .loop_step

    add ptrq, strideq
    inc yd
    cmp yd, heightd
    jl .loop_y

    RET
%endmacro

%if ARCH_X86_64
INIT_XMM sse4
HORIZ_SLICE

INIT_XMM avx2
HORIZ_SLICE
%endif
