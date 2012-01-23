;******************************************************************************
;* MMX/SSE2-optimized functions for the VP3 decoder
;* Copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

; MMX-optimized functions cribbed from the original VP3 source code.

SECTION_RODATA

vp3_idct_data: times 8 dw 64277
               times 8 dw 60547
               times 8 dw 54491
               times 8 dw 46341
               times 8 dw 36410
               times 8 dw 25080
               times 8 dw 12785

cextern pb_1
cextern pb_3
cextern pb_7
cextern pb_1F
cextern pb_81

cextern pw_8

cextern put_signed_pixels_clamped_mmx
cextern add_pixels_clamped_mmx

SECTION .text

; this is off by one or two for some cases when filter_limit is greater than 63
; in:  p0 in mm6, p1 in mm4, p2 in mm2, p3 in mm1
; out: p1 in mm4, p2 in mm3
%macro VP3_LOOP_FILTER 0
    movq          m7, m6
    pand          m6, [pb_7]    ; p0&7
    psrlw         m7, 3
    pand          m7, [pb_1F]   ; p0>>3
    movq          m3, m2        ; p2
    pxor          m2, m4
    pand          m2, [pb_1]    ; (p2^p1)&1
    movq          m5, m2
    paddb         m2, m2
    paddb         m2, m5        ; 3*(p2^p1)&1
    paddb         m2, m6        ; extra bits lost in shifts
    pcmpeqb       m0, m0
    pxor          m1, m0        ; 255 - p3
    pavgb         m1, m2        ; (256 - p3 + extrabits) >> 1
    pxor          m0, m4        ; 255 - p1
    pavgb         m0, m3        ; (256 + p2-p1) >> 1
    paddb         m1, [pb_3]
    pavgb         m1, m0        ; 128+2+(   p2-p1  - p3) >> 2
    pavgb         m1, m0        ; 128+1+(3*(p2-p1) - p3) >> 3
    paddusb       m7, m1        ; d+128+1
    movq          m6, [pb_81]
    psubusb       m6, m7
    psubusb       m7, [pb_81]

    movq          m5, [r2+516]  ; flim
    pminub        m6, m5
    pminub        m7, m5
    movq          m0, m6
    movq          m1, m7
    paddb         m6, m6
    paddb         m7, m7
    pminub        m6, m5
    pminub        m7, m5
    psubb         m6, m0
    psubb         m7, m1
    paddusb       m4, m7
    psubusb       m4, m6
    psubusb       m3, m7
    paddusb       m3, m6
%endmacro

%macro STORE_4_WORDS 1
    movd         r2d, %1
    mov  [r0     -1], r2w
    psrlq         %1, 32
    shr           r2, 16
    mov  [r0+r1  -1], r2w
    movd         r2d, %1
    mov  [r0+r1*2-1], r2w
    shr           r2, 16
    mov  [r0+r3  -1], r2w
%endmacro

INIT_MMX
cglobal vp3_v_loop_filter_mmx2, 3, 4
%if ARCH_X86_64
    movsxd        r1, r1d
%endif
    mov           r3, r1
    neg           r1
    movq          m6, [r0+r1*2]
    movq          m4, [r0+r1  ]
    movq          m2, [r0     ]
    movq          m1, [r0+r3  ]

    VP3_LOOP_FILTER

    movq     [r0+r1], m4
    movq     [r0   ], m3
    RET

cglobal vp3_h_loop_filter_mmx2, 3, 4
%if ARCH_X86_64
    movsxd        r1, r1d
%endif
    lea           r3, [r1*3]

    movd          m6, [r0     -2]
    movd          m4, [r0+r1  -2]
    movd          m2, [r0+r1*2-2]
    movd          m1, [r0+r3  -2]
    lea           r0, [r0+r1*4  ]
    punpcklbw     m6, [r0     -2]
    punpcklbw     m4, [r0+r1  -2]
    punpcklbw     m2, [r0+r1*2-2]
    punpcklbw     m1, [r0+r3  -2]
    sub           r0, r3
    sub           r0, r1

    TRANSPOSE4x4B  6, 4, 2, 1, 0
    VP3_LOOP_FILTER
    SBUTTERFLY    bw, 4, 3, 5

    STORE_4_WORDS m4
    lea           r0, [r0+r1*4  ]
    STORE_4_WORDS m3
    RET

