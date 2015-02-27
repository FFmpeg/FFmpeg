;******************************************************************************
;* SIMD-optimized MLP DSP functions
;* Copyright (c) 2014 James Almer <jamrial@gmail.com>
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

SECTION_TEXT

%if ARCH_X86_64

%macro SHLX 2
%if cpuflag(bmi2)
   shlx %1, %1, %2q
%else
   shl  %1, %2b
%endif
%endmacro

%macro REMATRIX 0
    movdqa        m0, [samplesq]
    movdqa        m1, [coeffsq ]
    pshufd        m2, m0, q2301
    pshufd        m3, m1, q2301
    pmuldq        m0, m1
    pmuldq        m3, m2
    paddq         m0, m3
%if notcpuflag(avx2)
    movdqa        m1, [samplesq + 16]
    movdqa        m2, [coeffsq  + 16]
    pshufd        m3, m1, q2301
    pshufd        m4, m2, q2301
    pmuldq        m1, m2
    pmuldq        m4, m3
    paddq         m0, m1
    paddq         m0, m4
%else
    vextracti128 xm1, m0, 1
    paddq        xm0, xm1
%endif
%endmacro

%macro LOOP_END 0
    pshufd       xm1, xm0, q0032
    paddq        xm0, xm1
    movq      accumq, xm0
    movzx     blsbsd, byte [blsbs_ptrq]             ; load *bypassed_lsbs
    sar       accumq, 14                            ; accum >>= 14
    and       accumd, maskd                         ; accum &= mask
    add       accumd, blsbsd                        ; accum += *bypassed_lsbs
    mov   [samplesq + dest_chq], accumd             ; samples[dest_ch] = accum
    add   blsbs_ptrq, 8                             ; bypassed_lsbs += MAX_CHANNELS;
    add     samplesq, 32                            ; samples += MAX_CHANNELS;
    cmp   blsbs_ptrq, cntq
%endmacro

%macro LOOP_SHIFT_END 0
    pshufd       xm1, xm0, q0032
    paddq        xm0, xm1
    movq      accumq, xm0
    and       indexd, auspd                         ; index &= access_unit_size_pow2;
    movsx     noiseq, byte [noise_bufferq + indexq] ; load noise_buffer[index]
    add       indexd, index2d                       ; index += index2
    SHLX      noiseq, mns                           ; noise_buffer[index] <<= matrix_noise_shift
    add       accumq, noiseq                        ; accum += noise_buffer[index]
    movzx     noised, byte [blsbs_ptrq]             ; load *bypassed_lsbs (reuse tmp noise register)
    sar       accumq, 14                            ; accum >>= 14
    and       accumd, maskd                         ; accum &= mask
    add       accumd, noised                        ; accum += *bypassed_lsbs
    mov   [samplesq + dest_chq], accumd             ; samples[dest_ch] = accum
    add   blsbs_ptrq, 8                             ; bypassed_lsbs += MAX_CHANNELS;
    add     samplesq, 32                            ; samples += MAX_CHANNELS;
    cmp   blsbs_ptrq, cntq
%endmacro

;void ff_mlp_rematrix_channel(int32_t *samples, const int32_t *coeffs,
;                             const uint8_t *bypassed_lsbs, const int8_t *noise_buffer,
;                             int index, unsigned int dest_ch, uint16_t blockpos,
;                             unsigned int maxchan, int matrix_noise_shift,
;                             int access_unit_size_pow2, int32_t mask)
%macro MLP_REMATRIX_CHANNEL 0
cglobal mlp_rematrix_channel, 0, 13, 5, samples, coeffs, blsbs_ptr, blsbs, \
                                        index, dest_ch, blockpos, maxchan, mns, \
                                        accum, mask, cnt
    mov         mnsd, mnsm                          ; load matrix_noise_shift
    movzx  blockposq, word blockposm                ; load and zero extend blockpos (16bit)
    mov     maxchand, maxchanm                      ; load maxchan
    mov        maskd, maskm                         ; load mask
%if WIN64
    mov     dest_chd, dest_chm                      ; load dest_chd (not needed on UNIX64)
%endif
    shl     dest_chd, 2
    lea         cntq, [blsbs_ptrq + blockposq*8]
    test        mnsd, mnsd                          ; is matrix_noise_shift != 0?
    jne .shift                                      ; jump if true
    cmp     maxchand, 4                             ; is maxchan < 4?
    jl .loop4                                       ; jump if true

align 16
.loop8:
    ; Process 5 or more channels
    REMATRIX
    LOOP_END
    jne .loop8
    RET

align 16
.loop4:
    ; Process up to 4 channels
    movdqa       xm0, [samplesq]
    movdqa       xm1, [coeffsq ]
    pshufd       xm2, xm0, q2301
    pshufd       xm3, xm1, q2301
    pmuldq       xm0, xm1
    pmuldq       xm3, xm2
    paddq        xm0, xm3
    LOOP_END
    jne .loop4
    RET

.shift:
%if WIN64
    mov       indexd, indexm         ; load index (not needed on UNIX64)
%endif
    mov          r9d, r9m            ; load access_unit_size_pow2
%if cpuflag(bmi2)
    ; bmi2 has shift functions that accept any gpr, not just cl, so keep things in place.
    DEFINE_ARGS samples, coeffs, blsbs_ptr, noise_buffer, \
                index, dest_ch, accum, index2, mns, \
                ausp, mask, cnt, noise
    add         mnsd, 7              ; matrix_noise_shift += 7
%else ; sse4
    mov           r6, rcx            ; move rcx elsewhere so we can use cl for matrix_noise_shift
%if WIN64
    ; r0 = rcx
    DEFINE_ARGS mns, coeffs, blsbs_ptr, noise_buffer, index, dest_ch, samples, \
                index2, accum, ausp, mask, cnt, noise
%else ; UNIX64
    ; r3 = rcx
    DEFINE_ARGS samples, coeffs, blsbs_ptr, mns, index, dest_ch, noise_buffer, \
                index2, accum, ausp, mask, cnt, noise
%endif
    lea         mnsd, [r8 + 7]       ; rcx = matrix_noise_shift + 7
%endif ; cpuflag
    sub        auspd, 1              ; access_unit_size_pow2 -= 1
    cmp          r7d, 4              ; is maxchan < 4?
    lea      index2q, [indexq*2 + 1] ; index2 = 2 * index + 1;
    jl .loop4_shift                  ; jump if maxchan < 4

align 16
.loop8_shift:
    ; Process 5 or more channels
    REMATRIX
    LOOP_SHIFT_END
    jne .loop8_shift
    RET

align 16
.loop4_shift:
    ; Process up to 4 channels
    movdqa       xm0, [samplesq]
    movdqa       xm1, [coeffsq ]
    pshufd       xm2, xm0, q2301
    pshufd       xm3, xm1, q2301
    pmuldq       xm0, xm1
    pmuldq       xm3, xm2
    paddq        xm0, xm3
    LOOP_SHIFT_END
    jne .loop4_shift
    RET
%endmacro

INIT_XMM sse4
MLP_REMATRIX_CHANNEL
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2, bmi2
MLP_REMATRIX_CHANNEL
%endif

%endif ; ARCH_X86_64
