;******************************************************************************
;* SIMD optimized Opus encoder DSP function
;*
;* Copyright (C) 2017 Ivan Kalvachev <ikalvachev@gmail.com>
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

%include "config.asm"
%include "libavutil/x86/x86util.asm"

%ifdef __NASM_VER__
%use "smartalign"
ALIGNMODE p6
%endif

SECTION_RODATA 64

const_float_abs_mask:   times 8 dd 0x7fffffff
const_align_abs_edge:   times 8 dd 0

const_float_0_5:        times 8 dd 0.5
const_float_1:          times 8 dd 1.0
const_float_sign_mask:  times 8 dd 0x80000000

const_int32_offsets:
                        %rep 8
                                dd $-const_int32_offsets
                        %endrep
SECTION .text

;
;   Setup High Register to be used
;   for holding memory constants
;
; %1 - the register to be used, assmues it is >= mm8
; %2 - name of the constant.
;
; Subsequent opcodes are going to use the constant in the form
; "addps m0, mm_const_name" and it would be turned into:
; "addps m0, [const_name]" on 32 bit arch or
; "addps m0, m8" on 64 bit arch
%macro SET_HI_REG_MM_CONSTANT 3 ; movop, reg, const_name
%if num_mmregs > 8
    %define  mm_%3   %2
    %{1}        %2, [%3]    ; movaps m8, [const_name]
%else
    %define  mm_%3  [%3]
%endif
%endmacro

;
;   Set Position Independent Code
;       Base address of a constant
; %1 - the register to be used, if PIC is set
; %2 - name of the constant.
;
; Subsequent opcode are going to use the base address in the form
; "movaps m0, [pic_base_constant_name+r4]" and it would be turned into
; "movaps m0, [r5 + r4]" if PIC is enabled
; "movaps m0, [constant_name + r4]" if texrel are used
%macro SET_PIC_BASE 3; reg, const_label
%ifdef PIC
    %{1}     %2, [%3]      ; lea r5, [rip+const]
    %define  pic_base_%3 %2
%else
    %define  pic_base_%3 %3
%endif
%endmacro

%macro PULSES_SEARCH 1
; m6 Syy_norm
; m7 Sxy_norm
    addps          m6, mm_const_float_0_5   ; Syy_norm += 1.0/2
    pxor           m1, m1                   ; max_idx
    xorps          m3, m3                   ; p_max
    xor           r4d, r4d
align 16
%%distortion_search:
    movd          xm2, dword r4d    ; movd zero extends
%ifidn %1,add
    movaps         m4, [tmpY + r4]  ; y[i]
    movaps         m5, [tmpX + r4]  ; X[i]

  %if USE_APPROXIMATION == 1
    xorps          m0, m0
    cmpps          m0, m0, m5, 4    ; m0 = (X[i] != 0.0)
  %endif

    addps          m4, m6           ; m4 = Syy_new = y[i] + Syy_norm
    addps          m5, m7           ; m5 = Sxy_new = X[i] + Sxy_norm

  %if USE_APPROXIMATION == 1
    andps          m5, m0           ; if(X[i] == 0) Sxy_new = 0; Prevent aproximation error from setting pulses in array padding.
  %endif

%else
    movaps         m5, [tmpY + r4]      ; m5 = y[i]

    xorps          m0, m0               ; m0 = 0;
    cmpps          m0, m0, m5, 1        ; m0 = (0<y)

    subps          m4, m6, m5           ; m4 = Syy_new = Syy_norm - y[i]
    subps          m5, m7, [tmpX + r4]  ; m5 = Sxy_new = Sxy_norm - X[i]
    andps          m5, m0               ; (0<y)?m5:0
%endif

%if USE_APPROXIMATION == 1
    rsqrtps        m4, m4
    mulps          m5, m4           ; m5 = p = Sxy_new*approx(1/sqrt(Syy) )
%else
    mulps          m5, m5
    divps          m5, m4           ; m5 = p = Sxy_new*Sxy_new/Syy
%endif
    VPBROADCASTD   m2, xm2          ; m2=i (all lanes get same values, we add the offset-per-lane, later)

    cmpps          m0, m3, m5, 1    ; m0 = (m3 < m5) ; (p_max < p) ; (p > p_max)
    maxps          m3, m5           ; m3=max(p_max,p)
                                    ; maxps here is faster than blendvps, despite blend having lower latency.

    pand           m2, m0           ; This version seems faster than sse41 pblendvb
    pmaxsw         m1, m2           ; SSE2 signed word, so it would work for N < 32768/4

    add           r4d, mmsize
    cmp           r4d, Nd
    jb   %%distortion_search

    por            m1, mm_const_int32_offsets  ; max_idx offsets per individual lane (skipped in the inner loop)
    movdqa         m4, m1                      ; needed for the aligned y[max_idx]+=1; processing

