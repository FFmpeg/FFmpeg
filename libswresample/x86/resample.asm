;******************************************************************************
;* Copyright (c) 2012 Michael Niedermayer
;* Copyright (c) 2014 James Almer <jamrial <at> gmail.com>
;* Copyright (c) 2014 Ronald S. Bultje <rsbultje@gmail.com>
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

pf_1:      dd 1.0
pdbl_1:    dq 1.0
pd_0x4000: dd 0x4000

SECTION .text

%macro RESAMPLE_FNS 3-5 ; format [float or int16], bps, log2_bps, float op suffix [s or d], 1.0 constant
; int resample_common_$format(ResampleContext *ctx, $format *dst,
;                             const $format *src, int size, int update_ctx)
%if ARCH_X86_64 ; unix64 and win64
cglobal resample_common_%1, 0, 15, 2, ctx, dst, src, phase_shift, index, frac, \
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
    shl           min_filter_len_x4d, %3
    lea                     dst_endq, [dstq+sizeq*%2]

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
cglobal resample_common_%1, 1, 7, 2, ctx, phase_shift, dst, frac, \
                                     index, min_filter_length_x4, filter_bank

    ; push temp variables to stack
%define ctx_stackq            r0mp
%define src_stackq            r2mp
%define update_context_stackd r4m

    mov                         dstq, r1mp
    mov                           r3, r3mp
    lea                           r3, [dstq+r3*%2]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_div]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_mod]
    PUSH                              dword [ctxq+ResampleContext.filter_alloc]
    PUSH                              r3
    PUSH                              dword [ctxq+ResampleContext.phase_mask]
    PUSH                              dword [ctxq+ResampleContext.src_incr]
    mov        min_filter_length_x4d, [ctxq+ResampleContext.filter_length]
    mov                       indexd, [ctxq+ResampleContext.index]
    shl        min_filter_length_x4d, %3
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
    lea                      filterq, [filter_bankq+filterq*%2]
%else ; x86-32
    mov         min_filter_count_x4q, filter_bankq
    lea                      filterq, [min_filter_count_x4q+filterq*%2]
    mov         min_filter_count_x4q, min_filter_length_x4q
%endif
%ifidn %1, int16
    movd                          m0, [pd_0x4000]
%else ; float/double
    xorps                         m0, m0, m0
%endif

    align 16
.inner_loop:
    movu                          m1, [srcq+min_filter_count_x4q*1]
%ifidn %1, int16
    PMADCSWD                      m0, m1, [filterq+min_filter_count_x4q*1], m0, m1
%else ; float/double
%if cpuflag(fma4) || cpuflag(fma3)
    fmaddp%4                      m0, m1, [filterq+min_filter_count_x4q*1], m0
%else
    mulp%4                        m1, m1, [filterq+min_filter_count_x4q*1]
    addp%4                        m0, m0, m1
%endif ; cpuflag
%endif
    add         min_filter_count_x4q, mmsize
    js .inner_loop

%ifidn %1, int16
    HADDD                         m0, m1
    psrad                         m0, 15
    add                        fracd, dst_incr_modd
    packssdw                      m0, m0
    add                       indexd, dst_incr_divd
    movd                      [dstq], m0
%else ; float/double
    ; horizontal sum & store
%if mmsize == 32
    vextractf128                 xm1, m0, 0x1
    addps                        xm0, xm1
%endif
    movhlps                      xm1, xm0
%ifidn %1, float
    addps                        xm0, xm1
    shufps                       xm1, xm0, xm0, q0001
%endif
    add                        fracd, dst_incr_modd
    addp%4                       xm0, xm1
    add                       indexd, dst_incr_divd
    movs%4                    [dstq], xm0
%endif
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
    add                         dstq, %2
    and                       indexd, phase_maskd
    sar                  index_incrd, phase_shiftb
    lea                         srcq, [srcq+index_incrq*%2]
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
    shr                          rax, %3

.skip_store:
%if ARCH_X86_32
    ADD                          rsp, 0x20
%endif
    RET

; int resample_linear_$format(ResampleContext *ctx, float *dst,
;                             const float *src, int size, int update_ctx)
%if ARCH_X86_64 ; unix64 and win64
%if UNIX64
cglobal resample_linear_%1, 0, 15, 5, ctx, dst, phase_mask, phase_shift, index, frac, \
                                      size, dst_incr_mod, min_filter_count_x4, \
                                      min_filter_len_x4, dst_incr_div, src_incr, \
                                      src, dst_end, filter_bank

    mov                         srcq, r2mp
%else ; win64
cglobal resample_linear_%1, 0, 15, 5, ctx, phase_mask, src, phase_shift, index, frac, \
                                      size, dst_incr_mod, min_filter_count_x4, \
                                      min_filter_len_x4, dst_incr_div, src_incr, \
                                      dst, dst_end, filter_bank

    mov                         dstq, r1mp
%endif

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
%ifidn %1, int16
    movd                          m4, [pd_0x4000]