; from original comments: The Macro does IDct on 4 1-D Dcts
%macro BeginIDCT 0
    movq          m2, I(3)
    movq          m6, C(3)
    movq          m4, m2
    movq          m7, J(5)
    pmulhw        m4, m6        ; r4 = c3*i3 - i3
    movq          m1, C(5)
    pmulhw        m6, m7        ; r6 = c3*i5 - i5
    movq          m5, m1
    pmulhw        m1, m2        ; r1 = c5*i3 - i3
    movq          m3, I(1)
    pmulhw        m5, m7        ; r5 = c5*i5 - i5
    movq          m0, C(1)
    paddw         m4, m2        ; r4 = c3*i3
    paddw         m6, m7        ; r6 = c3*i5
    paddw         m2, m1        ; r2 = c5*i3
    movq          m1, J(7)
    paddw         m7, m5        ; r7 = c5*i5
    movq          m5, m0        ; r5 = c1
    pmulhw        m0, m3        ; r0 = c1*i1 - i1
    paddsw        m4, m7        ; r4 = C = c3*i3 + c5*i5
    pmulhw        m5, m1        ; r5 = c1*i7 - i7
    movq          m7, C(7)
    psubsw        m6, m2        ; r6 = D = c3*i5 - c5*i3
    paddw         m0, m3        ; r0 = c1*i1
    pmulhw        m3, m7        ; r3 = c7*i1
    movq          m2, I(2)
    pmulhw        m7, m1        ; r7 = c7*i7
    paddw         m5, m1        ; r5 = c1*i7
    movq          m1, m2        ; r1 = i2
    pmulhw        m2, C(2)      ; r2 = c2*i2 - i2
    psubsw        m3, m5        ; r3 = B = c7*i1 - c1*i7
    movq          m5, J(6)
    paddsw        m0, m7        ; r0 = A = c1*i1 + c7*i7
    movq          m7, m5        ; r7 = i6
    psubsw        m0, m4        ; r0 = A - C
    pmulhw        m5, C(2)      ; r5 = c2*i6 - i6
    paddw         m2, m1        ; r2 = c2*i2
    pmulhw        m1, C(6)      ; r1 = c6*i2
    paddsw        m4, m4        ; r4 = C + C
    paddsw        m4, m0        ; r4 = C. = A + C
    psubsw        m3, m6        ; r3 = B - D
    paddw         m5, m7        ; r5 = c2*i6
    paddsw        m6, m6        ; r6 = D + D
    pmulhw        m7, C(6)      ; r7 = c6*i6
    paddsw        m6, m3        ; r6 = D. = B + D
    movq        I(1), m4        ; save C. at I(1)
    psubsw        m1, m5        ; r1 = H = c6*i2 - c2*i6
    movq          m4, C(4)
    movq          m5, m3        ; r5 = B - D
    pmulhw        m3, m4        ; r3 = (c4 - 1) * (B - D)
    paddsw        m7, m2        ; r3 = (c4 - 1) * (B - D)
    movq        I(2), m6        ; save D. at I(2)
    movq          m2, m0        ; r2 = A - C
    movq          m6, I(0)
    pmulhw        m0, m4        ; r0 = (c4 - 1) * (A - C)
    paddw         m5, m3        ; r5 = B. = c4 * (B - D)
    movq          m3, J(4)
    psubsw        m5, m1        ; r5 = B.. = B. - H
    paddw         m2, m0        ; r0 = A. = c4 * (A - C)
    psubsw        m6, m3        ; r6 = i0 - i4
    movq          m0, m6
    pmulhw        m6, m4        ; r6 = (c4 - 1) * (i0 - i4)
    paddsw        m3, m3        ; r3 = i4 + i4
    paddsw        m1, m1        ; r1 = H + H
    paddsw        m3, m0        ; r3 = i0 + i4
    paddsw        m1, m5        ; r1 = H. = B + H
    pmulhw        m4, m3        ; r4 = (c4 - 1) * (i0 + i4)
    paddsw        m6, m0        ; r6 = F = c4 * (i0 - i4)
    psubsw        m6, m2        ; r6 = F. = F - A.
    paddsw        m2, m2        ; r2 = A. + A.
    movq          m0, I(1)      ; r0 = C.
    paddsw        m2, m6        ; r2 = A.. = F + A.
    paddw         m4, m3        ; r4 = E = c4 * (i0 + i4)
    psubsw        m2, m1        ; r2 = R2 = A.. - H.
