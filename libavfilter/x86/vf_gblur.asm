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

SECTION .data

gblur_transpose_16x16_indices1: dq 2, 3, 0, 1, 6, 7, 4, 5
gblur_transpose_16x16_indices2: dq 1, 0, 3, 2, 5, 4, 7, 6
gblur_transpose_16x16_indices3: dd 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14
gblur_transpose_16x16_mask: dw 0xcc, 0x33, 0xaa, 0x55, 0xaaaa, 0x5555
gblur_vindex_width: dd 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

SECTION .text

%xdefine AVX2_MMSIZE   32
%xdefine AVX512_MMSIZE 64

%macro MOVSXDIFNIDN 1-*
    %rep %0
        movsxdifnidn %1q, %1d
        %rotate 1
    %endrep
%endmacro

%macro KXNOR 2-*
%if mmsize == AVX512_MMSIZE
    kxnorw %2, %2, %2
%else
    %if %0 == 3
        mov %3, -1
    %else
        vpcmpeqd %1, %1, %1
    %endif
%endif
%endmacro

%macro KMOVW 2-4
%if mmsize == AVX2_MMSIZE && %0 == 4
    mova %1, %2
%elif mmsize == AVX512_MMSIZE
    %if %0 == 4
        %rotate 2
    %endif
    kmovw %1, %2
%endif
%endmacro

%macro PUSH_MASK 5
%if mmsize == AVX2_MMSIZE
    %assign %%n mmsize/4
    %assign %%i 0
    %rep %%n
        mov %4, %3
        and %4, 1
        neg %4
        mov dword [%5 + %%i*4], %4
        sar %3, 1
        %assign %%i %%i+1
    %endrep
    movu %1, [%5]
%else
    kmovd %2, %3
%endif
%endmacro

%macro VMASKMOVPS 4
%if mmsize == AVX2_MMSIZE
    vpmaskmovd %1, %3, %2
%else
    kmovw k7, %4
    vmovups %1{k7}, %2
%endif
%endmacro

%macro VGATHERDPS 4
%if mmsize == AVX2_MMSIZE
    vgatherdps %1, %2, %3
%else
    vgatherdps %1{%4}, %2
%endif
%endmacro

%macro VSCATTERDPS128 7
    %rep 4
        mov %7, %6
        and %7, 1
        cmp %7, 0
        je %%end_scatter
        movss [%2 + %3*%4], xm%1
        vpshufd m%1, m%1, 0x39
        add %3, %5
        sar %6, 1
    %endrep
    %%end_scatter:
%endmacro

; %1=register index
; %2=base address   %3=vindex
; %4=scale          %5=width
; %6=mask           %7=tmp
; m15=reserved
%macro VSCATTERDPS256 7
    mova m15, m%1
    xor %3, %3
    VSCATTERDPS128 15, %2, %3, %4, %5, %6, %7
    vextractf128 xm15, m%1, 1
    VSCATTERDPS128 15, %2, %3, %4, %5, %6, %7
%endmacro

; %1=base address  %2=avx2 vindex
; %3=avx512 vindex %4=avx2 mask
; %5=avx512 mask   %6=register index
; %7=width         %8-*=tmp
%macro VSCATTERDPS 8-*
%if mmsize == AVX2_MMSIZE
    %if %0 == 9
        mov  %9, %4
        VSCATTERDPS256 %6, %1, %2, 4, %7, %9, %8
    %else
        VSCATTERDPS256 %6, %1, %2, 4, %7, %4, %8
    %endif
%else
    vscatterdps [%1 + %3*4]{%5}, m%6
%endif
%endmacro

%macro INIT_WORD_MASK 1-*
    %assign %%i 0
    %rep %0
        kmovw %1, [gblur_transpose_16x16_mask + %%i * 2]
        %assign %%i %%i+1
        %rotate 1
    %endrep
%endmacro

%macro INIT_INDICES 1-*
    %assign %%i 1
    %rep %0
        movu %1, [gblur_transpose_16x16_indices %+ %%i]
        %assign %%i %%i+1
        %rotate 1
    %endrep
%endmacro

%assign stack_offset 0
%macro PUSH_MM 1
%if mmsize == AVX2_MMSIZE
    movu [rsp + stack_offset], %1
    %assign stack_offset stack_offset+mmsize
%endif
%endmacro

%macro POP_MM 1
%if mmsize == AVX2_MMSIZE
    %assign stack_offset stack_offset-mmsize
    movu %1, [rsp + stack_offset]
