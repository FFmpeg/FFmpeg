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

; extern void ff_emu_edge_core(uint8_t *buf, const uint8_t *src, x86_reg linesize,
;                              x86_reg start_y, x86_reg end_y, x86_reg block_h,
;                              x86_reg start_x, x86_reg end_x, x86_reg block_w);
;
; The actual function itself is below. It basically wraps a very simple
; w = end_x - start_x
; if (w) {
;   if (w > 22) {
;     jump to the slow loop functions
;   } else {
;     jump to the fast loop functions
;   }
; }
;
; ... and then the same for left/right extend also. See below for loop
; function implementations. Fast are fixed-width, slow is variable-width

%macro EMU_EDGE_FUNC 0
%if ARCH_X86_64
%define w_reg r7
cglobal emu_edge_core, 6, 9, 1
    mov         r8, r5          ; save block_h
%else
%define w_reg r6
cglobal emu_edge_core, 2, 7, 0
    mov         r4, r4m         ; end_y
    mov         r5, r5m         ; block_h
%endif

    ; start with vertical extend (top/bottom) and body pixel copy
    mov      w_reg, r7m
    sub      w_reg, r6m         ; w = start_x - end_x
    sub         r5, r4
%if ARCH_X86_64
    sub         r4, r3
%else
    sub         r4, dword r3m
%endif
    cmp      w_reg, 22
    jg .slow_v_extend_loop
%if ARCH_X86_32
    mov         r2, r2m         ; linesize
%endif
    sal      w_reg, 7           ; w * 128
%ifdef PIC
    lea        rax, [.emuedge_v_extend_1 - (.emuedge_v_extend_2 - .emuedge_v_extend_1)]
    add      w_reg, rax
%else
    lea      w_reg, [.emuedge_v_extend_1 - (.emuedge_v_extend_2 - .emuedge_v_extend_1)+w_reg]
%endif
    call     w_reg              ; fast top extend, body copy and bottom extend
.v_extend_end:

    ; horizontal extend (left/right)
    mov      w_reg, r6m         ; start_x
    sub         r0, w_reg
%if ARCH_X86_64
    mov         r3, r0          ; backup of buf+block_h*linesize
    mov         r5, r8
%else
    mov        r0m, r0          ; backup of buf+block_h*linesize
    mov         r5, r5m
%endif
    test     w_reg, w_reg
    jz .right_extend
    cmp      w_reg, 22
    jg .slow_left_extend_loop
    mov         r1, w_reg
    dec      w_reg
    ; FIXME we can do a if size == 1 here if that makes any speed difference, test me
    sar      w_reg, 1
    sal      w_reg, 6
    ; r0=buf+block_h*linesize,r7(64)/r6(32)=start_x offset for funcs
    ; r6(rax)/r3(ebx)=val,r2=linesize,r1=start_x,r5=block_h
%ifdef PIC
    lea        rax, [.emuedge_extend_left_2]
    add      w_reg, rax
%else
    lea      w_reg, [.emuedge_extend_left_2+w_reg]
%endif
    call     w_reg

    ; now r3(64)/r0(32)=buf,r2=linesize,r8/r5=block_h,r6/r3=val, r7/r6=end_x, r1=block_w
.right_extend:
%if ARCH_X86_32
    mov         r0, r0m
    mov         r5, r5m
%endif
    mov      w_reg, r7m         ; end_x
    mov         r1, r8m         ; block_w
    mov         r4, r1
    sub         r1, w_reg
    jz .h_extend_end            ; if (end_x == block_w) goto h_extend_end
    cmp         r1, 22
    jg .slow_right_extend_loop
    dec         r1
    ; FIXME we can do a if size == 1 here if that makes any speed difference, test me
    sar         r1, 1
    sal         r1, 6
%ifdef PIC
    lea        rax, [.emuedge_extend_right_2]
    add         r1, rax
%else
    lea         r1, [.emuedge_extend_right_2+r1]
%endif
    call        r1
.h_extend_end:
    RET

%if ARCH_X86_64
%define vall  al
%define valh  ah
%define valw  ax
%define valw2 r7w
%define valw3 r3w
%if WIN64
%define valw4 r7w
%else ; unix64
%define valw4 r3w
%endif
%define vald eax
%else
%define vall  bl
%define valh  bh
%define valw  bx
%define valw2 r6w
%define valw3 valw2
%define valw4 valw3
%define vald ebx
%define stack_offset 0x14
%endif

