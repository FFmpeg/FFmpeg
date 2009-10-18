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

%ifdef ARCH_X86_64
    %ifidn __OUTPUT_FORMAT__,win32
        %define WIN64
    %else
        %define UNIX64
    %endif
%endif

; FIXME: All of the 64bit asm functions that take a stride as an argument
; via register, assume that the high dword of that register is filled with 0.
; This is true in practice (since we never do any 64bit arithmetic on strides,
; and x264's strides are all positive), but is not guaranteed by the ABI.

; Name of the .rodata section.
; Kludge: Something on OS X fails to align .rodata even given an align attribute,
; so use a different read-only section.
%macro SECTION_RODATA 0-1 16
    %ifidn __OUTPUT_FORMAT__,macho64
        SECTION .text align=%1
    %elifidn __OUTPUT_FORMAT__,macho
        SECTION .text align=%1
        fakegot:
    %else
        SECTION .rodata align=%1
    %endif
%endmacro

; PIC support macros.
; x86_64 can't fit 64bit address literals in most instruction types,
; so shared objects (under the assumption that they might be anywhere
; in memory) must use an address mode that does fit.
; So all accesses to global variables must use this macro, e.g.
;     mov eax, [foo GLOBAL]
; instead of
;     mov eax, [foo]
;
; x86_32 doesn't require PIC.
; Some distros prefer shared objects to be PIC, but nothing breaks if
; the code contains a few textrels, so we'll skip that complexity.

%ifdef WIN64
    %define PIC
%elifndef ARCH_X86_64
    %undef PIC
%endif
%ifdef PIC
    %define GLOBAL wrt rip
%else
    %define GLOBAL
%endif

; Macros to eliminate most code duplication between x86_32 and x86_64:
; Currently this works only for leaf functions which load all their arguments
; into registers at the start, and make no other use of the stack. Luckily that
; covers most of x264's asm.

; PROLOGUE:
; %1 = number of arguments. loads them from stack if needed.
; %2 = number of registers used. pushes callee-saved regs if needed.
; %3 = number of xmm registers used. pushes callee-saved xmm regs if needed.
; %4 = list of names to define to registers
; PROLOGUE can also be invoked by adding the same options to cglobal

; e.g.
; cglobal foo, 2,3,0, dst, src, tmp
; declares a function (foo), taking two args (dst and src) and one local variable (tmp)

; TODO Some functions can use some args directly from the stack. If they're the
; last args then you can just not declare them, but if they're in the middle
; we need more flexible macro.

; RET:
; Pops anything that was pushed by PROLOGUE

; REP_RET:
; Same, but if it doesn't pop anything it becomes a 2-byte ret, for athlons
; which are slow when a normal ret follows a branch.

; registers:
; rN and rNq are the native-size register holding function argument N
; rNd, rNw, rNb are dword, word, and byte size
; rNm is the original location of arg N (a register or on the stack), dword
; rNmp is native size

%macro DECLARE_REG 6
    %define r%1q %2
    %define r%1d %3
    %define r%1w %4
    %define r%1b %5
    %define r%1m %6
    %ifid %6 ; i.e. it's a register
        %define r%1mp %2
    %elifdef ARCH_X86_64 ; memory
        %define r%1mp qword %6
    %else
        %define r%1mp dword %6
    %endif
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

; t# defines for when per-arch register allocation is more complex than just function arguments

%macro DECLARE_REG_TMP 1-*
    %assign %%i 0
    %rep %0
        CAT_XDEFINE t, %%i, r%1
        %assign %%i %%i+1
        %rotate 1
    %endrep
%endmacro

%macro DECLARE_REG_TMP_SIZE 0-*
    %rep %0
        %define t%1q t%1 %+ q
        %define t%1d t%1 %+ d
        %define t%1w t%1 %+ w
        %define t%1b t%1 %+ b
        %rotate 1
    %endrep
%endmacro

DECLARE_REG_TMP_SIZE 0,1,2,3,4,5,6,7

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
            CAT_UNDEF arg_name %+ %%i, m
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
        %xdefine %1m r %+ %%i %+ m
        CAT_XDEFINE arg_name, %%i, %1
        %assign %%i %%i+1
        %rotate 1
    %endrep
    %assign n_arg_names %%i
%endmacro

%ifdef WIN64 ; Windows x64 ;=================================================

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
        mov r%1, [rsp + stack_offset + 8 + %1*8]
    %endif
%endmacro

%macro PROLOGUE 2-4+ ; #args, #regs, #xmm_regs, arg_names...
    ASSERT %2 >= %1
    %assign regs_used %2
    ASSERT regs_used <= 7
    %if %0 > 2
        %assign xmm_regs_used %3
    %else
        %assign xmm_regs_used 0
    %endif
    ASSERT xmm_regs_used <= 16
    %if regs_used > 4
        push r4
        push r5
        %assign stack_offset stack_offset+16
    %endif
    %if xmm_regs_used > 6
        sub rsp, (xmm_regs_used-6)*16+16
        %assign stack_offset stack_offset+(xmm_regs_used-6)*16+16
        %assign %%i xmm_regs_used
        %rep (xmm_regs_used-6)
            %assign %%i %%i-1
            movdqa [rsp + (%%i-6)*16+8], xmm %+ %%i
        %endrep
    %endif
    LOAD_IF_USED 4, %1
    LOAD_IF_USED 5, %1
    LOAD_IF_USED 6, %1
    DEFINE_ARGS %4
%endmacro

%macro RESTORE_XMM_INTERNAL 1
    %if xmm_regs_used > 6
        %assign %%i xmm_regs_used
        %rep (xmm_regs_used-6)
            %assign %%i %%i-1
            movdqa xmm %+ %%i, [%1 + (%%i-6)*16+8]
        %endrep
        add %1, (xmm_regs_used-6)*16+16
    %endif
%endmacro

%macro RESTORE_XMM 1
    RESTORE_XMM_INTERNAL %1
    %assign stack_offset stack_offset-(xmm_regs_used-6)*16+16
    %assign xmm_regs_used 0
%endmacro

%macro RET 0
    RESTORE_XMM_INTERNAL rsp
    %if regs_used > 4
        pop r5
        pop r4
    %endif
    ret
%endmacro

%macro REP_RET 0
    %if regs_used > 4 || xmm_regs_used > 6
        RET
    %else
        rep ret
    %endif
%endmacro

%elifdef ARCH_X86_64 ; *nix x64 ;=============================================

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

%macro PROLOGUE 2-4+ ; #args, #regs, #xmm_regs, arg_names...
    ASSERT %2 >= %1
    ASSERT %2 <= 7
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

%macro PROLOGUE 2-4+ ; #args, #regs, arg_names...
    ASSERT %2 >= %1
    %assign regs_used %2
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
    %xdefine %1.skip_prologue %1 %+ .skip_prologue
    %ifidn __OUTPUT_FORMAT__,elf
        global %1:function hidden
    %else
        global %1
    %endif
    align function_align
    %1:
    RESET_MM_PERMUTATION ; not really needed, but makes disassembly somewhat nicer
    %assign stack_offset 0
    %if %0 > 1
        PROLOGUE %2
    %endif
%endmacro

%macro cextern 1
    %ifdef PREFIX
        %xdefine %1 _%1
    %endif
    extern %1
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
    CAT_XDEFINE n, m %+ %%i, %%i
    %assign %%i %%i+1
    %endrep
%endmacro

%macro call 1
    call %1
    %ifdef %1_m0
        LOAD_MM_PERMUTATION %1
    %endif
%endmacro

;Substitutions that reduce instruction size but are functionally equivalent
%macro add 2
    %ifnum %2
        %if %2==128
            sub %1, -128
        %else
            add %1, %2
        %endif
    %else
        add %1, %2
    %endif
%endmacro

%macro sub 2
    %ifnum %2
        %if %2==128
            add %1, -128
        %else
            sub %1, %2
        %endif
    %else
        sub %1, %2
    %endif
%endmacro
