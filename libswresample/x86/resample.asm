;******************************************************************************
;* Copyright (c) 2012 Michael Niedermayer
;* Copyright (c) 2014 James Almer <jamrial <at> gmail.com>
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

%if ARCH_X86_64
%define pointer resq
%else
%define pointer resd
%endif

struc ResampleContext
    .av_class:              pointer 1
    .filter_bank:           pointer 1
    .filter_length:         resd 1
    .filter_alloc:          resd 1
    .ideal_dst_incr:        resd 1
    .dst_incr:              resd 1
    .dst_incr_div:          resd 1
    .dst_incr_mod:          resd 1
    .index:                 resd 1
    .frac:                  resd 1
    .src_incr:              resd 1
    .compensation_distance: resd 1
    .phase_shift:           resd 1
    .phase_mask:            resd 1

    ; there's a few more here but we only care about the first few
endstruc

SECTION_RODATA

pf_1: dd 1.0

SECTION .text

%macro RESAMPLE_FLOAT_FNS 0
; int resample_common_float(ResampleContext *ctx, float *dst,
;                           const float *src, int size, int update_ctx)
%if ARCH_X86_64 ; unix64 and win64
cglobal resample_common_float, 0, 15, 2, ctx, dst, src, phase_shift, index, frac, \
                                         dst_incr_mod, size, min_filter_count_x4, \
                                         min_filter_len_x4, dst_incr_div, src_incr, \
                                         phase_mask, dst_end, filter_bank

    ; use red-zone for variable storage
%define ctx_stackq            [rsp-0x8]
%define src_stackq            [rsp-0x10]
%if WIN64
%define update_context_stackd r4m
%else ; unix64
%define update_context_stackd [rsp-0x14]
%endif

    ; load as many variables in registers as possible; for the rest, store
    ; on stack so that we have 'ctx' available as one extra register
    mov                        sized, r3d
    mov                  phase_maskd, [ctxq+ResampleContext.phase_mask]
%if UNIX64
    mov        update_context_stackd, r4d
%endif
    mov                       indexd, [ctxq+ResampleContext.index]
    mov                        fracd, [ctxq+ResampleContext.frac]
    mov                dst_incr_modd, [ctxq+ResampleContext.dst_incr_mod]
    mov                 filter_bankq, [ctxq+ResampleContext.filter_bank]
    mov                    src_incrd, [ctxq+ResampleContext.src_incr]
    mov                   ctx_stackq, ctxq
    mov           min_filter_len_x4d, [ctxq+ResampleContext.filter_length]
    mov                dst_incr_divd, [ctxq+ResampleContext.dst_incr_div]
    shl           min_filter_len_x4d, 2
    lea                     dst_endq, [dstq+sizeq*4]

%if UNIX64
    mov                          ecx, [ctxq+ResampleContext.phase_shift]
    mov                          edi, [ctxq+ResampleContext.filter_alloc]

    DEFINE_ARGS filter_alloc, dst, src, phase_shift, index, frac, dst_incr_mod, \
                filter, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, phase_mask, dst_end, filter_bank