%endmacro

; macro to read/write a horizontal number of pixels (%2) to/from registers
; on x86-64, - fills xmm0-15 for consecutive sets of 16 pixels
;            - if (%2 & 15 == 8) fills the last 8 bytes into rax
;            - else if (%2 & 8)  fills 8 bytes into mm0
;            - if (%2 & 7 == 4)  fills the last 4 bytes into rax
;            - else if (%2 & 4)  fills 4 bytes into mm0-1
;            - if (%2 & 3 == 3)  fills 2 bytes into r7/r3, and 1 into eax
;              (note that we're using r3 for body/bottom because it's a shorter
;               opcode, and then the loop fits in 128 bytes)
;            - else              fills remaining bytes into rax
; on x86-32, - fills mm0-7 for consecutive sets of 8 pixels
;            - if (%2 & 7 == 4)  fills 4 bytes into ebx
;            - else if (%2 & 4)  fills 4 bytes into mm0-7
;            - if (%2 & 3 == 3)  fills 2 bytes into r6, and 1 into ebx
;            - else              fills remaining bytes into ebx
; writing data out is in the same way
%macro READ_NUM_BYTES 2
%assign %%src_off 0 ; offset in source buffer
%assign %%smidx   0 ; mmx register idx
%assign %%sxidx   0 ; xmm register idx

%if cpuflag(sse)
%rep %2/16
    movups xmm %+ %%sxidx, [r1+%%src_off]
%assign %%src_off %%src_off+16
%assign %%sxidx   %%sxidx+1
%endrep ; %2/16
%endif

%if ARCH_X86_64
%if (%2-%%src_off) == 8
    mov           rax, [r1+%%src_off]
%assign %%src_off %%src_off+8
%endif ; (%2-%%src_off) == 8
%endif ; x86-64

%rep (%2-%%src_off)/8
    movq    mm %+ %%smidx, [r1+%%src_off]
%assign %%src_off %%src_off+8
%assign %%smidx   %%smidx+1
%endrep ; (%2-%%dst_off)/8

%if (%2-%%src_off) == 4
    mov          vald, [r1+%%src_off]
%elif (%2-%%src_off) & 4
    movd    mm %+ %%smidx, [r1+%%src_off]
%assign %%src_off %%src_off+4
%endif ; (%2-%%src_off) ==/& 4

%if (%2-%%src_off) == 1
    mov          vall, [r1+%%src_off]
%elif (%2-%%src_off) == 2
    mov          valw, [r1+%%src_off]
%elif (%2-%%src_off) == 3
%ifidn %1, top
    mov         valw2, [r1+%%src_off]
%elifidn %1, body
    mov         valw3, [r1+%%src_off]
%elifidn %1, bottom
    mov         valw4, [r1+%%src_off]
%endif ; %1 ==/!= top
    mov          vall, [r1+%%src_off+2]
%endif ; (%2-%%src_off) == 1/2/3
%endmacro ; READ_NUM_BYTES

%macro WRITE_NUM_BYTES 2
%assign %%dst_off 0 ; offset in destination buffer
%assign %%dmidx   0 ; mmx register idx
%assign %%dxidx   0 ; xmm register idx

%if cpuflag(sse)
%rep %2/16
    movups [r0+%%dst_off], xmm %+ %%dxidx
%assign %%dst_off %%dst_off+16
%assign %%dxidx   %%dxidx+1
%endrep ; %2/16
%endif

%if ARCH_X86_64
%if (%2-%%dst_off) == 8
    mov    [r0+%%dst_off], rax
%assign %%dst_off %%dst_off+8
%endif ; (%2-%%dst_off) == 8
%endif ; x86-64

%rep (%2-%%dst_off)/8
    movq   [r0+%%dst_off], mm %+ %%dmidx
%assign %%dst_off %%dst_off+8
%assign %%dmidx   %%dmidx+1
%endrep ; (%2-%%dst_off)/8

%if (%2-%%dst_off) == 4
    mov    [r0+%%dst_off], vald
%elif (%2-%%dst_off) & 4
    movd   [r0+%%dst_off], mm %+ %%dmidx
%assign %%dst_off %%dst_off+4
%endif ; (%2-%%dst_off) ==/& 4

%if (%2-%%dst_off) == 1
    mov    [r0+%%dst_off], vall
