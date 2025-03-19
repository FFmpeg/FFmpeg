;******************************************************************************
;* Copyright (c) 2025 Niklas Haas
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

; High-level explanation of how the x86 backend works:
;
; sws_processN is the shared entry point for all operation chains. This
; function is responsible for the block loop, as well as initializing the
; plane pointers. It will jump directly into the first operation kernel,
; and each operation kernel will jump directly into the next one, with the
; final kernel jumping back into the sws_process return point. (See label
; `sws_process.return` in ops_int.asm)
;
; To handle the jump back to the return point, we append an extra address
; corresponding to the correct sws_process.return label into the SwsOpChain,
; and have the WRITE kernel jump into it as usual. (See the FINISH macro)
;
; Inside an operation chain, we use a custom calling convention to preserve
; registers between kernels. The exact register allocation is found further
; below in this file, but we basically reserve (and share) the following
; registers:
;
; - const execq (read-only, shared execution data, see SwsOpExec); stores the
;   static metadata for this call and describes the image layouts
;
; - implq (read-only, operation chain, see SwsOpChain); stores the private data
;   for each operation as well as the pointer to the next kernel in the sequence.
;   This register is automatically incremented by the CONTINUE macro, and will
;   be reset back to the first operation kernel by sws_process.
;
; - bxd, yd: current line and block number, used as loop counters in sws_process.
;   Also used by e.g. the dithering code to do position-dependent dithering.
;
; - tmp0, tmp1: two temporary registers which are NOT preserved between kernels
;
; - inNq, outNq: plane pointers. These are incremented automatically after the
;   corresponding read/write operation, by the read/write kernels themselves.
;   sws_process will take care of resetting these to the next line after the
;   block loop is done.
;
; Additionally, we pass data between kernels by directly keeping them inside
; vector registers. For this, we reserve the following registers:
;
; - mx, my, mz, mw:     low half of the X, Y, Z and W components
; - mx2, my2, mz2, mw2: high half of the X, Y, Z and W components
; (As well as sized variants for xmx, ymx, etc.)
;
; The "high half" registers are only sometimes used; in order to enable
; processing more pixels at the same time. See `decl_v2` below, which allows
; assembling the same operation twice, once with only the lower half (V2=0),
; and once with both halves (V2=1). The remaining vectors are free for use
; inside operation kernels, starting from m8.
;
; The basic rule is that we always use the full set of both vector registers
; when processing the largest element size within a pixel chain. For example,
; if we load 8-bit values and convert them to 32-bit floats internally, then
; we would have an operation chain which combines an SSE4 V2=0 u8 kernel (128
; bits = 16 pixels) with an AVX2 V2=1 f32 kernel (512 bits = 16 pixels). This
; keeps the number of pixels being processed (the block size) constant. The
; V2 setting is suffixed to the operation name (_m1 or _m2) during name
; mangling.
;
; This design leaves us with the following set of possibilities:
;
; SSE4:
; - max element is 32-bit: currently unsupported
; - max element is 16-bit: currently unsupported
; - max element is 8-bit:  block size 32, u8_m2_sse4
;
; AVX2:
; - max element is 32-bit: block size 16, u32_m2_avx2, u16_m1_avx2, u8_m1_sse4
; - max element is 16-bit: block size 32, u16_m2_avx2, u8_m1_avx2
; - max element is 8-bit:  block size 64, u8_m2_avx2
;
; Meaning we need to cover the following code paths for each bit depth:
;
; -  8-bit kernels: m1_sse4, m2_sse4, m1_avx2, m2_avx2
; - 16-bit kernels: m1_avx2, m2_avx2
; - 32-bit kernels: m2_avx2
;
; This is achieved by macro'ing each operation kernel and declaring it once
; per SIMD version, and (if needed) once per V2 setting using decl_v2. (See
; the bottom of ops_int.asm for an example)
;
; Finally, we overload some operation kernel to different number of components,
; using the `decl_pattern` and `decl_common_patterns` macros. Inside these
; kernels, the variables X, Y, Z and W will be set to 0 or 1 respectively,
; depending on which components are active for this particular kernel instance.
; They will receive the _pXYZW prefix during name mangling.

struc SwsOpExec
    .in0 resq 1
    .in1 resq 1
    .in2 resq 1
    .in3 resq 1
    .out0 resq 1
    .out1 resq 1
    .out2 resq 1
    .out3 resq 1
    .in_stride0 resq 1
    .in_stride1 resq 1
    .in_stride2 resq 1
    .in_stride3 resq 1
    .out_stride0 resq 1
    .out_stride1 resq 1
    .out_stride2 resq 1
    .out_stride3 resq 1
    .in_bump0 resq 1
    .in_bump1 resq 1
    .in_bump2 resq 1
    .in_bump3 resq 1
    .out_bump0 resq 1
    .out_bump1 resq 1
    .out_bump2 resq 1
    .out_bump3 resq 1
    .width resd 1
    .height resd 1
    .slice_y resd 1
    .slice_h resd 1
    .block_size_in resd 1
    .block_size_out resd 1