%elif WIN64
    mov                          R9d, [ctxq+ResampleContext.filter_alloc]
    mov                          ecx, [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS phase_shift, dst, src, filter_alloc, index, frac, dst_incr_mod, \
                filter, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, phase_mask, dst_end, filter_bank
%endif

    neg           min_filter_len_x4q
    sub                 filter_bankq, min_filter_len_x4q
    sub                         srcq, min_filter_len_x4q
    mov                   src_stackq, srcq
%else ; x86-32
cglobal resample_common_float, 1, 7, 2, ctx, phase_shift, dst, frac, \
                                        index, min_filter_length_x4, filter_bank

    ; push temp variables to stack
%define ctx_stackq            r0mp
%define src_stackq            r2mp
%define update_context_stackd r4m

    mov                         dstq, r1mp
    mov                           r3, r3mp
    lea                           r3, [dstq+r3*4]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_div]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_mod]
    PUSH                              dword [ctxq+ResampleContext.filter_alloc]
    PUSH                              r3
    PUSH                              dword [ctxq+ResampleContext.phase_mask]
    PUSH                              dword [ctxq+ResampleContext.src_incr]
    mov        min_filter_length_x4d, [ctxq+ResampleContext.filter_length]
    mov                       indexd, [ctxq+ResampleContext.index]
    shl        min_filter_length_x4d, 2
    mov                        fracd, [ctxq+ResampleContext.frac]
    neg        min_filter_length_x4q
    mov                 filter_bankq, [ctxq+ResampleContext.filter_bank]
    sub                         r2mp, min_filter_length_x4q
    sub                 filter_bankq, min_filter_length_x4q
    PUSH                              min_filter_length_x4q
    PUSH                              filter_bankq
    mov                 phase_shiftd, [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS src, phase_shift, dst, frac, index, min_filter_count_x4, filter

%define filter_bankq          dword [rsp+0x0]
%define min_filter_length_x4q dword [rsp+0x4]
%define src_incrd             dword [rsp+0x8]
%define phase_maskd           dword [rsp+0xc]
%define dst_endq              dword [rsp+0x10]
%define filter_allocd         dword [rsp+0x14]
%define dst_incr_modd         dword [rsp+0x18]
%define dst_incr_divd         dword [rsp+0x1c]

    mov                         srcq, r2mp
%endif

.loop:
    mov                      filterd, filter_allocd
    imul                     filterd, indexd
%if ARCH_X86_64
    mov         min_filter_count_x4q, min_filter_len_x4q
    lea                      filterq, [filter_bankq+filterq*4]
%else ; x86-32
    mov         min_filter_count_x4q, filter_bankq
    lea                      filterq, [min_filter_count_x4q+filterq*4]
    mov         min_filter_count_x4q, min_filter_length_x4q
%endif
    xorps                         m0, m0, m0

    align 16
.inner_loop:
    movups                        m1, [srcq+min_filter_count_x4q*1]
    mulps                         m1, m1, [filterq+min_filter_count_x4q*1]
    addps                         m0, m0, m1
    add         min_filter_count_x4q, mmsize
    js .inner_loop

%if cpuflag(avx)
    vextractf128                 xm1, m0, 0x1
    addps                        xm0, xm1
%endif

    ; horizontal sum & store
    movhlps                      xm1, xm0
    addps                        xm0, xm1
    shufps                       xm1, xm0, xm0, q0001
    add                        fracd, dst_incr_modd
    addps                        xm0, xm1
    add                       indexd, dst_incr_divd
    movss                     [dstq], xm0
    cmp                        fracd, src_incrd
    jl .skip
    sub                        fracd, src_incrd
    inc                       indexd

%if UNIX64
    DEFINE_ARGS filter_alloc, dst, src, phase_shift, index, frac, dst_incr_mod, \
                index_incr, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, phase_mask, dst_end, filter_bank
%elif WIN64
    DEFINE_ARGS phase_shift, dst, src, filter_alloc, index, frac, dst_incr_mod, \
                index_incr, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, phase_mask, dst_end, filter_bank
%else ; x86-32
    DEFINE_ARGS src, phase_shift, dst, frac, index, index_incr
%endif

.skip:
    mov                  index_incrd, indexd
    add                         dstq, 4
    and                       indexd, phase_maskd
    sar                  index_incrd, phase_shiftb
    lea                         srcq, [srcq+index_incrq*4]
    cmp                         dstq, dst_endq
    jne .loop

%if ARCH_X86_64
    DEFINE_ARGS ctx, dst, src, phase_shift, index, frac
%else ; x86-32
    DEFINE_ARGS src, ctx, update_context, frac, index
%endif

    cmp  dword update_context_stackd, 0
    jz .skip_store
    ; strictly speaking, the function should always return the consumed
    ; number of bytes; however, we only use the value if update_context
    ; is true, so let's just leave it uninitialized otherwise
    mov                         ctxq, ctx_stackq
    movifnidn                    rax, srcq
    mov [ctxq+ResampleContext.frac ], fracd
    sub                          rax, src_stackq
    mov [ctxq+ResampleContext.index], indexd
    shr                          rax, 2

.skip_store:
%if ARCH_X86_32
    ADD                          rsp, 0x20
%endif
    RET

; int resample_linear_float(ResampleContext *ctx, float *dst,
;                           const float *src, int size, int update_ctx)
%if ARCH_X86_64 ; unix64 and win64
cglobal resample_linear_float, 0, 15, 5, ctx, dst, src, phase_shift, index, frac, \
                                         dst_incr_mod, size, min_filter_count_x4, \
                                         min_filter_len_x4, dst_incr_div, src_incr, \
                                         phase_mask, dst_end, filter_bank

    ; use red-zone for variable storage
%define ctx_stackq            [rsp-0x8]
%define src_stackq            [rsp-0x10]
%define phase_mask_stackd     [rsp-0x14]
%if WIN64
%define update_context_stackd r4m
%else ; unix64
%define update_context_stackd [rsp-0x18]
%endif

    ; load as many variables in registers as possible; for the rest, store
    ; on stack so that we have 'ctx' available as one extra register
    mov                        sized, r3d
    mov                  phase_maskd, [ctxq+ResampleContext.phase_mask]
%if UNIX64
    mov        update_context_stackd, r4d
%endif
    mov                       indexd, [ctxq+ResampleContext.index]
    mov                        fracd, [ctxq+ResampleContext.frac]
    mov                dst_incr_modd, [ctxq+ResampleContext.dst_incr_mod]
    mov                 filter_bankq, [ctxq+ResampleContext.filter_bank]
    mov                    src_incrd, [ctxq+ResampleContext.src_incr]
    mov                   ctx_stackq, ctxq
    mov            phase_mask_stackd, phase_maskd
    mov           min_filter_len_x4d, [ctxq+ResampleContext.filter_length]
    cvtsi2ss                     xm0, src_incrd
    movss                        xm4, [pf_1]
    divss                        xm4, xm0
    mov                dst_incr_divd, [ctxq+ResampleContext.dst_incr_div]
    shl           min_filter_len_x4d, 2
    lea                     dst_endq, [dstq+sizeq*4]

%if UNIX64
    mov                          ecx, [ctxq+ResampleContext.phase_shift]
    mov                          edi, [ctxq+ResampleContext.filter_alloc]

    DEFINE_ARGS filter_alloc, dst, src, phase_shift, index, frac, dst_incr_mod, \
                filter1, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, filter2, dst_end, filter_bank
%elif WIN64
    mov                          R9d, [ctxq+ResampleContext.filter_alloc]
    mov                          ecx, [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS phase_shift, dst, src, filter_alloc, index, frac, dst_incr_mod, \
                filter1, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, filter2, dst_end, filter_bank
%endif

    neg           min_filter_len_x4q
    sub                 filter_bankq, min_filter_len_x4q
    sub                         srcq, min_filter_len_x4q
    mov                   src_stackq, srcq
%else ; x86-32
cglobal resample_linear_float, 1, 7, 5, ctx, filter1, dst, frac, \
                                        index, min_filter_length_x4, filter_bank

    ; push temp variables to stack
%define ctx_stackq            r0mp
%define src_stackq            r2mp
%define update_context_stackd r4m

    mov                         dstq, r1mp
    mov                           r3, r3mp
    lea                           r3, [dstq+r3*4]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_div]
    PUSH                              r3
    mov                           r3, dword [ctxq+ResampleContext.filter_alloc]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_mod]
    PUSH                              r3
    shl                           r3, 2
    PUSH                              r3
    mov                           r3, dword [ctxq+ResampleContext.src_incr]
    PUSH                              dword [ctxq+ResampleContext.phase_mask]
    PUSH                              r3d
    cvtsi2ss                     xm0, r3d
    movss                        xm4, [pf_1]
    divss                        xm4, xm0
    mov        min_filter_length_x4d, [ctxq+ResampleContext.filter_length]
    mov                       indexd, [ctxq+ResampleContext.index]
    shl        min_filter_length_x4d, 2
    mov                        fracd, [ctxq+ResampleContext.frac]
    neg        min_filter_length_x4q
    mov                 filter_bankq, [ctxq+ResampleContext.filter_bank]
    sub                         r2mp, min_filter_length_x4q
    sub                 filter_bankq, min_filter_length_x4q
    PUSH                              min_filter_length_x4q
    PUSH                              filter_bankq
    PUSH                              dword [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS src, filter1, dst, frac, index, min_filter_count_x4, filter2

%define phase_shift_stackd    dword [rsp+0x0]
%define filter_bankq          dword [rsp+0x4]
%define min_filter_length_x4q dword [rsp+0x8]
%define src_incrd             dword [rsp+0xc]
%define phase_mask_stackd     dword [rsp+0x10]
%define filter_alloc_x4q      dword [rsp+0x14]
%define filter_allocd         dword [rsp+0x18]
%define dst_incr_modd         dword [rsp+0x1c]
%define dst_endq              dword [rsp+0x20]
%define dst_incr_divd         dword [rsp+0x24]

    mov                         srcq, r2mp
%endif

.loop:
    mov                     filter1d, filter_allocd
    imul                    filter1d, indexd
%if ARCH_X86_64
    mov         min_filter_count_x4q, min_filter_len_x4q
    lea                     filter1q, [filter_bankq+filter1q*4]
    lea                     filter2q, [filter1q+filter_allocq*4]
%else ; x86-32
    mov         min_filter_count_x4q, filter_bankq
    lea                     filter1q, [min_filter_count_x4q+filter1q*4]
    mov         min_filter_count_x4q, min_filter_length_x4q
    mov                     filter2q, filter1q
    add                     filter2q, filter_alloc_x4q
%endif
    xorps                         m0, m0, m0
    xorps                         m2, m2, m2

    align 16
.inner_loop:
    movups                        m1, [srcq+min_filter_count_x4q*1]
    mulps                         m3, m1, [filter2q+min_filter_count_x4q*1]
    mulps                         m1, m1, [filter1q+min_filter_count_x4q*1]
    addps                         m2, m2, m3
    addps                         m0, m0, m1
    add         min_filter_count_x4q, mmsize
    js .inner_loop

%if cpuflag(avx)
    vextractf128                 xm1, m0, 0x1
    vextractf128                 xm3, m2, 0x1
    addps                        xm0, xm1
    addps                        xm2, xm3
%endif

    ; val += (v2 - val) * (FELEML) frac / c->src_incr;
    cvtsi2ss                     xm1, fracd
    subps                        xm2, xm0
    mulps                        xm1, xm4
    shufps                       xm1, xm1, q0000
    mulps                        xm2, xm1
    addps                        xm0, xm2

    ; horizontal sum & store
    movhlps                      xm1, xm0
    addps                        xm0, xm1
    shufps                       xm1, xm0, xm0, q0001
    add                        fracd, dst_incr_modd
    addps                        xm0, xm1
    add                       indexd, dst_incr_divd
    movss                     [dstq], xm0
    cmp                        fracd, src_incrd
    jl .skip
    sub                        fracd, src_incrd
    inc                       indexd

%if UNIX64
    DEFINE_ARGS filter_alloc, dst, src, phase_shift, index, frac, dst_incr_mod, \
                index_incr, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, filter2, dst_end, filter_bank
%elif WIN64
    DEFINE_ARGS phase_shift, dst, src, filter_alloc, index, frac, dst_incr_mod, \
                index_incr, min_filter_count_x4, min_filter_len_x4, dst_incr_div, \
                src_incr, filter2, dst_end, filter_bank
%else ; x86-32
    DEFINE_ARGS src, phase_shift, dst, frac, index, index_incr
%endif

.skip:
%if ARCH_X86_32
    mov                 phase_shiftd, phase_shift_stackd
%endif
    mov                  index_incrd, indexd
    add                         dstq, 4
    and                       indexd, phase_mask_stackd
    sar                  index_incrd, phase_shiftb
    lea                         srcq, [srcq+index_incrq*4]
    cmp                         dstq, dst_endq
    jne .loop

%if ARCH_X86_64
    DEFINE_ARGS ctx, dst, src, phase_shift, index, frac
%else ; x86-32
    DEFINE_ARGS src, ctx, update_context, frac, index
%endif

    cmp  dword update_context_stackd, 0
    jz .skip_store
    ; strictly speaking, the function should always return the consumed
    ; number of bytes; however, we only use the value if update_context
    ; is true, so let's just leave it uninitialized otherwise
    mov                         ctxq, ctx_stackq
    movifnidn                    rax, srcq
    mov [ctxq+ResampleContext.frac ], fracd
    sub                          rax, src_stackq
    mov [ctxq+ResampleContext.index], indexd
    shr                          rax, 2

.skip_store:
%if ARCH_X86_32
    ADD                          rsp, 0x28
%endif
    RET
%endmacro

INIT_XMM sse
RESAMPLE_FLOAT_FNS

%if HAVE_AVX_EXTERNAL
INIT_YMM avx
RESAMPLE_FLOAT_FNS
%endif