%elif (%2-%%dst_off) == 2
    mov    [r0+%%dst_off], valw
%elif (%2-%%dst_off) == 3
%ifidn %1, top
    mov    [r0+%%dst_off], valw2
%elifidn %1, body
    mov    [r0+%%dst_off], valw3
%elifidn %1, bottom
    mov    [r0+%%dst_off], valw4
%endif ; %1 ==/!= top
    mov  [r0+%%dst_off+2], vall
%endif ; (%2-%%dst_off) == 1/2/3
%endmacro ; WRITE_NUM_BYTES

; vertical top/bottom extend and body copy fast loops
; these are function pointers to set-width line copy functions, i.e.
; they read a fixed number of pixels into set registers, and write
; those out into the destination buffer
; r0=buf,r1=src,r2=linesize,r3(64)/r3m(32)=start_x,r4=end_y,r5=block_h
; r6(eax/64)/r3(ebx/32)=val_reg
%macro VERTICAL_EXTEND 0
%assign %%n 1
%rep 22
ALIGN 128
.emuedge_v_extend_ %+ %%n:
    ; extend pixels above body
%if ARCH_X86_64
    test           r3 , r3                   ; if (!start_y)
    jz .emuedge_copy_body_ %+ %%n %+ _loop   ;   goto body
%else ; ARCH_X86_32
    cmp      dword r3m, 0
    je .emuedge_copy_body_ %+ %%n %+ _loop
%endif ; ARCH_X86_64/32
    READ_NUM_BYTES  top,    %%n              ; read bytes
.emuedge_extend_top_ %+ %%n %+ _loop:        ; do {
    WRITE_NUM_BYTES top,    %%n              ;   write bytes
    add            r0 , r2                   ;   dst += linesize
%if ARCH_X86_64
    dec            r3d
%else ; ARCH_X86_32
    dec      dword r3m
%endif ; ARCH_X86_64/32
    jnz .emuedge_extend_top_ %+ %%n %+ _loop ; } while (--start_y)

    ; copy body pixels
.emuedge_copy_body_ %+ %%n %+ _loop:         ; do {
    READ_NUM_BYTES  body,   %%n              ;   read bytes
    WRITE_NUM_BYTES body,   %%n              ;   write bytes
    add            r0 , r2                   ;   dst += linesize
    add            r1 , r2                   ;   src += linesize
    dec            r4d
    jnz .emuedge_copy_body_ %+ %%n %+ _loop  ; } while (--end_y)

    ; copy bottom pixels
    test           r5 , r5                   ; if (!block_h)
    jz .emuedge_v_extend_end_ %+ %%n         ;   goto end
    sub            r1 , r2                   ; src -= linesize
    READ_NUM_BYTES  bottom, %%n              ; read bytes
.emuedge_extend_bottom_ %+ %%n %+ _loop:     ; do {
    WRITE_NUM_BYTES bottom, %%n              ;   write bytes
    add            r0 , r2                   ;   dst += linesize
    dec            r5d
    jnz .emuedge_extend_bottom_ %+ %%n %+ _loop ; } while (--block_h)

.emuedge_v_extend_end_ %+ %%n:
%if ARCH_X86_64
    ret
%else ; ARCH_X86_32
    rep ret
%endif ; ARCH_X86_64/32
%assign %%n %%n+1
%endrep
%endmacro VERTICAL_EXTEND

; left/right (horizontal) fast extend functions
; these are essentially identical to the vertical extend ones above,
; just left/right separated because number of pixels to extend is
; obviously not the same on both sides.
; for reading, pixels are placed in eax (x86-64) or ebx (x86-64) in the
; lowest two bytes of the register (so val*0x0101), and are splatted
; into each byte of mm0 as well if n_pixels >= 8

%macro READ_V_PIXEL 2
    mov        vall, %2
    mov        valh, vall
%if %1 >= 8
    movd        mm0, vald
%if cpuflag(mmxext)
    pshufw      mm0, mm0, 0
%else ; mmx
    punpcklwd   mm0, mm0
    punpckldq   mm0, mm0
%endif ; sse
%endif ; %1 >= 8
%endmacro

%macro WRITE_V_PIXEL 2
%assign %%dst_off 0
%rep %1/8
    movq [%2+%%dst_off], mm0