%else ; float/double
    cvtsi2s%4                    xm0, src_incrd
    movs%4                       xm4, [%5]
    divs%4                       xm4, xm0
%endif
    mov                dst_incr_divd, [ctxq+ResampleContext.dst_incr_div]
    shl           min_filter_len_x4d, %3
    lea                     dst_endq, [dstq+sizeq*%2]

%if UNIX64
    mov                          ecx, [ctxq+ResampleContext.phase_shift]
    mov                          edi, [ctxq+ResampleContext.filter_alloc]

    DEFINE_ARGS filter_alloc, dst, filter2, phase_shift, index, frac, filter1, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, src, dst_end, filter_bank
%elif WIN64
    mov                          R9d, [ctxq+ResampleContext.filter_alloc]
    mov                          ecx, [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS phase_shift, filter2, src, filter_alloc, index, frac, filter1, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, dst, dst_end, filter_bank
%endif

    neg           min_filter_len_x4q
    sub                 filter_bankq, min_filter_len_x4q
    sub                         srcq, min_filter_len_x4q
    mov                   src_stackq, srcq
%else ; x86-32
cglobal resample_linear_%1, 1, 7, 5, ctx, min_filter_length_x4, filter2, \
                                     frac, index, dst, filter_bank

    ; push temp variables to stack
%define ctx_stackq            r0mp
%define src_stackq            r2mp
%define update_context_stackd r4m

    mov                         dstq, r1mp
    mov                           r3, r3mp
    lea                           r3, [dstq+r3*%2]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_div]
    PUSH                              r3
    mov                           r3, dword [ctxq+ResampleContext.filter_alloc]
    PUSH                              dword [ctxq+ResampleContext.dst_incr_mod]
    PUSH                              r3
    shl                           r3, %3
    PUSH                              r3
    mov                           r3, dword [ctxq+ResampleContext.src_incr]
    PUSH                              dword [ctxq+ResampleContext.phase_mask]
    PUSH                              r3d
%ifidn %1, int16
    movd                          m4, [pd_0x4000]
%else ; float/double
    cvtsi2s%4                    xm0, r3d
    movs%4                       xm4, [%5]
    divs%4                       xm4, xm0
%endif
    mov        min_filter_length_x4d, [ctxq+ResampleContext.filter_length]
    mov                       indexd, [ctxq+ResampleContext.index]
    shl        min_filter_length_x4d, %3
    mov                        fracd, [ctxq+ResampleContext.frac]
    neg        min_filter_length_x4q
    mov                 filter_bankq, [ctxq+ResampleContext.filter_bank]
    sub                         r2mp, min_filter_length_x4q
    sub                 filter_bankq, min_filter_length_x4q
    PUSH                              min_filter_length_x4q
    PUSH                              filter_bankq
    PUSH                              dword [ctxq+ResampleContext.phase_shift]

    DEFINE_ARGS filter1, min_filter_count_x4, filter2, frac, index, dst, src

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
    lea                     filter1q, [filter_bankq+filter1q*%2]
    lea                     filter2q, [filter1q+filter_allocq*%2]
%else ; x86-32
    mov         min_filter_count_x4q, filter_bankq
    lea                     filter1q, [min_filter_count_x4q+filter1q*%2]
    mov         min_filter_count_x4q, min_filter_length_x4q
    mov                     filter2q, filter1q
    add                     filter2q, filter_alloc_x4q
%endif
%ifidn %1, int16
    mova                          m0, m4
    mova                          m2, m4
%else ; float/double
    xorps                         m0, m0, m0
    xorps                         m2, m2, m2
%endif

    align 16
.inner_loop:
    movu                          m1, [srcq+min_filter_count_x4q*1]
%ifidn %1, int16
%if cpuflag(xop)
    vpmadcswd                     m2, m1, [filter2q+min_filter_count_x4q*1], m2
    vpmadcswd                     m0, m1, [filter1q+min_filter_count_x4q*1], m0
%else
    pmaddwd                       m3, m1, [filter2q+min_filter_count_x4q*1]
    pmaddwd                       m1, [filter1q+min_filter_count_x4q*1]
    paddd                         m2, m3
    paddd                         m0, m1
%endif ; cpuflag
%else ; float/double
%if cpuflag(fma4) || cpuflag(fma3)
    fmaddp%4                      m2, m1, [filter2q+min_filter_count_x4q*1], m2
    fmaddp%4                      m0, m1, [filter1q+min_filter_count_x4q*1], m0
%else
    mulp%4                        m3, m1, [filter2q+min_filter_count_x4q*1]
    mulp%4                        m1, m1, [filter1q+min_filter_count_x4q*1]
    addp%4                        m2, m2, m3
    addp%4                        m0, m0, m1
%endif ; cpuflag
%endif
    add         min_filter_count_x4q, mmsize
    js .inner_loop