endstruc

struc SwsOpImpl
    .cont resb 16
    .priv resb 16
    .next resb 0
endstruc

;---------------------------------------------------------
; Common macros for declaring operations

; Declare an operation kernel with the correct name mangling.
%macro op 1 ; name
    %ifdef X
        %define ADD_PAT(name) p %+ X %+ Y %+ Z %+ W %+ _ %+ name
    %else
        %define ADD_PAT(name) name
    %endif

    %ifdef V2
        %if V2
            %define ADD_MUL(name) name %+ _m2
        %else
            %define ADD_MUL(name) name %+ _m1
        %endif
    %else
        %define ADD_MUL(name) name
    %endif

    cglobal ADD_PAT(ADD_MUL(%1)), 0, 0, 0 ; already allocated by entry point

    %undef ADD_PAT
    %undef ADD_MUL
%endmacro

; Declare an operation kernel twice, once with V2=0 and once with V2=1
%macro decl_v2 2+ ; v2, func
    %xdefine V2 %1
    %2
    %undef V2
%endmacro

; Declare an operation kernel specialized to a given subset of active components
%macro decl_pattern 5+ ; X, Y, Z, W, func
    %xdefine X %1
    %xdefine Y %2
    %xdefine Z %3
    %xdefine W %4
    %5
    %undef X
    %undef Y
    %undef Z
    %undef W
%endmacro

; Declare an operation kernel specialized to each common component pattern
%macro decl_common_patterns 1+ ; func
    decl_pattern 1, 0, 0, 0, %1 ; y
    decl_pattern 1, 0, 0, 1, %1 ; ya
    decl_pattern 1, 1, 1, 0, %1 ; yuv
    decl_pattern 1, 1, 1, 1, %1 ; yuva
%endmacro

;---------------------------------------------------------
; Common names for the internal calling convention
%define mx      m0
%define my      m1
%define mz      m2
%define mw      m3

%define xmx     xm0
%define xmy     xm1
%define xmz     xm2
%define xmw     xm3

%define ymx     ym0
%define ymy     ym1
%define ymz     ym2
%define ymw     ym3

%define mx2     m4
%define my2     m5
%define mz2     m6
%define mw2     m7

%define xmx2    xm4
%define xmy2    xm5
%define xmz2    xm6
%define xmw2    xm7

%define ymx2    ym4
%define ymy2    ym5
%define ymz2    ym6
%define ymw2    ym7

; Reserved in this order by the signature of SwsOpFunc
%define execq   r0q
%define implq   r1q
%define bxd     r2d
%define yd      r3d

; Extra registers for free use by kernels, not saved between ops
%define tmp0q   r4q
%define tmp1q   r5q

%define tmp0d   r4d
%define tmp1d   r5d

; Registers for plane pointers; put at the end (and in ascending plane order)
; so that we can avoid reserving them when not necessary
%define out0q   r6q
%define  in0q   r7q
%define out1q   r8q
%define  in1q   r9q
%define out2q   r10q
%define  in2q   r11q
%define out3q   r12q
%define  in3q   r13q

;---------------------------------------------------------
; Common macros for linking together different kernels

; Load the next operation kernel's address to a register
%macro LOAD_CONT 1 ; reg
    mov %1, [implq + SwsOpImpl.cont]
%endmacro

; Tail call into the next operation kernel, given that kernel's address
%macro CONTINUE 1 ; reg
    add implq, SwsOpImpl.next
    jmp %1
    annotate_function_size
%endmacro

; Convenience macro to load and continue to the next kernel in one step
%macro CONTINUE 0
    LOAD_CONT tmp0q
    CONTINUE tmp0q
%endmacro

; Final macro to end the operation chain, used by WRITE kernels to jump back
; to the process function return point. Very similar to CONTINUE, but skips
; incrementing the implq pointer, and also clears AVX registers to avoid
; phantom dependencies between loop iterations.
%macro FINISH 1 ; reg
    %if vzeroupper_required
        ; we may jump back into an SSE read, so always zero upper regs here
        vzeroupper
    %endif
    jmp %1
    annotate_function_size
%endmacro

; Helper for inline conditionals; used to conditionally include single lines
%macro IF 2+ ; cond, body
    %if %1
        %2
    %endif
%endmacro

; Alternate name; for nested usage (to work around NASM limitations)
%macro IF1 2+
    %if %1
        %2
    %endif
%endmacro
