; Copyright © 2018, VideoLAN and dav1d authors
; Copyright © 2018, Two Orioles, LLC
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice, this
;    list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
; ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
; WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
; ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
; (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
; ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

%undef private_prefix
%define private_prefix checkasm

%include "checkasm_config.asm"
%include "x86/x86inc.asm"

SECTION_RODATA 16

%if ARCH_X86_64
    ; just random numbers to reduce the chance of incidental match
    %if WIN64
        x6:  dq 0x1a1b2550a612b48c,0x79445c159ce79064
        x7:  dq 0x2eed899d5a28ddcd,0x86b2536fcd8cf636
        x8:  dq 0xb0856806085e7943,0x3f2bf84fc0fcca4e
        x9:  dq 0xacbd382dcf5b8de2,0xd229e1f5b281303f
        x10: dq 0x71aeaff20b095fd9,0xab63e2e11fa38ed9
        x11: dq 0x89b0c0765892729a,0x77d410d5c42c882d
        x12: dq 0xc45ea11a955d8dd5,0x24b3c1d2a024048b
        x13: dq 0x2e8ec680de14b47c,0xdd7b8919edd42786
        x14: dq 0x135ce6888fa02cbf,0x11e53e2b2ac655ef
        x15: dq 0x011ff554472a7a10,0x6de8f4c914c334d5
        n7:  dq 0x21f86d66c8ca00ce
        n8:  dq 0x75b6ba21077c48ad
    %endif
    n9:  dq 0xed56bb2dcb3c7736
    n10: dq 0x8bda43d3fd1a7e06
    n11: dq 0xb64a9c9e5d318408
    n12: dq 0xdf9a54b303f1d3a3
    n13: dq 0x4a75479abd64e097
    n14: dq 0x249214109d5d1c88
%endif

errmsg_stack: db "stack corruption", 0
errmsg_register: db "failed to preserve register:%s", 0
errmsg_vzeroupper: db "missing vzeroupper", 0
errmsg_emms: db "missing emms, fpu tag = 0x%x", 0

SECTION .text

cextern check_vzeroupper
cextern fail_abort

; max number of args used by any asm function.
; (max_args % 4) must equal 3 for stack alignment
%define max_args 15

%if UNIX64
    DECLARE_REG_TMP 0
%else
    DECLARE_REG_TMP 4
%endif

;-----------------------------------------------------------------------------
; uint64_t checkasm_cpu_xgetbv(unsigned xcr)
;-----------------------------------------------------------------------------
cvisible_for_tests cpu_xgetbv, 0, 0, 0, xcr
    movifnidn ecx, xcrm
    xgetbv
%if ARCH_X86_64
    shl       rdx, 32
    or        rax, rdx
%endif
    RET

;-----------------------------------------------------------------------------
; void checkasm_cpu_cpuid(CpuidRegisters *regs, unsigned leaf, unsigned subleaf)
;-----------------------------------------------------------------------------
cvisible_for_tests cpu_cpuid, 0, 5, 0, regs, leaf, subleaf
    mov        r4, regsmp
    mov       eax, leafm
    mov       ecx, subleafm
%if ARCH_X86_64
    mov        r5, rbx
%endif
    cpuid
    mov  [r4+4*0], eax
    mov  [r4+4*1], ebx
    mov  [r4+4*2], ecx
    mov  [r4+4*3], edx
%if ARCH_X86_64
    mov       rbx, r5
%endif
    RET

cvisible empty_mmx, 0, 0
    emms
    RET

%macro REPORT_FAILURE 1 ; err_msg
    LEA            r0, %1
%if ARCH_X86_64
    xor           eax, eax
%else
    mov         [esp], r0
%endif
    call fail_abort
    ud2 ; should never be reached
%endmacro

%if ARCH_X86_64

%if WIN64
    %define stack_param rsp+32 ; shadow space
    %define num_fn_args rsp+stack_offset+16*8
    %assign num_reg_args 4
    %assign free_regs 7
    %assign clobber_mask_stack_bit 16
    DECLARE_REG_TMP 4
%else
    %define stack_param rsp
    %define num_fn_args rsp+stack_offset+10*8
    %assign num_reg_args 6
    %assign free_regs 9
    %assign clobber_mask_stack_bit 64
    DECLARE_REG_TMP 7
%endif

%macro CLOBBER_UPPER 2 ; reg, mask_bit
    mov          r13d, %1d
    or            r13, r8
    test          r9b, %2
    cmovnz         %1, r13
%endmacro

%macro checked_call_fn 0-1
cvisible checked_call%1, 2, 15, 16, max_args*8+64+8
    mov          r10d, [num_fn_args]
    mov            r8, 0xdeadbeef00000000
    mov           r9d, [num_fn_args+r10*8+16] ; clobber_mask
    mov            t0, [num_fn_args+r10*8+8]  ; func

    ; Ensure a clean initial YMM state if vzeroupper-checking is enabled
    cmp dword [check_vzeroupper], 0
    je .clobber_upper_register_halves
    vzeroupper

.clobber_upper_register_halves:
    ; Clobber the upper halves of 32-bit parameters
    CLOBBER_UPPER  r0, 1
    CLOBBER_UPPER  r1, 2
    CLOBBER_UPPER  r2, 4
    CLOBBER_UPPER  r3, 8
%if UNIX64
    CLOBBER_UPPER  r4, 16
    CLOBBER_UPPER  r5, 32
%else ; WIN64
    %assign i 6
    %rep 16-6
        mova       m %+ i, [x %+ i]
        %assign i i+1
    %endrep
%endif

    xor          r11d, r11d
    sub          r10d, num_reg_args
    cmovs        r10d, r11d ; num stack args

    ; write stack canaries to the area above parameters passed on the stack
    mov           r12, [rsp+stack_offset] ; return address
    not           r12
%assign i 0
%rep 8 ; 64 bytes
    mov [stack_param+(r10+i)*8], r12
    %assign i i+1
%endrep

    test         r10d, r10d
    jz .stack_setup_done ; no stack parameters
.copy_stack_parameter:
    mov           r12, [stack_param+stack_offset+8+r11*8]
    CLOBBER_UPPER r12, clobber_mask_stack_bit
    shr           r9d, 1
    mov [stack_param+r11*8], r12
    inc          r11d
    cmp          r11d, r10d
    jl .copy_stack_parameter
.stack_setup_done:

%assign i 14
%rep 15-free_regs
    mov        r %+ i, [n %+ i]
    %assign i i-1
%endrep
    call           t0

    ; check for stack corruption
    mov           r0d, [num_fn_args]
    xor           r3d, r3d
    sub           r0d, num_reg_args
    cmovs         r0d, r3d ; num stack args

    mov            r3, [rsp+stack_offset]
    mov            r4, [stack_param+r0*8]
    not            r3
    xor            r4, r3
%assign i 1
%rep 6
    mov            r5, [stack_param+(r0+i)*8]
    xor            r5, r3
    or             r4, r5
    %assign i i+1
%endrep
    xor            r3, [stack_param+(r0+7)*8]
    or             r4, r3
    jz .stack_ok
    REPORT_FAILURE errmsg_stack
.stack_ok:

    ; check for failure to preserve registers
%assign i 14
%rep 15-free_regs
    cmp        r %+ i, [n %+ i]
    setne         r4b
    lea           r3d, [r4+r3*2]
    %assign i i-1
%endrep
    lea            r0, [stack_param]
    test          r3d, r3d
%if WIN64
    jz .gpr_ok
%else
    jz .gpr_xmm_ok
%endif
%assign i free_regs
%rep 15-free_regs
%if i < 10
    mov    dword [r0], " r0" + (i << 16)
    lea            r4, [r0+3]
%else
    mov    dword [r0], " r10" + ((i - 10) << 24)
    lea            r4, [r0+4]
%endif
    test          r3b, 1 << (i - free_regs)
    cmovnz         r0, r4
    %assign i i+1
%endrep

%if WIN64 ; xmm registers
.gpr_ok:
%assign i 6
%rep 16-6
    pxor       m %+ i, [x %+ i]
    %assign i i+1
%endrep
    packsswb       m6, m7
    packsswb       m8, m9
    packsswb      m10, m11
    packsswb      m12, m13
    packsswb      m14, m15
    packsswb       m6, m6
    packsswb       m8, m10
    packsswb      m12, m14
    packsswb       m6, m6
    packsswb       m8, m12
    packsswb       m6, m8
    pxor           m7, m7
    pcmpeqb        m6, m7
    pmovmskb      r3d, m6
    cmp           r3d, 0xffff
    je .xmm_ok
    mov           r7d, " xmm"
%assign i 6
%rep 16-6
    mov        [r0+0], r7d
%if i < 10
    mov   byte [r0+4], "0" + i
    lea            r4, [r0+5]
%else
    mov   word [r0+4], "10" + ((i - 10) << 8)
    lea            r4, [r0+6]
%endif
    test          r3d, 1 << i
    cmovz          r0, r4
    %assign i i+1
%endrep
.xmm_ok:
    lea            r1, [stack_param]
    cmp            r0, r1
    je .gpr_xmm_ok
%else ; UNIX64
    mov            r1, rsp
%endif
    mov     byte [r0], 0
    REPORT_FAILURE errmsg_register
.gpr_xmm_ok:
%ifnidn %1, _emms
    fstenv        [stack_param]
    movzx          r1, word [stack_param + 8]
    cmp            r1, 0xffff
    je .emms_ok ; x87 state clean
    emms
    REPORT_FAILURE errmsg_emms
.emms_ok:
%else ; _emms
    emms
%endif
    ; Check for dirty YMM state, i.e. missing vzeroupper
    mov           ecx, [check_vzeroupper]
    test          ecx, ecx
    jz .ok ; not supported, skip
    mov           r10, rax
    mov           r11, rdx
    xgetbv
    test           al, 0x04
    mov           rax, r10
    mov           rdx, r11
    jz .ok ; clean ymm state
    vzeroupper
    REPORT_FAILURE errmsg_vzeroupper
.ok:
    RET
%endmacro

checked_call_fn
checked_call_fn _emms

%else ; ARCH_X86_32

; just random numbers to reduce the chance of incidental match
%assign n3 0x6549315c
%assign n4 0xe02f3e23
%assign n5 0xb78d0d1d
%assign n6 0x33627ba7

;-----------------------------------------------------------------------------
; void checkasm_checked_call(void *func, ...)
;-----------------------------------------------------------------------------
%macro checked_call_fn 0-1
cvisible checked_call%1, 1, 7
    mov            r3, [esp+stack_offset]      ; return address
    mov            r1, [esp+stack_offset+17*4] ; num_stack_params
    mov            r2, 27
    not            r3
    sub            r2, r1

    ; Ensure a clean initial YMM state if vzeroupper-checking is enabled
    LEA            r4, check_vzeroupper
    cmp    dword [r4], 0
    je .push_canary
    vzeroupper

.push_canary:
    push           r3
    dec            r2
    jg .push_canary
.push_parameter:
    push dword [esp+32*4]
    dec            r1
    jg .push_parameter
    mov            r3, n3
    mov            r4, n4
    mov            r5, n5
    mov            r6, n6
    call           r0

    ; check for failure to preserve registers
    cmp            r3, n3
    setne         r3h
    cmp            r4, n4
    setne         r3b
    shl           r3d, 16
    cmp            r5, n5
    setne         r3h
    cmp            r6, n6
    setne         r3b
    test           r3, r3
    jz .gpr_ok
    lea            r1, [esp+16]
    mov       [esp+4], r1
%assign i 3
%rep 4
    mov    dword [r1], " r0" + (i << 16)
    lea            r4, [r1+3]
    test           r3, 1 << ((6 - i) * 8)
    cmovnz         r1, r4
    %assign i i+1
%endrep
    mov     byte [r1], 0
    REPORT_FAILURE errmsg_register
.gpr_ok:
    ; check for stack corruption
    mov            r3, [esp+48*4] ; num_stack_params
    mov            r6, [esp+31*4] ; return address
    mov            r4, [esp+r3*4]
    sub            r3, 26
    not            r6
    xor            r4, r6
.check_canary:
    mov            r5, [esp+(r3+27)*4]
    xor            r5, r6
    or             r4, r5
    inc            r3
    jl .check_canary
    test           r4, r4
    jz .stack_ok
    REPORT_FAILURE errmsg_stack
.stack_ok:
%ifidn %1, _emms
    emms
%elifnidn %1, _float
    fstenv        [esp]
    movzx          r1, word [esp + 8]
    cmp            r1, 0xffff
    je .emms_ok ; x87 state clean
    emms
    mov       [esp+4], r1
    REPORT_FAILURE errmsg_emms
.emms_ok:
    emms
%endif
    ; check for dirty YMM state, i.e. missing vzeroupper
    LEA           ecx, check_vzeroupper
    mov           ecx, [ecx]
    test          ecx, ecx
    jz .ok ; not supported, skip
    mov           r5, eax
    mov           r6, edx
    xgetbv
    test           al, 0x04
    mov          eax, r5
    mov          edx, r6
    jz .ok ; clean ymm state
    vzeroupper
    REPORT_FAILURE errmsg_vzeroupper
.ok:
    add           esp, 27*4
    RET
%endmacro

checked_call_fn
checked_call_fn _float
checked_call_fn _emms

%endif ; ARCH_X86_32

;-----------------------------------------------------------------------------
; void checkasm_warmup_avx*(void)
;-----------------------------------------------------------------------------
%macro WARMUP 0
cglobal warmup, 0, 0
    xorps          m0, m0
    mulps          m0, m0
    RET
%endmacro

INIT_YMM avx
WARMUP
INIT_ZMM avx512
WARMUP

INIT_XMM
cglobal dirty_ymm_state
    ; Test if the YMM state is considered dirty or clean, after touching an
    ; XMM register using a non-VEX-encoded instruction, after using YMM
    ; followed by vzeroupper. (This currently fails on Zen4 CPUs.)
    vxorps       ymm0, ymm0
    vmulps       ymm0, ymm0
    vzeroupper
    MULPS        xmm0, xmm0 ; Use the raw instruction to circumvent FORCE_VEX_ENCODING conversion
    ret