%endmacro

; RowIDCT gets ready to transpose
%macro RowIDCT 0
    BeginIDCT
    movq          m3, I(2)      ; r3 = D.
    psubsw        m4, m7        ; r4 = E. = E - G
    paddsw        m1, m1        ; r1 = H. + H.
    paddsw        m7, m7        ; r7 = G + G
    paddsw        m1, m2        ; r1 = R1 = A.. + H.
    paddsw        m7, m4        ; r1 = R1 = A.. + H.
    psubsw        m4, m3        ; r4 = R4 = E. - D.
    paddsw        m3, m3
    psubsw        m6, m5        ; r6 = R6 = F. - B..
    paddsw        m5, m5
    paddsw        m3, m4        ; r3 = R3 = E. + D.
    paddsw        m5, m6        ; r5 = R5 = F. + B..
    psubsw        m7, m0        ; r7 = R7 = G. - C.
    paddsw        m0, m0
    movq        I(1), m1        ; save R1
    paddsw        m0, m7        ; r0 = R0 = G. + C.
%endmacro

; Column IDCT normalizes and stores final results
%macro ColumnIDCT 0
    BeginIDCT
    paddsw        m2, OC_8      ; adjust R2 (and R1) for shift
    paddsw        m1, m1        ; r1 = H. + H.
    paddsw        m1, m2        ; r1 = R1 = A.. + H.
    psraw         m2, 4         ; r2 = NR2
    psubsw        m4, m7        ; r4 = E. = E - G
    psraw         m1, 4         ; r1 = NR2
    movq          m3, I(2)      ; r3 = D.
    paddsw        m7, m7        ; r7 = G + G
    movq        I(2), m2        ; store NR2 at I2
    paddsw        m7, m4        ; r7 = G. = E + G
    movq        I(1), m1        ; store NR1 at I1
    psubsw        m4, m3        ; r4 = R4 = E. - D.
    paddsw        m4, OC_8      ; adjust R4 (and R3) for shift
    paddsw        m3, m3        ; r3 = D. + D.
    paddsw        m3, m4        ; r3 = R3 = E. + D.
    psraw         m4, 4         ; r4 = NR4
    psubsw        m6, m5        ; r6 = R6 = F. - B..
    psraw         m3, 4         ; r3 = NR3
    paddsw        m6, OC_8      ; adjust R6 (and R5) for shift
    paddsw        m5, m5        ; r5 = B.. + B..
    paddsw        m5, m6        ; r5 = R5 = F. + B..
    psraw         m6, 4         ; r6 = NR6
    movq        J(4), m4        ; store NR4 at J4
    psraw         m5, 4         ; r5 = NR5
    movq        I(3), m3        ; store NR3 at I3
    psubsw        m7, m0        ; r7 = R7 = G. - C.
    paddsw        m7, OC_8      ; adjust R7 (and R0) for shift
    paddsw        m0, m0        ; r0 = C. + C.
    paddsw        m0, m7        ; r0 = R0 = G. + C.
    psraw         m7, 4         ; r7 = NR7
    movq        J(6), m6        ; store NR6 at J6
    psraw         m0, 4         ; r0 = NR0
    movq        J(5), m5        ; store NR5 at J5
    movq        J(7), m7        ; store NR7 at J7
    movq        I(0), m0        ; store NR0 at I0
