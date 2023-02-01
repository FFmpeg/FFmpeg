;******************************************************************************
;* Core video DSP functions
;* Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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

; slow vertical extension loop function. Works with variable-width, and
; does per-line reading/writing of source data

%macro V_COPY_ROW 2 ; type (top/body/bottom), h
.%1_y_loop:                                     ; do {
    mov              wq, r7mp                   ;   initialize w (r7mp = wmp)
.%1_x_loop:                                     ;   do {
    movu             m0, [srcq+wq]              ;     m0 = read($mmsize)
    movu      [dstq+wq], m0                     ;     write(m0, $mmsize)
    add              wq, mmsize                 ;     w -= $mmsize
    cmp              wq, -mmsize                ;   } while (w > $mmsize);
    jl .%1_x_loop
    movu             m0, [srcq-mmsize]          ;     m0 = read($mmsize)
    movu  [dstq-mmsize], m0                     ;     write(m0, $mmsize)
%ifidn %1, body                                 ;   if ($type == body) {
    add            srcq, src_strideq            ;     src += src_stride
%endif                                          ;   }
    add            dstq, dst_strideq            ;   dst += dst_stride
    dec              %2                         ; } while (--$h);
    jnz .%1_y_loop
%endmacro

; .----. <- zero
; |    |    <- top is copied from first line in body of source
; |----| <- start_y
; |    |    <- body is copied verbatim (line-by-line) from source
; |----| <- end_y
; |    |    <- bottom is copied from last line in body of source
; '----' <- bh
INIT_XMM sse
%if ARCH_X86_64
cglobal emu_edge_vvar, 7, 8, 1, dst, dst_stride, src, src_stride, \
                                start_y, end_y, bh, w
%else ; x86-32
cglobal emu_edge_vvar, 1, 6, 1, dst, src, start_y, end_y, bh, w
%define src_strideq r3mp
%define dst_strideq r1mp
    mov            srcq, r2mp
    mov        start_yq, r4mp
    mov          end_yq, r5mp
    mov             bhq, r6mp
%endif
    sub             bhq, end_yq                 ; bh    -= end_q
    sub          end_yq, start_yq               ; end_q -= start_q
    add            srcq, r7mp                   ; (r7mp = wmp)
    add            dstq, r7mp                   ; (r7mp = wmp)
    neg            r7mp                         ; (r7mp = wmp)
    test       start_yq, start_yq               ; if (start_q) {
    jz .body
    V_COPY_ROW      top, start_yq               ;   v_copy_row(top, start_yq)
.body:                                          ; }
    V_COPY_ROW     body, end_yq                 ; v_copy_row(body, end_yq)
    test            bhq, bhq                    ; if (bh) {
    jz .end
    sub            srcq, src_strideq            ;   src -= src_stride
    V_COPY_ROW   bottom, bhq                    ;   v_copy_row(bottom, bh)
.end:                                           ; }
    RET

%macro hvar_fn 0
cglobal emu_edge_hvar, 5, 6, 1, dst, dst_stride, start_x, n_words, h, w
    lea            dstq, [dstq+n_wordsq*2]
    neg        n_wordsq
    lea        start_xq, [start_xq+n_wordsq*2]
.y_loop:                                        ; do {
%if cpuflag(avx2)
    vpbroadcastb     m0, [dstq+start_xq]
    mov              wq, n_wordsq               ;   initialize w
%else
    movzx            wd, byte [dstq+start_xq]   ;   w = read(1)
    imul             wd, 0x01010101             ;   w *= 0x01010101
    movd             m0, wd
    mov              wq, n_wordsq               ;   initialize w
    pshufd           m0, m0, q0000              ;   splat
%endif ; avx2
.x_loop:                                        ;   do {
    movu    [dstq+wq*2], m0                     ;     write($reg, $mmsize)
    add              wq, mmsize/2               ;     w -= $mmsize/2
    cmp              wq, -(mmsize/2)            ;   } while (w > $mmsize/2)
    jl .x_loop
    movu  [dstq-mmsize], m0                     ;   write($reg, $mmsize)
    add            dstq, dst_strideq            ;   dst += dst_stride
    dec              hq                         ; } while (h--)
    jnz .y_loop
    RET