%ifidn %1, int16
%if mmsize == 16
%if cpuflag(xop)
    vphadddq                      m2, m2
    vphadddq                      m0, m0
%endif
    pshufd                        m3, m2, q0032
    pshufd                        m1, m0, q0032
    paddd                         m2, m3
    paddd                         m0, m1
%endif
%if notcpuflag(xop)
    PSHUFLW                       m3, m2, q0032
    PSHUFLW                       m1, m0, q0032
    paddd                         m2, m3
    paddd                         m0, m1
%endif
    psubd                         m2, m0
    ; This is probably a really bad idea on atom and other machines with a
    ; long transfer latency between GPRs and XMMs (atom). However, it does
    ; make the clip a lot simpler...
    movd                         eax, m2
    add                       indexd, dst_incr_divd
    imul                              fracd
    idiv                              src_incrd
    movd                          m1, eax
    add                        fracd, dst_incr_modd
    paddd                         m0, m1
    psrad                         m0, 15
    packssdw                      m0, m0
    movd                      [dstq], m0

    ; note that for imul/idiv, I need to move filter to edx/eax for each:
    ; - 32bit: eax=r0[filter1], edx=r2[filter2]
    ; - win64: eax=r6[filter1], edx=r1[todo]
    ; - unix64: eax=r6[filter1], edx=r2[todo]
%else ; float/double
    ; val += (v2 - val) * (FELEML) frac / c->src_incr;
%if mmsize == 32
    vextractf128                 xm1, m0, 0x1
    vextractf128                 xm3, m2, 0x1
    addps                        xm0, xm1
    addps                        xm2, xm3
%endif
    cvtsi2s%4                    xm1, fracd
    subp%4                       xm2, xm0
    mulp%4                       xm1, xm4
    shufp%4                      xm1, xm1, q0000
%if cpuflag(fma4) || cpuflag(fma3)
    fmaddp%4                     xm0, xm2, xm1, xm0
%else
    mulp%4                       xm2, xm1
    addp%4                       xm0, xm2
%endif ; cpuflag

    ; horizontal sum & store
    movhlps                      xm1, xm0
%ifidn %1, float
    addps                        xm0, xm1
    shufps                       xm1, xm0, xm0, q0001
%endif
    add                        fracd, dst_incr_modd
    addp%4                       xm0, xm1
    add                       indexd, dst_incr_divd
    movs%4                    [dstq], xm0
%endif
    cmp                        fracd, src_incrd
    jl .skip
    sub                        fracd, src_incrd
    inc                       indexd

%if UNIX64
    DEFINE_ARGS filter_alloc, dst, filter2, phase_shift, index, frac, index_incr, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, src, dst_end, filter_bank
%elif WIN64
    DEFINE_ARGS phase_shift, filter2, src, filter_alloc, index, frac, index_incr, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, dst, dst_end, filter_bank
%else ; x86-32
    DEFINE_ARGS filter1, phase_shift, index_incr, frac, index, dst, src
%endif

.skip:
%if ARCH_X86_32
    mov                 phase_shiftd, phase_shift_stackd
%endif
    mov                  index_incrd, indexd
    add                         dstq, %2
    and                       indexd, phase_mask_stackd
    sar                  index_incrd, phase_shiftb
    lea                         srcq, [srcq+index_incrq*%2]
    cmp                         dstq, dst_endq
    jne .loop

%if UNIX64
    DEFINE_ARGS ctx, dst, filter2, phase_shift, index, frac, index_incr, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, src, dst_end, filter_bank
%elif WIN64
    DEFINE_ARGS ctx, filter2, src, phase_shift, index, frac, index_incr, \
                dst_incr_mod, min_filter_count_x4, min_filter_len_x4, \
                dst_incr_div, src_incr, dst, dst_end, filter_bank
%else ; x86-32
    DEFINE_ARGS filter1, ctx, update_context, frac, index, dst, src
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
    shr                          rax, %3

.skip_store:
%if ARCH_X86_32
    ADD                          rsp, 0x28
%endif
    RET
%endmacro

INIT_XMM sse
RESAMPLE_FNS float, 4, 2, s, pf_1

%if HAVE_AVX_EXTERNAL
INIT_YMM avx
RESAMPLE_FNS float, 4, 2, s, pf_1
%endif
%if HAVE_FMA3_EXTERNAL
INIT_YMM fma3
RESAMPLE_FNS float, 4, 2, s, pf_1
%endif
%if HAVE_FMA4_EXTERNAL
INIT_XMM fma4
RESAMPLE_FNS float, 4, 2, s, pf_1
%endif

%if ARCH_X86_32
INIT_MMX mmxext
RESAMPLE_FNS int16, 2, 1
%endif

INIT_XMM sse2
RESAMPLE_FNS int16, 2, 1
%if HAVE_XOP_EXTERNAL
INIT_XMM xop
RESAMPLE_FNS int16, 2, 1
%endif

INIT_XMM sse2
RESAMPLE_FNS double, 8, 3, d, pdbl_1