%endmacro

; Following macro does two 4x4 transposes in place.
;
; At entry (we assume):
;
;   r0 = a3 a2 a1 a0
;   I(1) = b3 b2 b1 b0
;   r2 = c3 c2 c1 c0
;   r3 = d3 d2 d1 d0
;
;   r4 = e3 e2 e1 e0
;   r5 = f3 f2 f1 f0
;   r6 = g3 g2 g1 g0
;   r7 = h3 h2 h1 h0
;
; At exit, we have:
;
;   I(0) = d0 c0 b0 a0
;   I(1) = d1 c1 b1 a1
;   I(2) = d2 c2 b2 a2
;   I(3) = d3 c3 b3 a3
;
;   J(4) = h0 g0 f0 e0
;   J(5) = h1 g1 f1 e1
;   J(6) = h2 g2 f2 e2
;   J(7) = h3 g3 f3 e3
;
;  I(0) I(1) I(2) I(3)  is the transpose of r0 I(1) r2 r3.
;  J(4) J(5) J(6) J(7)  is the transpose of r4 r5 r6 r7.
;
;  Since r1 is free at entry, we calculate the Js first.
%macro Transpose 0
    movq          m1, m4        ; r1 = e3 e2 e1 e0
    punpcklwd     m4, m5        ; r4 = f1 e1 f0 e0
    movq        I(0), m0        ; save a3 a2 a1 a0
    punpckhwd     m1, m5        ; r1 = f3 e3 f2 e2
    movq          m0, m6        ; r0 = g3 g2 g1 g0
    punpcklwd     m6, m7        ; r6 = h1 g1 h0 g0
    movq          m5, m4        ; r5 = f1 e1 f0 e0
    punpckldq     m4, m6        ; r4 = h0 g0 f0 e0 = R4
    punpckhdq     m5, m6        ; r5 = h1 g1 f1 e1 = R5
    movq          m6, m1        ; r6 = f3 e3 f2 e2
    movq        J(4), m4
    punpckhwd     m0, m7        ; r0 = h3 g3 h2 g2
    movq        J(5), m5
    punpckhdq     m6, m0        ; r6 = h3 g3 f3 e3 = R7
    movq          m4, I(0)      ; r4 = a3 a2 a1 a0
    punpckldq     m1, m0        ; r1 = h2 g2 f2 e2 = R6
    movq          m5, I(1)      ; r5 = b3 b2 b1 b0
    movq          m0, m4        ; r0 = a3 a2 a1 a0
    movq        J(7), m6
    punpcklwd     m0, m5        ; r0 = b1 a1 b0 a0
    movq        J(6), m1
    punpckhwd     m4, m5        ; r4 = b3 a3 b2 a2
    movq          m5, m2        ; r5 = c3 c2 c1 c0
    punpcklwd     m2, m3        ; r2 = d1 c1 d0 c0
    movq          m1, m0        ; r1 = b1 a1 b0 a0
    punpckldq     m0, m2        ; r0 = d0 c0 b0 a0 = R0
    punpckhdq     m1, m2        ; r1 = d1 c1 b1 a1 = R1
    movq          m2, m4        ; r2 = b3 a3 b2 a2
    movq        I(0), m0
    punpckhwd     m5, m3        ; r5 = d3 c3 d2 c2
    movq        I(1), m1
    punpckhdq     m4, m5        ; r4 = d3 c3 b3 a3 = R3
    punpckldq     m2, m5        ; r2 = d2 c2 b2 a2 = R2
    movq        I(3), m4
    movq        I(2), m2
%endmacro