%endmacro

INIT_XMM sse2
hvar_fn

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
hvar_fn
%endif

; macro to read/write a horizontal number of pixels (%2) to/from registers
; on sse, - fills xmm0-15 for consecutive sets of 16 pixels
;         - if (%2 & 8)  fills 8 bytes into xmm$next
;         - if (%2 & 4)  fills 4 bytes into xmm$next
;         - if (%2 & 3)  fills 1, 2 or 4 bytes in eax
; on mmx, - fills mm0-7 for consecutive sets of 8 pixels
;         - if (%2 & 4)  fills 4 bytes into mm$next
;         - if (%2 & 3)  fills 1, 2 or 4 bytes in eax
; writing data out is in the same way
%macro READ_NUM_BYTES 2
%assign %%off 0     ; offset in source buffer
%assign %%mmx_idx 0 ; mmx register index
%assign %%xmm_idx 0 ; xmm register index

%rep %2/mmsize
%if mmsize == 16
    movu   xmm %+ %%xmm_idx, [srcq+%%off]
%assign %%xmm_idx %%xmm_idx+1
%else ; mmx
    movu    mm %+ %%mmx_idx, [srcq+%%off]
%assign %%mmx_idx %%mmx_idx+1
%endif
%assign %%off %%off+mmsize
%endrep ; %2/mmsize

%if mmsize == 16
%if (%2-%%off) >= 8
%if %2 > 16 && (%2-%%off) > 8
    movu   xmm %+ %%xmm_idx, [srcq+%2-16]
%assign %%xmm_idx %%xmm_idx+1
%assign %%off %2
%else
    movq    mm %+ %%mmx_idx, [srcq+%%off]
%assign %%mmx_idx %%mmx_idx+1
%assign %%off %%off+8
%endif
%endif ; (%2-%%off) >= 8
%endif

%if (%2-%%off) >= 4
%if %2 > 8 && (%2-%%off) > 4
    movq    mm %+ %%mmx_idx, [srcq+%2-8]
%assign %%off %2
%else
    movd    mm %+ %%mmx_idx, [srcq+%%off]
%assign %%off %%off+4
%endif
%assign %%mmx_idx %%mmx_idx+1
%endif ; (%2-%%off) >= 4

%if (%2-%%off) >= 1
%if %2 >= 4
    movd mm %+ %%mmx_idx, [srcq+%2-4]
%elif (%2-%%off) == 1
    mov            valb, [srcq+%2-1]
%elif (%2-%%off) == 2
    mov            valw, [srcq+%2-2]
%else
    mov            valb, [srcq+%2-1]
    ror            vald, 16
    mov            valw, [srcq+%2-3]
%endif
%endif ; (%2-%%off) >= 1
%endmacro ; READ_NUM_BYTES

%macro WRITE_NUM_BYTES 2
%assign %%off 0     ; offset in destination buffer
%assign %%mmx_idx 0 ; mmx register index
%assign %%xmm_idx 0 ; xmm register index

%rep %2/mmsize
%if mmsize == 16
    movu   [dstq+%%off], xmm %+ %%xmm_idx
%assign %%xmm_idx %%xmm_idx+1
%else ; mmx
    movu   [dstq+%%off], mm %+ %%mmx_idx
%assign %%mmx_idx %%mmx_idx+1
%endif
%assign %%off %%off+mmsize
%endrep ; %2/mmsize

%if mmsize == 16
%if (%2-%%off) >= 8
%if %2 > 16 && (%2-%%off) > 8
    movu   [dstq+%2-16], xmm %+ %%xmm_idx
%assign %%xmm_idx %%xmm_idx+1
%assign %%off %2
%else
    movq   [dstq+%%off], mm %+ %%mmx_idx