%endif
%endmacro

%macro READ_LOCAL_BUFFER 1
    %if mmsize == AVX512_MMSIZE
        %assign %%i 19
    %else
        %assign %%i 9
    %endif
    %assign  %%j %%i-1
    %assign  %%k %1-1
    %xdefine %%m m %+ %%i
    mova %%m, m3
    FMULADD_PS %%m, %%m, m0, [localbufq + %%k * mmsize], %%m
    %assign %%k %%k-1
    %rep %1-1
        %xdefine %%m m %+ %%j
        mova %%m, m %+ %%i
        FMULADD_PS %%m, %%m, m0, [localbufq + %%k * mmsize], %%m
        %assign %%i %%i-1
        %assign %%j %%j-1
        %assign %%k %%k-1
    %endrep
    %if mmsize == AVX512_MMSIZE
        mova m3, m %+ %%i
    %endif
%endmacro

%macro FMADD_WRITE 4
    FMULADD_PS %1, %1, %2, %3, %1
    mova %4, %1
%endmacro

%macro WRITE_LOCAL_BUFFER_INTERNAL 8-16
    %assign %%i 0
    %rep %0
        FMADD_WRITE m3, m0, m %+ %1,  [localbufq + %%i * mmsize]
        %assign %%i %%i+1
        %rotate 1
    %endrep
%endmacro

%macro GATHERPS 1
    %if mmsize == AVX512_MMSIZE
        %assign %%i 4
    %else
        %assign %%i 2
    %endif
    movu m %+ %%i, [ptrq]
    mov strideq, widthq
    %assign %%i %%i+1
    %rep %1-2
        movu m %+ %%i, [ptrq + strideq*4]
        add strideq, widthq
        %assign %%i %%i+1
    %endrep
    movu m %+ %%i, [ptrq + strideq*4]
%endmacro

%macro SCATTERPS_INTERNAL 8-16
    movu [ptrq + strideq*0], m %+ %1
    mov strideq, widthq
    %rotate 1
    %rep %0-2
        movu [ptrq + strideq*4], m %+ %1
        add strideq, widthq
        %rotate 1
    %endrep
    movu [ptrq + strideq*4], m %+ %1
%endmacro

%macro BATCH_INSERT64X4 4-*
    %assign %%imm8 %1
    %rotate 1
    %rep (%0-1)/3
        vinserti64x4 m%1, m%2, ym%3, %%imm8
        %rotate 3
    %endrep
%endmacro

%macro BATCH_EXTRACT_INSERT 2-*
    %assign %%imm8 %1
    %rotate 1
    %rep (%0-1)/2
        vextractf64x4 ym%1, m%1,       %%imm8
        vextractf64x4 ym%2, m%2,       %%imm8
        vinserti64x4   m%1, m%1, ym%2, %%imm8
        %rotate 2
    %endrep
%endmacro

%macro BATCH_MOVE 2-*
    %rep %0/2
        mova m%1, m%2
        %rotate 2
    %endrep
%endmacro

%macro BATCH_PERMUTE 3-*
    %xdefine %%decorator %1
    %xdefine %%mask      %2
    %assign  %%index     %3
    %rotate 3
    %rep (%0-3)/2
        vperm %+ %%decorator m%1{%%mask}, m %+ %%index, m%2
        %rotate 2
    %endrep
%endmacro

; input : m3-m19
; output: m8 m5 m9 m15 m16 m7 m17 m27 m24 m21 m25 m19 m12 m23 m13 m11
%macro TRANSPOSE_16X16_AVX512 0
    BATCH_INSERT64X4 0x1, 20,4,12, 21,5,13,  22,6,14,  23,7,15
    BATCH_INSERT64X4 0x1, 24,8,16, 25,9,17, 26,10,18, 27,11,19

    BATCH_EXTRACT_INSERT 0x1, 4,12, 5,13,  6,14,  7,15
    BATCH_EXTRACT_INSERT 0x1, 8,16, 9,17, 10,18, 11,19

    BATCH_MOVE 12,20, 13,21, 14,22, 15,23
    BATCH_PERMUTE q, k6, 28, 12,24, 13,25, 14,26, 15,27
    BATCH_PERMUTE q, k5, 28, 24,20, 25,21, 26,22, 27,23

    BATCH_MOVE 16,4, 17,5, 18,6, 19,7
    BATCH_PERMUTE q, k6, 28, 16,8, 17,9, 18,10, 19,11
    BATCH_PERMUTE q, k5, 28,  8,4,  9,5,  10,6,  11,7

    BATCH_MOVE  4,12,  5,13, 6,24, 7,25
    BATCH_MOVE 20,16, 21,17, 22,8, 23,9

    BATCH_PERMUTE q, k4, 29,  4,14,  5,15,  6,26,  7,27
    BATCH_PERMUTE q, k3, 29, 14,12, 15,13, 26,24, 27,25
    BATCH_PERMUTE q, k4, 29, 20,18, 21,19, 22,10, 23,11
    BATCH_PERMUTE q, k3, 29, 18,16, 19,17,  10,8,  11,9

    BATCH_MOVE   8,4,  9,14,  16,6, 17,26
    BATCH_MOVE 24,20, 25,18, 12,22, 13,10

    BATCH_PERMUTE d, k2, 30,   8,5,  9,15,  16,7, 17,27
    BATCH_PERMUTE d, k1, 30,   5,4, 15,14,   7,6, 27,26
    BATCH_PERMUTE d, k2, 30, 24,21, 25,19, 12,23, 13,11
    BATCH_PERMUTE d, k1, 30, 21,20, 19,18, 23,22, 11,10