%assign %%dst_off %%dst_off+8
%endrep
%if %1 & 4
%if %1 >= 8
    movd [%2+%%dst_off], mm0
%else ; %1 < 8
    mov  [%2+%%dst_off]  , valw
    mov  [%2+%%dst_off+2], valw
%endif ; %1 >=/< 8
%assign %%dst_off %%dst_off+4
%endif ; %1 & 4
%if %1&2
    mov  [%2+%%dst_off], valw
%endif ; %1 & 2
%endmacro

; r0=buf+block_h*linesize, r1=start_x, r2=linesize, r5=block_h, r6/r3=val
%macro LEFT_EXTEND 0
%assign %%n 2
%rep 11
ALIGN 64
.emuedge_extend_left_ %+ %%n:          ; do {
    sub         r0, r2                 ;   dst -= linesize
    READ_V_PIXEL  %%n, [r0+r1]         ;   read pixels
    WRITE_V_PIXEL %%n, r0              ;   write pixels
    dec         r5
    jnz .emuedge_extend_left_ %+ %%n   ; } while (--block_h)
%if ARCH_X86_64
    ret
%else ; ARCH_X86_32
    rep ret
%endif ; ARCH_X86_64/32
%assign %%n %%n+2
%endrep
%endmacro ; LEFT_EXTEND

; r3/r0=buf+block_h*linesize, r2=linesize, r8/r5=block_h, r0/r6=end_x, r6/r3=val
%macro RIGHT_EXTEND 0
%assign %%n 2
%rep 11
ALIGN 64
.emuedge_extend_right_ %+ %%n:          ; do {
%if ARCH_X86_64
    sub        r3, r2                   ;   dst -= linesize
    READ_V_PIXEL  %%n, [r3+w_reg-1]     ;   read pixels
    WRITE_V_PIXEL %%n, r3+r4-%%n        ;   write pixels
    dec       r8
%else ; ARCH_X86_32
    sub        r0, r2                   ;   dst -= linesize
    READ_V_PIXEL  %%n, [r0+w_reg-1]     ;   read pixels
    WRITE_V_PIXEL %%n, r0+r4-%%n        ;   write pixels
    dec     r5
%endif ; ARCH_X86_64/32
    jnz .emuedge_extend_right_ %+ %%n   ; } while (--block_h)
%if ARCH_X86_64
    ret
%else ; ARCH_X86_32
    rep ret
%endif ; ARCH_X86_64/32
%assign %%n %%n+2
%endrep

%if ARCH_X86_32
%define stack_offset 0x10
%endif
%endmacro ; RIGHT_EXTEND

; below follow the "slow" copy/extend functions, these act on a non-fixed
; width specified in a register, and run a loop to copy the full amount
; of bytes. They are optimized for copying of large amounts of pixels per
; line, so they unconditionally splat data into mm registers to copy 8
; bytes per loop iteration. It could be considered to use xmm for x86-64
; also, but I haven't optimized this as much (i.e. FIXME)
%macro V_COPY_NPX 4-5
%if %0 == 4
    test     w_reg, %4
    jz .%1_skip_%4_px
%else ; %0 == 5
.%1_%4_px_loop:
%endif
    %3          %2, [r1+cnt_reg]
    %3 [r0+cnt_reg], %2
    add    cnt_reg, %4
%if %0 == 5
    sub      w_reg, %4
    test     w_reg, %5
    jnz .%1_%4_px_loop
%endif
.%1_skip_%4_px:
%endmacro

%macro V_COPY_ROW 2
%ifidn %1, bottom
    sub         r1, linesize
%endif
.%1_copy_loop:
    xor    cnt_reg, cnt_reg
%if notcpuflag(sse)
%define linesize r2m
    V_COPY_NPX %1,  mm0, movq,    8, 0xFFFFFFF8
%else ; sse
    V_COPY_NPX %1, xmm0, movups, 16, 0xFFFFFFF0
%if ARCH_X86_64
%define linesize r2
    V_COPY_NPX %1, rax , mov,     8
%else ; ARCH_X86_32
%define linesize r2m
    V_COPY_NPX %1,  mm0, movq,    8
%endif ; ARCH_X86_64/32
%endif ; sse
    V_COPY_NPX %1, vald, mov,     4
    V_COPY_NPX %1, valw, mov,     2
    V_COPY_NPX %1, vall, mov,     1
    mov      w_reg, cnt_reg