%assign %%mmx_idx %%mmx_idx+1
%assign %%off %%off+8
%endif
%endif ; (%2-%%off) >= 8
%endif

%if (%2-%%off) >= 4
%if %2 > 8 && (%2-%%off) > 4
    movq    [dstq+%2-8], mm %+ %%mmx_idx
%assign %%off %2
%else
    movd   [dstq+%%off], mm %+ %%mmx_idx
%assign %%off %%off+4
%endif
%assign %%mmx_idx %%mmx_idx+1
%endif ; (%2-%%off) >= 4

%if (%2-%%off) >= 1
%if %2 >= 4
    movd    [dstq+%2-4], mm %+ %%mmx_idx
%elif (%2-%%off) == 1
    mov     [dstq+%2-1], valb
%elif (%2-%%off) == 2
    mov     [dstq+%2-2], valw
%else
    mov     [dstq+%2-3], valw
    ror            vald, 16
    mov     [dstq+%2-1], valb
%ifnidn %1, body
    ror            vald, 16
%endif
%endif
%endif ; (%2-%%off) >= 1
%endmacro ; WRITE_NUM_BYTES

; vertical top/bottom extend and body copy fast loops
; these are function pointers to set-width line copy functions, i.e.
; they read a fixed number of pixels into set registers, and write
; those out into the destination buffer
%macro VERTICAL_EXTEND 2
%assign %%n %1
%rep 1+%2-%1
%if %%n <= 3
%if ARCH_X86_64
cglobal emu_edge_vfix %+ %%n, 6, 8, 0, dst, dst_stride, src, src_stride, \
                                       start_y, end_y, val, bh
    mov             bhq, r6mp                   ; r6mp = bhmp
%else ; x86-32
cglobal emu_edge_vfix %+ %%n, 0, 6, 0, val, dst, src, start_y, end_y, bh
    mov            dstq, r0mp
    mov            srcq, r2mp
    mov        start_yq, r4mp
    mov          end_yq, r5mp
    mov             bhq, r6mp
%define dst_strideq r1mp
%define src_strideq r3mp
%endif ; x86-64/32
%else
%if ARCH_X86_64
cglobal emu_edge_vfix %+ %%n, 7, 7, 1, dst, dst_stride, src, src_stride, \
                                       start_y, end_y, bh
%else ; x86-32
cglobal emu_edge_vfix %+ %%n, 1, 5, 1, dst, src, start_y, end_y, bh
    mov            srcq, r2mp
    mov        start_yq, r4mp
    mov          end_yq, r5mp
    mov             bhq, r6mp
%define dst_strideq r1mp
%define src_strideq r3mp
%endif ; x86-64/32
%endif
    ; FIXME move this to c wrapper?
    sub             bhq, end_yq                 ; bh    -= end_y
    sub          end_yq, start_yq               ; end_y -= start_y

    ; extend pixels above body
    test       start_yq, start_yq               ; if (start_y) {
    jz .body_loop
    READ_NUM_BYTES  top, %%n                    ;   $variable_regs = read($n)
.top_loop:                                      ;   do {
    WRITE_NUM_BYTES top, %%n                    ;     write($variable_regs, $n)
    add            dstq, dst_strideq            ;     dst += linesize
    dec        start_yq                         ;   } while (--start_y)
    jnz .top_loop                               ; }

    ; copy body pixels
.body_loop:                                     ; do {
    READ_NUM_BYTES  body, %%n                   ;   $variable_regs = read($n)
    WRITE_NUM_BYTES body, %%n                   ;   write($variable_regs, $n)
    add            dstq, dst_strideq            ;   dst += dst_stride
    add            srcq, src_strideq            ;   src += src_stride
    dec          end_yq                         ; } while (--end_y)
    jnz .body_loop

    ; copy bottom pixels
    test            bhq, bhq                    ; if (block_h) {
    jz .end
    sub            srcq, src_strideq            ;   src -= linesize
    READ_NUM_BYTES  bottom, %%n                 ;   $variable_regs = read($n)
.bottom_loop:                                   ;   do {
    WRITE_NUM_BYTES bottom, %%n                 ;     write($variable_regs, $n)
    add            dstq, dst_strideq            ;     dst += linesize
    dec             bhq                         ;   } while (--bh)
    jnz .bottom_loop                            ; }