%macro VP3_IDCT_mmx 1
    ; eax = quantized input
    ; ebx = dequantizer matrix
    ; ecx = IDCT constants
    ;  M(I) = ecx + MaskOffset(0) + I * 8
    ;  C(I) = ecx + CosineOffset(32) + (I-1) * 8
    ; edx = output
    ; r0..r7 = mm0..mm7
%define OC_8 [pw_8]
%define C(x) [vp3_idct_data+16*(x-1)]

    ; at this point, function has completed dequantization + dezigzag +
    ; partial transposition; now do the idct itself
%define I(x) [%1+16* x     ]
%define J(x) [%1+16*(x-4)+8]
    RowIDCT
    Transpose

%define I(x) [%1+16* x   +64]
%define J(x) [%1+16*(x-4)+72]
    RowIDCT
    Transpose

%define I(x) [%1+16*x]
%define J(x) [%1+16*x]
    ColumnIDCT

%define I(x) [%1+16*x+8]
%define J(x) [%1+16*x+8]
    ColumnIDCT
%endmacro

%macro VP3_1D_IDCT_SSE2 0
    movdqa        m2, I(3)      ; xmm2 = i3
    movdqa        m6, C(3)      ; xmm6 = c3
    movdqa        m4, m2        ; xmm4 = i3
    movdqa        m7, I(5)      ; xmm7 = i5
    pmulhw        m4, m6        ; xmm4 = c3 * i3 - i3
    movdqa        m1, C(5)      ; xmm1 = c5
    pmulhw        m6, m7        ; xmm6 = c3 * i5 - i5
    movdqa        m5, m1        ; xmm5 = c5
    pmulhw        m1, m2        ; xmm1 = c5 * i3 - i3
    movdqa        m3, I(1)      ; xmm3 = i1
    pmulhw        m5, m7        ; xmm5 = c5 * i5 - i5
    movdqa        m0, C(1)      ; xmm0 = c1
    paddw         m4, m2        ; xmm4 = c3 * i3
    paddw         m6, m7        ; xmm6 = c3 * i5
    paddw         m2, m1        ; xmm2 = c5 * i3
    movdqa        m1, I(7)      ; xmm1 = i7
    paddw         m7, m5        ; xmm7 = c5 * i5
    movdqa        m5, m0        ; xmm5 = c1
    pmulhw        m0, m3        ; xmm0 = c1 * i1 - i1
    paddsw        m4, m7        ; xmm4 = c3 * i3 + c5 * i5 = C
    pmulhw        m5, m1        ; xmm5 = c1 * i7 - i7
    movdqa        m7, C(7)      ; xmm7 = c7
    psubsw        m6, m2        ; xmm6 = c3 * i5 - c5 * i3 = D
    paddw         m0, m3        ; xmm0 = c1 * i1
    pmulhw        m3, m7        ; xmm3 = c7 * i1
    movdqa        m2, I(2)      ; xmm2 = i2
    pmulhw        m7, m1        ; xmm7 = c7 * i7
    paddw         m5, m1        ; xmm5 = c1 * i7
    movdqa        m1, m2        ; xmm1 = i2
    pmulhw        m2, C(2)      ; xmm2 = i2 * c2 -i2
    psubsw        m3, m5        ; xmm3 = c7 * i1 - c1 * i7 = B
    movdqa        m5, I(6)      ; xmm5 = i6
    paddsw        m0, m7        ; xmm0 = c1 * i1 + c7 * i7 = A
    movdqa        m7, m5        ; xmm7 = i6
    psubsw        m0, m4        ; xmm0 = A - C
    pmulhw        m5, C(2)      ; xmm5 = c2 * i6 - i6
    paddw         m2, m1        ; xmm2 = i2 * c2
    pmulhw        m1, C(6)      ; xmm1 = c6 * i2
    paddsw        m4, m4        ; xmm4 = C + C
    paddsw        m4, m0        ; xmm4 = A + C = C.
    psubsw        m3, m6        ; xmm3 = B - D
    paddw         m5, m7        ; xmm5 = c2 * i6
    paddsw        m6, m6        ; xmm6 = D + D
    pmulhw        m7, C(6)      ; xmm7 = c6 * i6
    paddsw        m6, m3        ; xmm6 = B + D = D.
    movdqa      I(1), m4        ; Save C. at I(1)
    psubsw        m1, m5        ; xmm1 = c6 * i2 - c2 * i6 = H
    movdqa        m4, C(4)      ; xmm4 = C4
    movdqa        m5, m3        ; xmm5 = B - D
    pmulhw        m3, m4        ; xmm3 = ( c4 -1 ) * ( B - D )
    paddsw        m7, m2        ; xmm7 = c2 * i2 + c6 * i6 = G
    movdqa      I(2), m6        ; save D. at I(2)
    movdqa        m2, m0        ; xmm2 = A - C
    movdqa        m6, I(0)      ; xmm6 = i0
    pmulhw        m0, m4        ; xmm0 = ( c4 - 1 ) * ( A - C ) = A.
    paddw         m5, m3        ; xmm5 = c4 * ( B - D ) = B.
    movdqa        m3, I(4)      ; xmm3 = i4
    psubsw        m5, m1        ; xmm5 = B. - H = B..
    paddw         m2, m0        ; xmm2 = c4 * ( A - C) = A.
    psubsw        m6, m3        ; xmm6 = i0 - i4
    movdqa        m0, m6        ; xmm0 = i0 - i4
    pmulhw        m6, m4        ; xmm6 = (c4 - 1) * (i0 - i4) = F
    paddsw        m3, m3        ; xmm3 = i4 + i4
    paddsw        m1, m1        ; xmm1 = H + H
    paddsw        m3, m0        ; xmm3 = i0 + i4
    paddsw        m1, m5        ; xmm1 = B. + H = H.
    pmulhw        m4, m3        ; xmm4 = ( c4 - 1 ) * ( i0 + i4 )
    paddw         m6, m0        ; xmm6 = c4 * ( i0 - i4 )
    psubsw        m6, m2        ; xmm6 = F - A. = F.
    paddsw        m2, m2        ; xmm2 = A. + A.
    movdqa        m0, I(1)      ; Load        C. from I(1)
    paddsw        m2, m6        ; xmm2 = F + A. = A..
    paddw         m4, m3        ; xmm4 = c4 * ( i0 + i4 ) = 3
    psubsw        m2, m1        ; xmm2 = A.. - H. = R2
    ADD(m2)                     ; Adjust R2 and R1 before shifting
    paddsw        m1, m1        ; xmm1 = H. + H.
    paddsw        m1, m2        ; xmm1 = A.. + H. = R1
    SHIFT(m2)                   ; xmm2 = op2
    psubsw        m4, m7        ; xmm4 = E - G = E.
    SHIFT(m1)                   ; xmm1 = op1
    movdqa        m3, I(2)      ; Load D. from I(2)
    paddsw        m7, m7        ; xmm7 = G + G
    paddsw        m7, m4        ; xmm7 = E + G = G.
    psubsw        m4, m3        ; xmm4 = E. - D. = R4
    ADD(m4)                     ; Adjust R4 and R3 before shifting
    paddsw        m3, m3        ; xmm3 = D. + D.
    paddsw        m3, m4        ; xmm3 = E. + D. = R3
    SHIFT(m4)                   ; xmm4 = op4
    psubsw        m6, m5        ; xmm6 = F. - B..= R6
    SHIFT(m3)                   ; xmm3 = op3
    ADD(m6)                     ; Adjust R6 and R5 before shifting
    paddsw        m5, m5        ; xmm5 = B.. + B..
    paddsw        m5, m6        ; xmm5 = F. + B.. = R5
    SHIFT(m6)                   ; xmm6 = op6
    SHIFT(m5)                   ; xmm5 = op5
    psubsw        m7, m0        ; xmm7 = G. - C. = R7
    ADD(m7)                     ; Adjust R7 and R0 before shifting
    paddsw        m0, m0        ; xmm0 = C. + C.
    paddsw        m0, m7        ; xmm0 = G. + C.
    SHIFT(m7)                   ; xmm7 = op7
    SHIFT(m0)                   ; xmm0 = op0
