;*****************************************************************************
;* x86inc.asm
;*****************************************************************************
;* Copyright (C) 2005-2008 Loren Merritt <lorenm@u.washington.edu>
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;*****************************************************************************

; FIXME: All of the 64bit asm functions that take a stride as an argument
; via register, assume that the high dword of that register is filled with 0.
; This is true in practice (since we never do any 64bit arithmetic on strides,
; and x264's strides are all positive), but is not guaranteed by the ABI.

; Name of the .rodata section.
; Kludge: Something on OS X fails to align .rodata even given an align attribute,
; so use a different read-only section.
%macro SECTION_RODATA 0
    %ifidn __OUTPUT_FORMAT__,macho64
        SECTION .text align=16
    %elifidn __OUTPUT_FORMAT__,macho
        SECTION .text align=16
        fakegot:
    %else
        SECTION .rodata align=16
    %endif
%endmacro

; PIC support macros. All these macros are totally harmless when PIC is
; not defined but can ruin everything if misused in PIC mode. On x86_32, shared
; objects cannot directly access global variables by address, they need to
; go through the GOT (global offset table). Most OSes do not care about it
; and let you load non-shared .so objects (Linux, Win32...). However, OS X
; requires PIC code in its .dylib objects.
;
; - GLOBAL should be used as a suffix for global addressing, eg.
;     picgetgot ebx
;     mov eax, [foo GLOBAL]
;   instead of
;     mov eax, [foo]
;
; - picgetgot computes the GOT address into the given register in PIC
;   mode, otherwise does nothing. You need to do this before using GLOBAL.
;   Before in both execution order and compiled code order (so GLOBAL knows
;   which register the GOT is in).

%ifndef PIC
    %define GLOBAL
    %macro picgetgot 1
    %endmacro
%elifdef ARCH_X86_64
    %define PIC64
    %define GLOBAL wrt rip
    %macro picgetgot 1
    %endmacro
%else
    %define PIC32
    %ifidn __OUTPUT_FORMAT__,macho
        ; There is no real global offset table on OS X, but we still
        ; need to reference our variables by offset.
        %macro picgetgot 1
            call %%getgot
          %%getgot:
            pop %1
            add %1, $$ - %%getgot
            %undef GLOBAL
            %define GLOBAL + %1 - fakegot
        %endmacro
    %else ; elf
        extern _GLOBAL_OFFSET_TABLE_
        %macro picgetgot 1
            call %%getgot
          %%getgot:
            pop %1
            add %1, _GLOBAL_OFFSET_TABLE_ + $$ - %%getgot wrt ..gotpc
            %undef GLOBAL
            %define GLOBAL + %1 wrt ..gotoff
        %endmacro
    %endif
%endif

; Macros to eliminate most code duplication between x86_32 and x86_64:
; Currently this works only for leaf functions which load all their arguments
; into registers at the start, and make no other use of the stack. Luckily that
; covers most of x264's asm.

; PROLOGUE:
; %1 = number of arguments. loads them from stack if needed.
; %2 = number of registers used, not including PIC. pushes callee-saved regs if needed.
; %3 = whether global constants are used in this function. inits x86_32 PIC if needed.
; %4 = list of names to define to registers
; PROLOGUE can also be invoked by adding the same options to cglobal

; e.g.
; cglobal foo, 2,3,0, dst, src, tmp
; declares a function (foo), taking two args (dst and src), one local variable (tmp), and not using globals

; TODO Some functions can use some args directly from the stack. If they're the
; last args then you can just not declare them, but if they're in the middle
; we need more flexible macro.

; RET:
; Pops anything that was pushed by PROLOGUE

; REP_RET:
; Same, but if it doesn't pop anything it becomes a 2-byte ret, for athlons
; which are slow when a normal ret follows a branch.

%macro DECLARE_REG 6
    %define r%1q %2
    %define r%1d %3
    %define r%1w %4
    %define r%1b %5
    %define r%1m %6
    %define r%1  %2
%endmacro

%macro DECLARE_REG_SIZE 2
    %define r%1q r%1
    %define e%1q r%1
    %define r%1d e%1
    %define e%1d e%1
    %define r%1w %1
    %define e%1w %1
    %define r%1b %2
    %define e%1b %2
%ifndef ARCH_X86_64
    %define r%1  e%1
%endif
%endmacro

DECLARE_REG_SIZE ax, al
DECLARE_REG_SIZE bx, bl
DECLARE_REG_SIZE cx, cl
DECLARE_REG_SIZE dx, dl
DECLARE_REG_SIZE si, sil
DECLARE_REG_SIZE di, dil
DECLARE_REG_SIZE bp, bpl

%ifdef ARCH_X86_64
    %define gprsize 8
%else
    %define gprsize 4
%endif

%macro PUSH 1
    push %1
    %assign stack_offset stack_offset+gprsize
%endmacro

%macro POP 1
    pop %1
    %assign stack_offset stack_offset-gprsize
%endmacro

%macro SUB 2
    sub %1, %2
    %ifidn %1, rsp
        %assign stack_offset stack_offset+(%2)
    %endif
%endmacro

%macro ADD 2
    add %1, %2
    %ifidn %1, rsp
        %assign stack_offset stack_offset-(%2)
    %endif
%endmacro

%macro movifnidn 2
    %ifnidn %1, %2
        mov %1, %2
    %endif
%endmacro

%macro movsxdifnidn 2
    %ifnidn %1, %2
        movsxd %1, %2
    %endif
%endmacro

%macro ASSERT 1
    %if (%1) == 0
        %error assert failed
    %endif
%endmacro

%macro DEFINE_ARGS 0-*
    %ifdef n_arg_names
        %assign %%i 0
        %rep n_arg_names
            CAT_UNDEF arg_name %+ %%i, q
            CAT_UNDEF arg_name %+ %%i, d
            CAT_UNDEF arg_name %+ %%i, w
            CAT_UNDEF arg_name %+ %%i, b
            CAT_UNDEF arg_name, %%i
            %assign %%i %%i+1
        %endrep
    %endif

    %assign %%i 0
    %rep %0
        %xdefine %1q r %+ %%i %+ q
        %xdefine %1d r %+ %%i %+ d
        %xdefine %1w r %+ %%i %+ w
        %xdefine %1b r %+ %%i %+ b
        CAT_XDEFINE arg_name, %%i, %1
        %assign %%i %%i+1
        %rotate 1
    %endrep
    %assign n_arg_names %%i
%endmacro

%ifdef ARCH_X86_64 ;==========================================================
%ifidn __OUTPUT_FORMAT__,win32

DECLARE_REG 0, rcx, ecx, cx,  cl,  ecx
DECLARE_REG 1, rdx, edx, dx,  dl,  edx
DECLARE_REG 2, r8,  r8d, r8w, r8b, r8d
DECLARE_REG 3, r9,  r9d, r9w, r9b, r9d
DECLARE_REG 4, rdi, edi, di,  dil, [rsp + stack_offset + 40]
DECLARE_REG 5, rsi, esi, si,  sil, [rsp + stack_offset + 48]
DECLARE_REG 6, rax, eax, ax,  al,  [rsp + stack_offset + 56]
%define r7m [rsp + stack_offset + 64]
%define r8m [rsp + stack_offset + 72]

%macro LOAD_IF_USED 2 ; reg_id, number_of_args
    %if %1 < %2
        mov r%1, [rsp + 8 + %1*8]
    %endif
%endmacro

%else ;=======================================================================

DECLARE_REG 0, rdi, edi, di,  dil, edi
DECLARE_REG 1, rsi, esi, si,  sil, esi
DECLARE_REG 2, rdx, edx, dx,  dl,  edx
DECLARE_REG 3, rcx, ecx, cx,  cl,  ecx
DECLARE_REG 4, r8,  r8d, r8w, r8b, r8d
DECLARE_REG 5, r9,  r9d, r9w, r9b, r9d
DECLARE_REG 6, rax, eax, ax,  al,  [rsp + stack_offset + 8]
%define r7m [rsp + stack_offset + 16]
%define r8m [rsp + stack_offset + 24]

%macro LOAD_IF_USED 2 ; reg_id, number_of_args
    %if %1 < %2
        mov r%1, [rsp - 40 + %1*8]
    %endif
%endmacro

%endif ; !WIN64

%macro PROLOGUE 2-4+ 0 ; #args, #regs, pic, arg_names...
    ASSERT %2 >= %1
    ASSERT %2 <= 7
    %assign stack_offset 0
%ifidn __OUTPUT_FORMAT__,win32
    LOAD_IF_USED 4, %1
    LOAD_IF_USED 5, %1
%endif
    LOAD_IF_USED 6, %1
    DEFINE_ARGS %4
%endmacro

%macro RET 0
    ret
%endmacro

%macro REP_RET 0
    rep ret
%endmacro

%else ; X86_32 ;==============================================================

DECLARE_REG 0, eax, eax, ax, al,   [esp + stack_offset + 4]
DECLARE_REG 1, ecx, ecx, cx, cl,   [esp + stack_offset + 8]
DECLARE_REG 2, edx, edx, dx, dl,   [esp + stack_offset + 12]
DECLARE_REG 3, ebx, ebx, bx, bl,   [esp + stack_offset + 16]
DECLARE_REG 4, esi, esi, si, null, [esp + stack_offset + 20]
DECLARE_REG 5, edi, edi, di, null, [esp + stack_offset + 24]
DECLARE_REG 6, ebp, ebp, bp, null, [esp + stack_offset + 28]
%define r7m [esp + stack_offset + 32]
%define r8m [esp + stack_offset + 36]
%define rsp esp

%macro PUSH_IF_USED 1 ; reg_id
    %if %1 < regs_used
        push r%1
        %assign stack_offset stack_offset+4
    %endif
%endmacro

%macro POP_IF_USED 1 ; reg_id
    %if %1 < regs_used
        pop r%1
    %endif
%endmacro

%macro LOAD_IF_USED 2 ; reg_id, number_of_args
    %if %1 < %2
        mov r%1, [esp + stack_offset + 4 + %1*4]
    %endif
%endmacro

%macro PROLOGUE 2-4+ 0 ; #args, #regs, pic, arg_names...
    ASSERT %2 >= %1
    %assign stack_offset 0
    %assign regs_used %2
    %ifdef PIC
    %if %3
        %assign regs_used regs_used+1
    %endif
    %endif
    ASSERT regs_used <= 7
    PUSH_IF_USED 3
    PUSH_IF_USED 4
    PUSH_IF_USED 5
    PUSH_IF_USED 6
    LOAD_IF_USED 0, %1
    LOAD_IF_USED 1, %1
    LOAD_IF_USED 2, %1
    LOAD_IF_USED 3, %1
    LOAD_IF_USED 4, %1
    LOAD_IF_USED 5, %1
    LOAD_IF_USED 6, %1
    %if %3
        picgetgot r%2
    %endif
    DEFINE_ARGS %4
%endmacro

%macro RET 0
    POP_IF_USED 6
    POP_IF_USED 5
    POP_IF_USED 4
    POP_IF_USED 3
    ret
%endmacro

%macro REP_RET 0
    %if regs_used > 3
        RET
    %else
        rep ret
    %endif
%endmacro

%endif ;======================================================================



;=============================================================================
; arch-independent part
;=============================================================================

%assign function_align 16

; Symbol prefix for C linkage
%macro cglobal 1-2+
    %xdefine %1 ff_%1
    %ifdef PREFIX
        %xdefine %1 _ %+ %1
    %endif
    %ifidn __OUTPUT_FORMAT__,elf
        global %1:function hidden
    %else
        global %1
    %endif
    align function_align
    %1:
    RESET_MM_PERMUTATION ; not really needed, but makes disassembly somewhat nicer
    %if %0 > 1
        PROLOGUE %2
    %endif
%endmacro

%macro cextern 1
    %ifdef PREFIX
        extern _%1
        %define %1 _%1
    %else
        extern %1
    %endif
%endmacro

; This is needed for ELF, otherwise the GNU linker assumes the stack is
; executable by default.
%ifidn __OUTPUT_FORMAT__,elf
SECTION .note.GNU-stack noalloc noexec nowrite progbits
%endif

%assign FENC_STRIDE 16
%assign FDEC_STRIDE 32

; merge mmx and sse*

%macro CAT_XDEFINE 3
    %xdefine %1%2 %3
%endmacro

%macro CAT_UNDEF 2
    %undef %1%2
%endmacro

%macro INIT_MMX 0
    %define RESET_MM_PERMUTATION INIT_MMX
    %define mmsize 8
    %define num_mmregs 8
    %define mova movq
    %define movu movq
    %define movh movd
    %define movnt movntq
    %assign %%i 0
    %rep 8
    CAT_XDEFINE m, %%i, mm %+ %%i
    CAT_XDEFINE nmm, %%i, %%i
    %assign %%i %%i+1
    %endrep
    %rep 8
    CAT_UNDEF m, %%i
    CAT_UNDEF nmm, %%i
    %assign %%i %%i+1
    %endrep
%endmacro

%macro INIT_XMM 0
    %define RESET_MM_PERMUTATION INIT_XMM
    %define mmsize 16
    %define num_mmregs 8
    %ifdef ARCH_X86_64
    %define num_mmregs 16
    %endif
    %define mova movdqa
    %define movu movdqu
    %define movh movq
    %define movnt movntdq
    %assign %%i 0
    %rep num_mmregs
    CAT_XDEFINE m, %%i, xmm %+ %%i
    CAT_XDEFINE nxmm, %%i, %%i
    %assign %%i %%i+1
    %endrep
%endmacro

INIT_MMX

; I often want to use macros that permute their arguments. e.g. there's no
; efficient way to implement butterfly or transpose or dct without swapping some
; arguments.
;
; I would like to not have to manually keep track of the permutations:
; If I insert a permutation in the middle of a function, it should automatically
; change everything that follows. For more complex macros I may also have multiple
; implementations, e.g. the SSE2 and SSSE3 versions may have different permutations.
;
; Hence these macros. Insert a PERMUTE or some SWAPs at the end of a macro that
; permutes its arguments. It's equivalent to exchanging the contents of the
; registers, except that this way you exchange the register names instead, so it
; doesn't cost any cycles.

%macro PERMUTE 2-* ; takes a list of pairs to swap
%rep %0/2
    %xdefine tmp%2 m%2
    %xdefine ntmp%2 nm%2
    %rotate 2
%endrep
%rep %0/2
    %xdefine m%1 tmp%2
    %xdefine nm%1 ntmp%2
    %undef tmp%2
    %undef ntmp%2
    %rotate 2
%endrep
%endmacro

%macro SWAP 2-* ; swaps a single chain (sometimes more concise than pairs)
%rep %0-1
%ifdef m%1
    %xdefine tmp m%1
    %xdefine m%1 m%2
    %xdefine m%2 tmp
    CAT_XDEFINE n, m%1, %1
    CAT_XDEFINE n, m%2, %2
%else
    ; If we were called as "SWAP m0,m1" rather than "SWAP 0,1" infer the original numbers here.
    ; Be careful using this mode in nested macros though, as in some cases there may be
    ; other copies of m# that have already been dereferenced and don't get updated correctly.
    %xdefine %%n1 n %+ %1
    %xdefine %%n2 n %+ %2
    %xdefine tmp m %+ %%n1
    CAT_XDEFINE m, %%n1, m %+ %%n2
    CAT_XDEFINE m, %%n2, tmp
    CAT_XDEFINE n, m %+ %%n1, %%n1
    CAT_XDEFINE n, m %+ %%n2, %%n2
%endif
    %undef tmp
    %rotate 1
%endrep
%endmacro

%macro SAVE_MM_PERMUTATION 1
    %assign %%i 0
    %rep num_mmregs
    CAT_XDEFINE %1_m, %%i, m %+ %%i
    %assign %%i %%i+1
    %endrep
%endmacro

%macro LOAD_MM_PERMUTATION 1
    %assign %%i 0
    %rep num_mmregs
    CAT_XDEFINE m, %%i, %1_m %+ %%i
    %assign %%i %%i+1
    %endrep
%endmacro

%macro call 1
    call %1
    %ifdef %1_m0
        LOAD_MM_PERMUTATION %1
    %endif
%endmacro

; substitutions which are functionally identical but reduce code size
%define movdqa movaps
%define movdqu movups