%if mmsize >= 32
; Merge parallel maximums round 8 (4 vs 4)

    vextractf128  xm5, ym3, 1       ; xmm5 = ymm3[1x128] = ymm3[255..128b]
    cmpps         xm0, xm3, xm5, 1  ; m0 = (m3 < m5) = ( p[0x128] < p[1x128] )

    vextracti128  xm2, ym1, 1       ; xmm2 = ymm1[1x128] = ymm1[255..128b]
    BLENDVPS      xm3, xm5, xm0     ; max_idx = m0 ? max_idx[1x128] : max_idx[0x128]
    PBLENDVB      xm1, xm2, xm0     ; p       = m0 ? p[1x128]       : p[0x128]
%endif

; Merge parallel maximums round 4 (2 vs 2)
                                    ; m3=p[3210]
    movhlps       xm5, xm3          ; m5=p[xx32]
    cmpps         xm0, xm3, xm5, 1  ; m0 = (m3 < m5) = ( p[1,0] < p[3,2] )

    pshufd        xm2, xm1, q3232
    BLENDVPS      xm3, xm5, xm0     ; max_idx = m0 ? max_idx[3,2] : max_idx[1,0]
    PBLENDVB      xm1, xm2, xm0     ; p       = m0 ? p[3,2]       : p[1,0]

; Merge parallel maximums final round (1 vs 1)
    shufps        xm0, xm3, xm3, q1111  ; m0 = m3[1] = p[1]
    cmpss         xm0, xm3, 5           ; m0 = !(m0 >= m3) = !( p[1] >= p[0] )

    pshufd        xm2, xm1, q1111
    PBLENDVB      xm1, xm2, xm0

    movd    dword r4d, xm1          ; zero extends to the rest of r4q

    VBROADCASTSS   m3, [tmpX + r4]
    %{1}ps         m7, m3           ; Sxy += X[max_idx]

    VBROADCASTSS   m5, [tmpY + r4]
    %{1}ps         m6, m5           ; Syy += Y[max_idx]

    ; We have to update a single element in Y[i]
    ; However writing 4 bytes and then doing 16 byte load in the inner loop
    ; could cause a stall due to breaking write forwarding.
    VPBROADCASTD   m1, xm1
    pcmpeqd        m1, m1, m4           ; exactly 1 element matches max_idx and this finds it

    and           r4d, ~(mmsize-1)      ; align address down, so the value pointed by max_idx is inside a mmsize load
    movaps         m5, [tmpY + r4]      ; m5 = Y[y3...ym...y0]
    andps          m1, mm_const_float_1 ; m1 =  [ 0...1.0...0]
    %{1}ps         m5, m1               ; m5 = Y[y3...ym...y0] +/- [0...1.0...0]
    movaps [tmpY + r4], m5              ; Y[max_idx] +-= 1.0;
%endmacro

;
; We need one more register for
; PIC relative addressing. Use this
; to count it in cglobal
;
%ifdef PIC
  %define num_pic_regs 1
%else
  %define num_pic_regs 0
%endif

;
; Pyramid Vector Quantization Search implementation
;
; float * inX   - Unaligned (SIMD) access, it will be overread,
;                 but extra data is masked away.
; int32 * outY  - Should be aligned and padded buffer.
;                 It is used as temp buffer.
; uint32 K      - Number of pulses to have after quantizations.
; uint32 N      - Number of vector elements. Must be 0 < N < 256
;
%macro PVQ_FAST_SEARCH 1
cglobal pvq_search%1, 4, 5+num_pic_regs, 11, 256*4, inX, outY, K, N
%define tmpX rsp
%define tmpY outYq

    movaps     m0, [const_float_abs_mask]
    shl        Nd, 2    ; N *= sizeof(float); also 32 bit operation zeroes the high 32 bits in 64 bit mode.
    mov       r4d, Nd

    neg       r4d
    and       r4d, mmsize-1

    SET_PIC_BASE lea, r5, const_align_abs_edge  ; rip+const
    movups     m2, [pic_base_const_align_abs_edge + r4 - mmsize]

    add        Nd, r4d              ; N = align(N, mmsize)

    lea       r4d, [Nd - mmsize]    ; N is rounded up (aligned up) to mmsize, so r4 can't become negative here, unless N=0.
    movups     m1, [inXq + r4]
    andps      m1, m2
    movaps  [tmpX + r4], m1         ; Sx = abs( X[N-1] )

align 16
%%loop_abs_sum:
    sub       r4d, mmsize
    jc   %%end_loop_abs_sum

    movups     m2, [inXq + r4]
    andps      m2, m0

    movaps  [tmpX + r4], m2 ; tmpX[i]=abs(X[i])
    addps      m1, m2       ; Sx += abs(X[i])
    jmp  %%loop_abs_sum