%endmacro

%macro PUT_BLOCK 8
    movdqa      O(0), m%1
    movdqa      O(1), m%2
    movdqa      O(2), m%3
    movdqa      O(3), m%4
    movdqa      O(4), m%5
    movdqa      O(5), m%6
    movdqa      O(6), m%7
    movdqa      O(7), m%8
%endmacro

%macro VP3_IDCT_sse2 1
%define I(x) [%1+16*x]
%define O(x) [%1+16*x]
%define C(x) [vp3_idct_data+16*(x-1)]
%define SHIFT(x)
%define ADD(x)
        VP3_1D_IDCT_SSE2
%if ARCH_X86_64
        TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, 8
%else
        TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, [%1], [%1+16]
%endif
        PUT_BLOCK 0, 1, 2, 3, 4, 5, 6, 7

%define SHIFT(x) psraw  x, 4
%define ADD(x)   paddsw x, [pw_8]
        VP3_1D_IDCT_SSE2
        PUT_BLOCK 0, 1, 2, 3, 4, 5, 6, 7
%endmacro

%macro vp3_idct_funcs 3
cglobal vp3_idct_%1, 1, 1, %2
    VP3_IDCT_%1   r0
    RET

cglobal vp3_idct_put_%1, 3, %3, %2
    VP3_IDCT_%1   r2