.end:
    RET
%assign %%n %%n+1
%endrep ; 1+%2-%1
%endmacro ; VERTICAL_EXTEND

INIT_MMX mmx
VERTICAL_EXTEND 1, 15

INIT_XMM sse
VERTICAL_EXTEND 16, 22

; left/right (horizontal) fast extend functions
; these are essentially identical to the vertical extend ones above,
; just left/right separated because number of pixels to extend is
; obviously not the same on both sides.

%macro READ_V_PIXEL 2
%if cpuflag(avx2)
    vpbroadcastb     m0, %2
%else
    movzx          vald, byte %2
    imul           vald, 0x01010101
%if %1 >= 8
    movd             m0, vald
%if mmsize == 16
    pshufd           m0, m0, q0000
%else
    punpckldq        m0, m0
%endif ; mmsize == 16
%endif ; %1 > 16
%endif ; avx2
%endmacro ; READ_V_PIXEL

%macro WRITE_V_PIXEL 2
%assign %%off 0

%if %1 >= 8

%rep %1/mmsize
    movu     [%2+%%off], m0
%assign %%off %%off+mmsize
%endrep ; %1/mmsize

%if mmsize == 16
%if %1-%%off >= 8
%if %1 > 16 && %1-%%off > 8
    movu     [%2+%1-16], m0
%assign %%off %1
%else
    movq     [%2+%%off], m0
%assign %%off %%off+8
%endif
%endif ; %1-%%off >= 8
%endif ; mmsize == 16

%if %1-%%off >= 4
%if %1 > 8 && %1-%%off > 4
    movq      [%2+%1-8], m0
%assign %%off %1
%else
    movd     [%2+%%off], m0
%assign %%off %%off+4
%endif
%endif ; %1-%%off >= 4

%else ; %1 < 8

%rep %1/4
    mov      [%2+%%off], vald
%assign %%off %%off+4
%endrep ; %1/4

%endif ; %1 >=/< 8

%if %1-%%off == 2
%if cpuflag(avx2)
    movd     [%2+%%off-2], m0
%else
    mov      [%2+%%off], valw
%endif ; avx2
%endif ; (%1-%%off)/2
%endmacro ; WRITE_V_PIXEL

%macro H_EXTEND 2
%assign %%n %1
%rep 1+(%2-%1)/2
%if cpuflag(avx2)
cglobal emu_edge_hfix %+ %%n, 4, 4, 1, dst, dst_stride, start_x, bh
%else
cglobal emu_edge_hfix %+ %%n, 4, 5, 1, dst, dst_stride, start_x, bh, val
%endif
.loop_y:                                        ; do {
    READ_V_PIXEL    %%n, [dstq+start_xq]        ;   $variable_regs = read($n)
    WRITE_V_PIXEL   %%n, dstq                   ;   write($variable_regs, $n)
    add            dstq, dst_strideq            ;   dst += dst_stride
    dec             bhq                         ; } while (--bh)
    jnz .loop_y
    RET
%assign %%n %%n+2
%endrep ; 1+(%2-%1)/2
%endmacro ; H_EXTEND

INIT_MMX mmx
H_EXTEND 2, 14

INIT_XMM sse2
H_EXTEND 16, 22

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
H_EXTEND 8, 22
%endif

INIT_MMX mmxext
cglobal prefetch, 3, 3, 0, buf, stride, h
.loop:
    prefetcht0 [bufq]
    add      bufq, strideq
    dec        hd
    jg .loop
    RET
