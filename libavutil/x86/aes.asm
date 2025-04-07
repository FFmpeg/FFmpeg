;*****************************************************************************
;* Copyright (c) 2015 Rodger Combs <rodger.combs@gmail.com>
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

%include "x86util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void ff_aes_decrypt(AVAES *a, uint8_t *dst, const uint8_t *src,
;                     int count, uint8_t *iv, int rounds)
;-----------------------------------------------------------------------------
%macro AES_CRYPT 2
cglobal aes_%1rypt_%2, 5, 5, 2
    test     r3d, r3d
    je .ret
    shl      r3d, 4
    add       r0, 0x70
    add       r2, r3
    add       r1, r3
    neg       r3
    pxor      m1, m1
    test      r4, r4
    je .block
    movu      m1, [r4] ; iv
.block:
    movu      m0, [r2+r3] ; state
%ifidn %1, enc
    pxor      m0, m1
%endif
    pxor      m0, [r0+8*2*%2-0x70]
%if %2 > 10
%if %2 > 12
    aes%1     m0, [r0+0x60]
    aes%1     m0, [r0+0x50]
%endif
    aes%1     m0, [r0+0x40]
    aes%1     m0, [r0+0x30]
%endif
    aes%1     m0, [r0+0x20]
    aes%1     m0, [r0+0x10]
    aes%1     m0, [r0+0x00]
    aes%1     m0, [r0-0x10]
    aes%1     m0, [r0-0x20]
    aes%1     m0, [r0-0x30]
    aes%1     m0, [r0-0x40]
    aes%1     m0, [r0-0x50]
    aes%1     m0, [r0-0x60]
    aes%1last m0, [r0-0x70]
    test      r4, r4
    je .noiv
%ifidn %1, enc
    mova      m1, m0
%else
    pxor      m0, m1
    movu      m1, [r2+r3]
%endif
.noiv:
    movu [r1+r3], m0
    add       r3, 16
    jl .block
    test      r4, r4
    je .ret
%ifidn %1, dec
    movu    [r4], m1
%else
    movu    [r4], m0
%endif
.ret:
    REP_RET
%endmacro

%if HAVE_AESNI_EXTERNAL
INIT_XMM aesni
AES_CRYPT enc, 10
AES_CRYPT enc, 12
AES_CRYPT enc, 14
AES_CRYPT dec, 10
AES_CRYPT dec, 12
AES_CRYPT dec, 14
%endif