%if ARCH_X86_64
    mov           r3, r2
    mov           r2, r1
    mov           r1, r0
    mov           r0, r3
%else
    mov          r0m, r2
    mov          r1m, r0
    mov          r2m, r1
%endif
%if WIN64
    call put_signed_pixels_clamped_mmx
    RET
%else
    jmp put_signed_pixels_clamped_mmx
%endif

cglobal vp3_idct_add_%1, 3, %3, %2
    VP3_IDCT_%1   r2
%if ARCH_X86_64
    mov           r3, r2
    mov           r2, r1
    mov           r1, r0
    mov           r0, r3
%else
    mov          r0m, r2
    mov          r1m, r0
    mov          r2m, r1
%endif
%if WIN64
    call add_pixels_clamped_mmx
    RET
%else
    jmp add_pixels_clamped_mmx
%endif
%endmacro

%if ARCH_X86_64
%define REGS 4
%else
%define REGS 3
%endif
INIT_MMX
vp3_idct_funcs mmx,  0, REGS
INIT_XMM
vp3_idct_funcs sse2, 9, REGS
%undef REGS

%macro DC_ADD 0
    movq          m2, [r0     ]
    movq          m3, [r0+r1  ]
    paddusb       m2, m0
    movq          m4, [r0+r1*2]
    paddusb       m3, m0
    movq          m5, [r0+r3  ]
    paddusb       m4, m0
    paddusb       m5, m0
    psubusb       m2, m1
    psubusb       m3, m1
    movq   [r0     ], m2
    psubusb       m4, m1
    movq   [r0+r1  ], m3
    psubusb       m5, m1
    movq   [r0+r1*2], m4
    movq   [r0+r3  ], m5
%endmacro

INIT_MMX
cglobal vp3_idct_dc_add_mmx2, 3, 4
%if ARCH_X86_64
    movsxd        r1, r1d
%endif
    lea           r3, [r1*3]
    movsx         r2, word [r2]
    add           r2, 15
    sar           r2, 5
    movd          m0, r2d
    pshufw        m0, m0, 0x0
    pxor          m1, m1
    psubw         m1, m0
    packuswb      m0, m0
    packuswb      m1, m1
    DC_ADD
    lea           r0, [r0+r1*4]
    DC_ADD
    RET