%endmacro

%macro INSERT_UNPACK 8
    vinsertf128 m%5, m%1, xm%3, 0x1
    vinsertf128 m%6, m%2, xm%4, 0x1
    vunpcklpd   m%7, m%5,  m%6
    vunpckhpd   m%8, m%5,  m%6
%endmacro

%macro SHUFFLE 4
    vshufps m%3, m%1, m%2, 0x88
    vshufps m%4, m%1, m%2, 0xDD
    mova    m%1, m%3
    mova    m%2, m%4
%endmacro

%macro EXTRACT_INSERT_UNPACK 6
    vextractf128 xm%1, m%1,       0x1
    vextractf128 xm%2, m%2,       0x1
    vinsertf128   m%3, m%3, xm%1, 0x0
    vinsertf128   m%4, m%4, xm%2, 0x0
    vunpcklpd     m%5, m%3, m%4
    vunpckhpd     m%6, m%3, m%4
%endmacro

; Transpose 8x8 AVX2
; Limit the number ym# register to 16 for compatibility
; Used up registers instead of using stack memory
; Input:  m2-m9
; Output: m12, m14, m13, m15, m8, m10, m9, m11
%macro TRANSPOSE_8X8_AVX2 0
    INSERT_UNPACK 2, 3, 6, 7, 10, 11, 12, 13
    INSERT_UNPACK 4, 5, 8, 9, 10, 11, 14, 15

    SHUFFLE 12, 14, 10, 11
    SHUFFLE 13, 15, 10, 11

    EXTRACT_INSERT_UNPACK 4, 5, 8, 9, 10, 11
    EXTRACT_INSERT_UNPACK 2, 3, 6, 7,  8, 9

    SHUFFLE 8, 10, 6, 7
    SHUFFLE 9, 11, 6, 7
%endmacro

%macro TRANSPOSE 0
    %if cpuflag(avx512)
        TRANSPOSE_16X16_AVX512
    %elif cpuflag(avx2)
        TRANSPOSE_8X8_AVX2
    %endif
%endmacro

%macro WRITE_LOCAL_BUFFER 0
    %if cpuflag(avx512)
        WRITE_LOCAL_BUFFER_INTERNAL 8, 5, 9, 15, 16, 7, 17, 27, \
                                    24, 21, 25, 19, 12, 23, 13, 11
    %elif cpuflag(avx2)
        WRITE_LOCAL_BUFFER_INTERNAL 12, 14, 13, 15, 8, 10, 9, 11
    %endif
%endmacro

%macro SCATTERPS 0
    %if cpuflag(avx512)
        SCATTERPS_INTERNAL 8, 5, 9, 15, 16, 7, 17, 27, \
                           24, 21, 25, 19, 12, 23, 13, 11
    %elif cpuflag(avx2)
        SCATTERPS_INTERNAL 12, 14, 13, 15, 8, 10, 9, 11
    %endif
%endmacro

%macro OPTIMIZED_LOOP_STEP 0
    lea stepd, [stepsd - 1]
    cmp stepd, 0
    jle %%bscale_scalar
