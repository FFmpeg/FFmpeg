;*****************************************************************************
;* Copyright (C) 2005-2010 x264 project
;*
;* Authors: Loren Merritt <lorenm@u.washington.edu>
;*          Fiona Glaser <fiona@x264.com>
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

%include "x86util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void ff_cpu_cpuid(int index, int *eax, int *ebx, int *ecx, int *edx)
;-----------------------------------------------------------------------------
cglobal cpu_cpuid, 5,7
    push rbx
    push  r4
    push  r3
    push  r2
    push  r1
    mov  eax, r0d
    xor  ecx, ecx
    cpuid
    pop   r4
    mov [r4], eax
    pop   r4
    mov [r4], ebx
    pop   r4
    mov [r4], ecx
    pop   r4
    mov [r4], edx
    pop  rbx
    RET

;-----------------------------------------------------------------------------
; void ff_cpu_xgetbv(int op, int *eax, int *edx)
;-----------------------------------------------------------------------------
cglobal cpu_xgetbv, 3,7
    push  r2
    push  r1
    mov  ecx, r0d
    xgetbv
    pop   r4
    mov [r4], eax
    pop   r4
    mov [r4], edx
    RET

%if ARCH_X86_64 == 0
;-----------------------------------------------------------------------------
; int ff_cpu_cpuid_test(void)
; return 0 if unsupported
;-----------------------------------------------------------------------------
cglobal cpu_cpuid_test
    pushfd
    push    ebx
    push    ebp
    push    esi
    push    edi
    pushfd
    pop     eax
    mov     ebx, eax
    xor     eax, 0x200000
    push    eax
    popfd
    pushfd
    pop     eax
    xor     eax, ebx
    pop     edi
    pop     esi
    pop     ebp
    pop     ebx
    popfd
    ret
%endif