%ifidn %1, body
    add         r1, linesize
%endif
    add         r0, linesize
    dec         %2
    jnz .%1_copy_loop
%endmacro

%macro SLOW_V_EXTEND 0
.slow_v_extend_loop:
; r0=buf,r1=src,r2(64)/r2m(32)=linesize,r3(64)/r3m(32)=start_x,r4=end_y,r5=block_h
; r8(64)/r3(later-64)/r2(32)=cnt_reg,r6(64)/r3(32)=val_reg,r7(64)/r6(32)=w=end_x-start_x
%if ARCH_X86_64
    push        r8              ; save old value of block_h
    test        r3, r3
%define cnt_reg r8
    jz .do_body_copy            ; if (!start_y) goto do_body_copy
    V_COPY_ROW top, r3
%else
    cmp  dword r3m, 0
%define cnt_reg r2
    je .do_body_copy            ; if (!start_y) goto do_body_copy
    V_COPY_ROW top, dword r3m
%endif

.do_body_copy:
    V_COPY_ROW body, r4

%if ARCH_X86_64
    pop         r8              ; restore old value of block_h
%define cnt_reg r3
%endif
    test        r5, r5
%if ARCH_X86_64
    jz .v_extend_end
%else
    jz .skip_bottom_extend
%endif
    V_COPY_ROW bottom, r5
%if ARCH_X86_32
.skip_bottom_extend:
    mov         r2, r2m
%endif
    jmp .v_extend_end
%endmacro

%macro SLOW_LEFT_EXTEND 0
.slow_left_extend_loop:
; r0=buf+block_h*linesize,r2=linesize,r6(64)/r3(32)=val,r5=block_h,r4=cntr,r7/r6=start_x
    mov         r4, 8
    sub         r0, linesize
    READ_V_PIXEL 8, [r0+w_reg]
.left_extend_8px_loop:
    movq [r0+r4-8], mm0
    add         r4, 8
    cmp         r4, w_reg
    jle .left_extend_8px_loop
    sub         r4, 8
    cmp         r4, w_reg
    jge .left_extend_loop_end
.left_extend_2px_loop:
    mov    [r0+r4], valw
    add         r4, 2
    cmp         r4, w_reg
    jl .left_extend_2px_loop
.left_extend_loop_end:
    dec         r5
    jnz .slow_left_extend_loop
%if ARCH_X86_32
    mov         r2, r2m
%endif
    jmp .right_extend
%endmacro

%macro SLOW_RIGHT_EXTEND 0
.slow_right_extend_loop:
; r3(64)/r0(32)=buf+block_h*linesize,r2=linesize,r4=block_w,r8(64)/r5(32)=block_h,
; r7(64)/r6(32)=end_x,r6/r3=val,r1=cntr
%if ARCH_X86_64
%define buf_reg r3
%define bh_reg r8
%else
%define buf_reg r0
%define bh_reg r5
%endif
    lea         r1, [r4-8]
    sub    buf_reg, linesize
    READ_V_PIXEL 8, [buf_reg+w_reg-1]
.right_extend_8px_loop:
    movq [buf_reg+r1], mm0
    sub         r1, 8
    cmp         r1, w_reg
    jge .right_extend_8px_loop
    add         r1, 8
    cmp         r1, w_reg
    je .right_extend_loop_end
.right_extend_2px_loop:
    sub         r1, 2
    mov [buf_reg+r1], valw
    cmp         r1, w_reg
    jg .right_extend_2px_loop
.right_extend_loop_end:
    dec         bh_reg
    jnz .slow_right_extend_loop
    jmp .h_extend_end
%endmacro

%macro emu_edge 1
INIT_XMM %1
EMU_EDGE_FUNC
VERTICAL_EXTEND
LEFT_EXTEND
RIGHT_EXTEND
SLOW_V_EXTEND
SLOW_LEFT_EXTEND
SLOW_RIGHT_EXTEND
%endmacro

emu_edge sse
%if ARCH_X86_32
emu_edge mmx
%endif

%macro PREFETCH_FN 1
cglobal prefetch, 3, 3, 0, buf, stride, h
.loop:
    %1      [bufq]
    add      bufq, strideq
    dec        hd
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PREFETCH_FN prefetcht0
%if ARCH_X86_32
INIT_MMX 3dnow
PREFETCH_FN prefetch
%endif