%%loop_step:
    sub localbufq, mmsize
    mulps m3, m1
    movu [localbufq], m3

    ; Filter leftwards
    lea xq, [widthq - 1]
    %%loop_step_x_back:
        sub localbufq, mmsize
        FMULADD_PS m3, m3, m0, [localbufq], m3
        movu [localbufq], m3

        dec xq
        cmp xq, 0
        jg %%loop_step_x_back

    ; Filter rightwards
    mulps m3, m1
    movu [localbufq], m3
    add localbufq, mmsize

    lea xq, [widthq - 1]
    %%loop_step_x:
        FMULADD_PS m3, m3, m0, [localbufq], m3
        movu [localbufq], m3
        add localbufq, mmsize

        dec xq
        cmp xq, 0
        jg %%loop_step_x

    dec stepd
    cmp stepd, 0
    jg %%loop_step

%%bscale_scalar:
%endmacro

;***************************************************************************
; void ff_horiz_slice(float *ptr, int width, int height, int steps,
;                          float nu, float bscale)
;***************************************************************************
%macro HORIZ_SLICE 0
%if UNIX64
%if cpuflag(avx512) || cpuflag(avx2)
cglobal horiz_slice, 5, 12, mmnum, 0-mmsize*4, buffer, width, height, steps, \
                                          localbuf, x, y, step, stride, remain, ptr, mask
%else
cglobal horiz_slice, 4, 9, 9, ptr, width, height, steps, x, y, step, stride, remain
%endif
%else
%if cpuflag(avx512) || cpuflag(avx2)
cglobal horiz_slice, 5, 12, mmnum, 0-mmsize*4, buffer, width, height, steps, nu, bscale, \
                                          localbuf, x, y, step, stride, remain, ptr, mask
%else
cglobal horiz_slice, 4, 9, 9, ptr, width, height, steps, nu, bscale, x, y, step, stride, remain
%endif
%endif
%if cpuflag(avx512) || cpuflag(avx2)
%assign rows mmsize/4
%assign cols mmsize/4
%if WIN64
    VBROADCASTSS    m0, num ; nu
    VBROADCASTSS    m1, bscalem ; bscale

    mov nuq, localbufm
    DEFINE_ARGS buffer, width, height, steps, \
                localbuf, x, y, step, stride, remain, ptr, mask
%else
    VBROADCASTSS    m0, xmm0 ; nu
    VBROADCASTSS    m1, xmm1 ; bscale
%endif

    MOVSXDIFNIDN width, height, steps

%if cpuflag(avx512)
    vpbroadcastd    m2, widthd
    INIT_WORD_MASK  k6, k5, k4, k3, k2, k1
    INIT_INDICES   m28, m29, m30
%else
    movd         xm2, widthd
    VBROADCASTSS  m2, xm2
%endif

    vpmulld m2, m2, [gblur_vindex_width] ; vindex width

    xor yq, yq ; y = 0
    xor xq, xq ; x = 0

    cmp heightq, rows
    jl .y_scalar
    sub heightq, rows

.loop_y:
    ; ptr = buffer + y * width;
    mov  ptrq, yq
    imul ptrq, widthq
    lea  ptrq, [bufferq + ptrq*4]

    KXNOR m5, k7
    VGATHERDPS m3, [ptrq + m2*4], m5, k7
    mulps m3, m1
    movu [localbufq], m3
    add ptrq, 4
    add localbufq, mmsize

    ; Filter rightwards
    PUSH_MM m2
    lea xq, [widthq - 1]
    .loop_x:
        PUSH_MM m3
        GATHERPS cols
        TRANSPOSE
        POP_MM m3
        WRITE_LOCAL_BUFFER

        add ptrq,      mmsize
        add localbufq, rows * mmsize
        sub xq,        cols
        cmp xq,        cols
        jge .loop_x
        POP_MM m2

    cmp xq, 0
    jle .bscale_scalar
    .loop_x_scalar:
        KXNOR m5, k7
        VGATHERDPS m4, [ptrq + m2*4], m5, k7
        FMULADD_PS m3, m3, m0, m4, m3
        movu [localbufq], m3

        add ptrq,      0x4
        add localbufq, mmsize
        dec xq
        cmp xq,        0
        jg .loop_x_scalar

    .bscale_scalar:
        OPTIMIZED_LOOP_STEP
        sub ptrq, 4
        sub localbufq, mmsize
        mulps m3, m1
        KXNOR m5, k7, maskq
        VSCATTERDPS ptrq, strideq, m2, maskq, k7, 3, widthq, remainq

    ; Filter leftwards
    PUSH_MM m2
    lea xq, [widthq - 1]
    .loop_x_back:
        sub localbufq, rows * mmsize
        READ_LOCAL_BUFFER cols
        PUSH_MM m2
        TRANSPOSE
        POP_MM m3
        sub ptrq, mmsize
        SCATTERPS

        sub xq, cols
        cmp xq, cols
        jge .loop_x_back
        POP_MM m2

    cmp xq, 0
    jle .end_loop_x
    .loop_x_back_scalar:
        sub ptrq, 0x4
        sub localbufq, mmsize
        FMULADD_PS m3, m3, m0, [localbufq], m3
        KXNOR m5, k7, maskq
        VSCATTERDPS ptrq, strideq, m2, maskq, k7, 3, widthq, remainq

        dec xq
        cmp xq, 0
        jg .loop_x_back_scalar

    .end_loop_x:

    add yq, rows
    cmp yq, heightq
    jle .loop_y

    add heightq, rows
    cmp yq, heightq
    jge .end_scalar

    mov remainq, widthq
    imul remainq, mmsize
    add ptrq, remainq