align 16
%%end_loop_abs_sum:

    HSUMPS     m1, m2       ; m1  = Sx

    xorps      m0, m0
    comiss    xm0, xm1      ;
    jz   %%zero_input       ; if (Sx==0) goto zero_input

    cvtsi2ss  xm0, dword Kd ; m0 = K
%if USE_APPROXIMATION == 1
    rcpss     xm1, xm1      ; m1 = approx(1/Sx)
    mulss     xm0, xm1      ; m0 = K*(1/Sx)
%else
    divss     xm0, xm1      ; b = K/Sx
                            ; b = K/max_x
%endif

    VBROADCASTSS  m0, xm0

    lea       r4d, [Nd - mmsize]
    pxor       m5, m5             ; Sy    ( Sum of abs( y[i]) )
    xorps      m6, m6             ; Syy   ( Sum of y[i]*y[i]  )
    xorps      m7, m7             ; Sxy   ( Sum of X[i]*y[i]  )
align 16
%%loop_guess:
    movaps     m1, [tmpX + r4]    ; m1   = X[i]
    mulps      m2, m0, m1         ; m2   = res*X[i]
    cvtps2dq   m2, m2             ; yt   = (int)lrintf( res*X[i] )
    paddd      m5, m2             ; Sy  += yt
    cvtdq2ps   m2, m2             ; yt   = (float)yt
    mulps      m1, m2             ; m1   = X[i]*yt
    movaps  [tmpY + r4], m2       ; y[i] = m2
    addps      m7, m1             ; Sxy += m1;
    mulps      m2, m2             ; m2   = yt*yt
    addps      m6, m2             ; Syy += m2

    sub       r4d, mmsize
    jnc  %%loop_guess

    HSUMPS     m6, m1       ; Syy_norm
    HADDD      m5, m4       ; pulses

    movd  dword r4d, xm5    ; zero extends to the rest of r4q

    sub        Kd, r4d      ; K -= pulses , also 32 bit operation zeroes high 32 bit in 64 bit mode.
    jz   %%finish           ; K - pulses == 0

    SET_HI_REG_MM_CONSTANT movaps,  m8, const_float_0_5
    SET_HI_REG_MM_CONSTANT movaps,  m9, const_float_1
    SET_HI_REG_MM_CONSTANT movdqa, m10, const_int32_offsets
    ; Use Syy/2 in distortion parameter calculations.
    ; Saves pre and post-caclulation to correct Y[] values.
    ; Same precision, since float mantisa is normalized.
    ; The SQRT approximation does differ.
    HSUMPS     m7, m0         ; Sxy_norm
    mulps      m6, mm_const_float_0_5

    jc   %%remove_pulses_loop   ; K - pulses < 0

align 16                        ; K - pulses > 0
%%add_pulses_loop:

    PULSES_SEARCH add   ; m6 Syy_norm ; m7 Sxy_norm

    sub        Kd, 1
    jnz  %%add_pulses_loop

    addps      m6, m6 ; Syy*=2

    jmp  %%finish

align 16
%%remove_pulses_loop:

    PULSES_SEARCH sub   ; m6 Syy_norm ; m7 Sxy_norm

    add        Kd, 1
    jnz  %%remove_pulses_loop

    addps      m6, m6 ; Syy*=2

align 16
%%finish:
    lea       r4d, [Nd - mmsize]
    movaps     m2, [const_float_sign_mask]

align 16
%%restore_sign_loop:
    movaps     m0, [tmpY + r4]    ; m0 = Y[i]
    movups     m1, [inXq + r4]    ; m1 = X[i]
    andps      m1, m2             ; m1 = sign(X[i])
    orps       m0, m1             ; m0 = Y[i]*sign
    cvtps2dq   m3, m0             ; m3 = (int)m0
    movaps  [outYq + r4], m3

    sub       r4d, mmsize
    jnc  %%restore_sign_loop
%%return:

%if ARCH_X86_64 == 0    ; sbrdsp
    movss     r0m, xm6  ; return (float)Syy_norm
    fld dword r0m
%else
    movaps     m0, m6   ; return (float)Syy_norm
%endif

    RET

align 16
%%zero_input:
    lea       r4d, [Nd - mmsize]
    xorps      m0, m0
%%zero_loop:
    movaps  [outYq + r4], m0
    sub       r4d, mmsize
    jnc  %%zero_loop

    movaps     m6, [const_float_1]
    jmp  %%return
%endmacro

; if 1, use a float op that give half precision but execute for around 3 cycles.
; On Skylake & Ryzen the division is much faster (around 11c/3),
; that makes the full precision code about 2% slower.
; Opus also does use rsqrt approximation in their intrinsics code.
%define USE_APPROXIMATION   1

INIT_XMM sse2
PVQ_FAST_SEARCH _approx

INIT_XMM sse4
PVQ_FAST_SEARCH _approx

%define USE_APPROXIMATION   0

INIT_XMM avx
PVQ_FAST_SEARCH _exact