.y_scalar:
    mov remainq, heightq
    sub remainq, yq
    mov maskq, 1
    shlx maskq, maskq, remainq
    sub maskq, 1
    mov remainq, maskq
    PUSH_MASK m5, k1, remaind, xd, rsp + 0x20

    mov ptrq, yq
    imul ptrq, widthq
    lea ptrq, [bufferq + ptrq * 4] ; ptrq = buffer + y * width
    KMOVW m6, m5, k7, k1
    VGATHERDPS m3, [ptrq + m2 * 4], m6, k7
    mulps m3, m1 ; p0 *= bscale
    movu [localbufq], m3
    add localbufq, mmsize

    ; Filter rightwards
    lea xq, [widthq - 1]
    .y_scalar_loop_x:
        add ptrq, 4
        KMOVW m6, m5, k7, k1
        VGATHERDPS m4, [ptrq + m2 * 4], m6, k7
        FMULADD_PS m3, m3, m0, m4, m3
        movu [localbufq], m3
        add localbufq, mmsize

        dec xq
        cmp xq, 0
        jg .y_scalar_loop_x

    OPTIMIZED_LOOP_STEP

    sub localbufq, mmsize
    mulps m3, m1 ; p0 *= bscale
    KMOVW k7, k1
    VSCATTERDPS ptrq, strideq, m2, maskq, k7, 3, widthq, remainq, heightq

    ; Filter leftwards
    lea xq, [widthq - 1]
    .y_scalar_loop_x_back:
        sub ptrq, 4
        sub localbufq, mmsize
        FMULADD_PS m3, m3, m0, [localbufq], m3
        KMOVW k7, k1
        VSCATTERDPS ptrq, strideq, m2, maskq, k7, 3, widthq, remainq, heightq
        dec xq
        cmp xq, 0
        jg .y_scalar_loop_x_back

.end_scalar:
    RET
%else
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
        jge .end_scalar

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
        jle .end_scalar_back

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
%endif
%endmacro

%if ARCH_X86_64
INIT_XMM sse4
HORIZ_SLICE

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
%xdefine mmnum 16
HORIZ_SLICE
%endif

%if HAVE_AVX512_EXTERNAL
INIT_ZMM avx512
%xdefine mmnum 32
HORIZ_SLICE
%endif
%endif

%macro POSTSCALE_SLICE 0
cglobal postscale_slice, 2, 2, 4, ptr, length, postscale, min, max
    shl lengthd, 2
    add ptrq, lengthq
    neg lengthq
%if ARCH_X86_32
    VBROADCASTSS m0, postscalem
    VBROADCASTSS m1, minm
    VBROADCASTSS m2, maxm
%elif WIN64
    VBROADCASTSS m0, xmm2
    VBROADCASTSS m1, xmm3
    VBROADCASTSS m2, maxm
%else ; UNIX
    VBROADCASTSS m0, xmm0
    VBROADCASTSS m1, xmm1
    VBROADCASTSS m2, xmm2
%endif

    .loop:
%if cpuflag(avx2) || cpuflag(avx512)
    mulps         m3, m0, [ptrq + lengthq]
%else
    movu          m3, [ptrq + lengthq]
    mulps         m3, m0
%endif
    maxps         m3, m1
    minps         m3, m2
    movu   [ptrq+lengthq], m3

    add lengthq, mmsize
    jl .loop

    RET
%endmacro

INIT_XMM sse
POSTSCALE_SLICE

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
POSTSCALE_SLICE
%endif

%if HAVE_AVX512_EXTERNAL
INIT_ZMM avx512
POSTSCALE_SLICE
%endif

;*******************************************************************************
; void ff_verti_slice(float *buffer, int width, int height, int column_begin,
;                     int column_end, int steps, float nu, float bscale);
;*******************************************************************************
%macro VERTI_SLICE 0
%if UNIX64
cglobal verti_slice, 6, 12, 9, 0-mmsize*2, buffer, width, height, cbegin, cend, \
                                         steps, x, y, cwidth, step, ptr, stride
%else
cglobal verti_slice, 6, 12, 9, 0-mmsize*2, buffer, width, height, cbegin, cend, \
                                         steps, nu, bscale, x, y, cwidth, step, \
                                         ptr, stride
%endif
%assign cols mmsize/4
%if WIN64
    VBROADCASTSS m0, num
    VBROADCASTSS m1, bscalem
    DEFINE_ARGS buffer, width, height, cbegin, cend, \
                steps, x, y, cwidth, step, ptr, stride
%else
    VBROADCASTSS m0, xmm0 ; nu
    VBROADCASTSS m1, xmm1 ; bscale
%endif
    MOVSXDIFNIDN width, height, cbegin, cend, steps

    mov cwidthq, cendq
    sub cwidthq, cbeginq
    lea strideq, [widthq * 4]

    xor xq, xq ; x = 0
    cmp cwidthq, cols
    jl .x_scalar
    cmp cwidthq, 0x0
    je .end_scalar

    sub cwidthq, cols
.loop_x:
    xor stepq, stepq
    .loop_step:
        ; ptr = buffer + x + column_begin;
        lea ptrq, [xq + cbeginq]
        lea ptrq, [bufferq + ptrq*4]

        ;  ptr[15:0] *= bcale;
        movu m2, [ptrq]
        mulps m2, m1
        movu [ptrq], m2

        ; Filter downwards
        mov yq, 1
        .loop_y_down:
            add ptrq, strideq ; ptrq += width
            movu m3, [ptrq]
            FMULADD_PS m2, m2, m0, m3, m2
            movu [ptrq], m2

            inc yq
            cmp yq, heightq
            jl .loop_y_down

        mulps m2, m1
        movu [ptrq], m2

        ; Filter upwards
        dec yq
        .loop_y_up:
            sub ptrq, strideq
            movu m3, [ptrq]
            FMULADD_PS m2, m2, m0, m3, m2
            movu [ptrq], m2

            dec yq
            cmp yq, 0
            jg .loop_y_up

        inc stepq
        cmp stepq, stepsq
        jl .loop_step

    add xq, cols
    cmp xq, cwidthq
    jle .loop_x

    add cwidthq, cols
    cmp xq, cwidthq
    jge .end_scalar

.x_scalar:
    xor stepq, stepq
    mov qword [rsp + 0x10], xq
    sub cwidthq, xq
    mov xq, 1
    shlx cwidthq, xq, cwidthq
    sub cwidthq, 1
    PUSH_MASK m4, k1, cwidthd, xd, rsp + 0x20
    mov xq, qword [rsp + 0x10]

    .loop_step_scalar:
        lea ptrq, [xq + cbeginq]
        lea ptrq, [bufferq + ptrq*4]

        VMASKMOVPS m2, [ptrq], m4, k1
        mulps m2, m1
        VMASKMOVPS [ptrq], m2, m4, k1

        ; Filter downwards
        mov yq, 1
        .x_scalar_loop_y_down:
            add ptrq, strideq
            VMASKMOVPS m3, [ptrq], m4, k1
            FMULADD_PS m2, m2, m0, m3, m2
            VMASKMOVPS [ptrq], m2, m4, k1

            inc yq
            cmp yq, heightq
            jl .x_scalar_loop_y_down

        mulps m2, m1
        VMASKMOVPS [ptrq], m2, m4, k1

        ; Filter upwards
        dec yq
        .x_scalar_loop_y_up:
            sub ptrq, strideq
            VMASKMOVPS m3, [ptrq], m4, k1
            FMULADD_PS m2, m2, m0, m3, m2
            VMASKMOVPS [ptrq], m2, m4, k1

            dec yq
            cmp yq, 0
            jg .x_scalar_loop_y_up

        inc stepq
        cmp stepq, stepsq
        jl .loop_step_scalar

.end_scalar:
    RET
%endmacro

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
VERTI_SLICE
%endif

%if HAVE_AVX512_EXTERNAL
INIT_ZMM avx512
VERTI_SLICE
%endif
%endif
