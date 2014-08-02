/*
 * FFT transform with Altivec optimizations
 * Copyright (c) 2009 Loren Merritt
 *
 * This algorithm (though not any of the implementation details) is
 * based on libdjbfft by D. J. Bernstein.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * These functions are not individually interchangeable with the C versions.
 * While C takes arrays of FFTComplex, Altivec leaves intermediate results
 * in blocks as convenient to the vector size.
 * i.e. {4x real, 4x imaginary, 4x real, ...}
 *
 * I ignore standard calling convention.
 * Instead, the following registers are treated as global constants:
 * v14: zero
 * v15..v18: cosines
 * v19..v29: permutations
 * r9: 16
 * r12: ff_cos_tabs
 * and the rest are free for local use.
 */

#include "config.h"

#if HAVE_GNU_AS && HAVE_ALTIVEC

#include "asm.S"

.text

.macro addi2 ra, imm // add 32-bit immediate
.if \imm & 0xffff
    addi \ra, \ra, \imm@l
.endif
.if (\imm+0x8000)>>16
    addis \ra, \ra, \imm@ha
.endif
.endm

.macro FFT4 a0, a1, a2, a3 // in:0-1 out:2-3
    vperm   \a2,\a0,\a1,v20 // vcprm(0,1,s2,s1) // {r0,i0,r3,i2}
    vperm   \a3,\a0,\a1,v21 // vcprm(2,3,s0,s3) // {r1,i1,r2,i3}
    vaddfp  \a0,\a2,\a3                         // {t1,t2,t6,t5}
    vsubfp  \a1,\a2,\a3                         // {t3,t4,t8,t7}
    vmrghw  \a2,\a0,\a1     // vcprm(0,s0,1,s1) // {t1,t3,t2,t4}
    vperm   \a3,\a0,\a1,v22 // vcprm(2,s3,3,s2) // {t6,t7,t5,t8}
    vaddfp  \a0,\a2,\a3                         // {r0,r1,i0,i1}
    vsubfp  \a1,\a2,\a3                         // {r2,r3,i2,i3}
    vperm   \a2,\a0,\a1,v23 // vcprm(0,1,s0,s1) // {r0,r1,r2,r3}
    vperm   \a3,\a0,\a1,v24 // vcprm(2,3,s2,s3) // {i0,i1,i2,i3}
.endm

.macro FFT4x2 a0, a1, b0, b1, a2, a3, b2, b3
    vperm   \a2,\a0,\a1,v20 // vcprm(0,1,s2,s1) // {r0,i0,r3,i2}
    vperm   \a3,\a0,\a1,v21 // vcprm(2,3,s0,s3) // {r1,i1,r2,i3}
    vperm   \b2,\b0,\b1,v20
    vperm   \b3,\b0,\b1,v21
    vaddfp  \a0,\a2,\a3                         // {t1,t2,t6,t5}
    vsubfp  \a1,\a2,\a3                         // {t3,t4,t8,t7}
    vaddfp  \b0,\b2,\b3
    vsubfp  \b1,\b2,\b3
    vmrghw  \a2,\a0,\a1     // vcprm(0,s0,1,s1) // {t1,t3,t2,t4}
    vperm   \a3,\a0,\a1,v22 // vcprm(2,s3,3,s2) // {t6,t7,t5,t8}
    vmrghw  \b2,\b0,\b1
    vperm   \b3,\b0,\b1,v22
    vaddfp  \a0,\a2,\a3                         // {r0,r1,i0,i1}
    vsubfp  \a1,\a2,\a3                         // {r2,r3,i2,i3}
    vaddfp  \b0,\b2,\b3
    vsubfp  \b1,\b2,\b3
    vperm   \a2,\a0,\a1,v23 // vcprm(0,1,s0,s1) // {r0,r1,r2,r3}
    vperm   \a3,\a0,\a1,v24 // vcprm(2,3,s2,s3) // {i0,i1,i2,i3}
    vperm   \b2,\b0,\b1,v23
    vperm   \b3,\b0,\b1,v24
.endm

.macro FFT8 a0, a1, b0, b1, a2, a3, b2, b3, b4 // in,out:a0-b1
    vmrghw  \b2,\b0,\b1     // vcprm(0,s0,1,s1) // {r4,r6,i4,i6}
    vmrglw  \b3,\b0,\b1     // vcprm(2,s2,3,s3) // {r5,r7,i5,i7}
    vperm   \a2,\a0,\a1,v20         // FFT4 ...
    vperm   \a3,\a0,\a1,v21
    vaddfp  \b0,\b2,\b3                         // {t1,t3,t2,t4}
    vsubfp  \b1,\b2,\b3                         // {r5,r7,i5,i7}
    vperm   \b4,\b1,\b1,v25 // vcprm(2,3,0,1)   // {i5,i7,r5,r7}
    vaddfp  \a0,\a2,\a3
    vsubfp  \a1,\a2,\a3
    vmaddfp \b1,\b1,v17,v14 // * {-1,1,1,-1}/sqrt(2)
    vmaddfp \b1,\b4,v18,\b1 // * { 1,1,1,1 }/sqrt(2) // {t8,ta,t7,t9}
    vmrghw  \a2,\a0,\a1
    vperm   \a3,\a0,\a1,v22
    vperm   \b2,\b0,\b1,v26 // vcprm(1,2,s3,s0) // {t3,t2,t9,t8}
    vperm   \b3,\b0,\b1,v27 // vcprm(0,3,s2,s1) // {t1,t4,t7,ta}
    vaddfp  \a0,\a2,\a3
    vsubfp  \a1,\a2,\a3
    vaddfp  \b0,\b2,\b3                         // {t1,t2,t9,ta}
    vsubfp  \b1,\b2,\b3                         // {t6,t5,tc,tb}
    vperm   \a2,\a0,\a1,v23
    vperm   \a3,\a0,\a1,v24
    vperm   \b2,\b0,\b1,v28 // vcprm(0,2,s1,s3) // {t1,t9,t5,tb}
    vperm   \b3,\b0,\b1,v29 // vcprm(1,3,s0,s2) // {t2,ta,t6,tc}
    vsubfp  \b0,\a2,\b2                         // {r4,r5,r6,r7}
    vsubfp  \b1,\a3,\b3                         // {i4,i5,i6,i7}
    vaddfp  \a0,\a2,\b2                         // {r0,r1,r2,r3}
    vaddfp  \a1,\a3,\b3                         // {i0,i1,i2,i3}
.endm

.macro BF d0,d1,s0,s1
    vsubfp  \d1,\s0,\s1
    vaddfp  \d0,\s0,\s1
.endm

.macro zip d0,d1,s0,s1
    vmrghw  \d0,\s0,\s1
    vmrglw  \d1,\s0,\s1
.endm

.macro def_fft4 interleave
fft4\interleave\()_altivec:
    lvx    v0, 0,r3
    lvx    v1,r9,r3
    FFT4   v0,v1,v2,v3
.ifnb \interleave
    zip    v0,v1,v2,v3
    stvx   v0, 0,r3
    stvx   v1,r9,r3
.else
    stvx   v2, 0,r3
    stvx   v3,r9,r3
.endif
    blr
.endm

.macro def_fft8 interleave
fft8\interleave\()_altivec:
    addi   r4,r3,32
    lvx    v0, 0,r3
    lvx    v1,r9,r3
    lvx    v2, 0,r4
    lvx    v3,r9,r4
    FFT8   v0,v1,v2,v3,v4,v5,v6,v7,v8
.ifnb \interleave
    zip    v4,v5,v0,v1
    zip    v6,v7,v2,v3
    stvx   v4, 0,r3
    stvx   v5,r9,r3
    stvx   v6, 0,r4
    stvx   v7,r9,r4
.else
    stvx   v0, 0,r3
    stvx   v1,r9,r3
    stvx   v2, 0,r4
    stvx   v3,r9,r4
.endif
    blr
.endm

.macro def_fft16 interleave
fft16\interleave\()_altivec:
    addi   r5,r3,64
    addi   r6,r3,96
    addi   r4,r3,32
    lvx    v0, 0,r5
    lvx    v1,r9,r5
    lvx    v2, 0,r6
    lvx    v3,r9,r6
    FFT4x2 v0,v1,v2,v3,v4,v5,v6,v7
    lvx    v0, 0,r3
    lvx    v1,r9,r3
    lvx    v2, 0,r4
    lvx    v3,r9,r4
    FFT8   v0,v1,v2,v3,v8,v9,v10,v11,v12
    vmaddfp   v8,v4,v15,v14 // r2*wre
    vmaddfp   v9,v5,v15,v14 // i2*wre
    vmaddfp  v10,v6,v15,v14 // r3*wre
    vmaddfp  v11,v7,v15,v14 // i3*wre
    vmaddfp   v8,v5,v16,v8  // i2*wim
    vnmsubfp  v9,v4,v16,v9  // r2*wim
    vnmsubfp v10,v7,v16,v10 // i3*wim
    vmaddfp  v11,v6,v16,v11 // r3*wim
    BF     v10,v12,v10,v8
    BF     v11,v13,v9,v11
    BF     v0,v4,v0,v10
    BF     v3,v7,v3,v12
    BF     v1,v5,v1,v11
    BF     v2,v6,v2,v13
.ifnb \interleave
    zip     v8, v9,v0,v1
    zip    v10,v11,v2,v3
    zip    v12,v13,v4,v5
    zip    v14,v15,v6,v7
    stvx    v8, 0,r3
    stvx    v9,r9,r3
    stvx   v10, 0,r4
    stvx   v11,r9,r4
    stvx   v12, 0,r5
    stvx   v13,r9,r5
    stvx   v14, 0,r6
    stvx   v15,r9,r6
.else
    stvx   v0, 0,r3
    stvx   v4, 0,r5
    stvx   v3,r9,r4
    stvx   v7,r9,r6
    stvx   v1,r9,r3
    stvx   v5,r9,r5
    stvx   v2, 0,r4
    stvx   v6, 0,r6
.endif
    blr
.endm

// void pass(float *z, float *wre, int n)
.macro PASS interleave, suffix
fft_pass\suffix\()_altivec:
    mtctr  r5
    slwi   r0,r5,4
    slwi   r7,r5,6   // o2
    slwi   r5,r5,5   // o1
    add   r10,r5,r7  // o3
    add    r0,r4,r0  // wim
    addi   r6,r5,16  // o1+16
    addi   r8,r7,16  // o2+16
    addi  r11,r10,16 // o3+16
1:
    lvx    v8, 0,r4  // wre
    lvx   v10, 0,r0  // wim
    sub    r0,r0,r9
    lvx    v9, 0,r0
    vperm  v9,v9,v10,v19   // vcprm(s0,3,2,1) => wim[0 .. -3]
    lvx    v4,r3,r7        // r2 = z[o2]
    lvx    v5,r3,r8        // i2 = z[o2+16]
    lvx    v6,r3,r10       // r3 = z[o3]
    lvx    v7,r3,r11       // i3 = z[o3+16]
    vmaddfp  v10,v4,v8,v14 // r2*wre
    vmaddfp  v11,v5,v8,v14 // i2*wre
    vmaddfp  v12,v6,v8,v14 // r3*wre
    vmaddfp  v13,v7,v8,v14 // i3*wre
    lvx    v0, 0,r3        // r0 = z[0]
    lvx    v3,r3,r6        // i1 = z[o1+16]
    vmaddfp  v10,v5,v9,v10 // i2*wim
    vnmsubfp v11,v4,v9,v11 // r2*wim
    vnmsubfp v12,v7,v9,v12 // i3*wim
    vmaddfp  v13,v6,v9,v13 // r3*wim
    lvx    v1,r3,r9        // i0 = z[16]
    lvx    v2,r3,r5        // r1 = z[o1]
    BF     v12,v8,v12,v10
    BF     v13,v9,v11,v13
    BF     v0,v4,v0,v12
    BF     v3,v7,v3,v8
.if !\interleave
    stvx   v0, 0,r3
    stvx   v4,r3,r7
    stvx   v3,r3,r6
    stvx   v7,r3,r11
.endif
    BF     v1,v5,v1,v13
    BF     v2,v6,v2,v9
.if !\interleave
    stvx   v1,r3,r9
    stvx   v2,r3,r5
    stvx   v5,r3,r8
    stvx   v6,r3,r10
.else
    vmrghw v8,v0,v1
    vmrglw v9,v0,v1
    stvx   v8, 0,r3
    stvx   v9,r3,r9
    vmrghw v8,v2,v3
    vmrglw v9,v2,v3
    stvx   v8,r3,r5
    stvx   v9,r3,r6
    vmrghw v8,v4,v5
    vmrglw v9,v4,v5
    stvx   v8,r3,r7
    stvx   v9,r3,r8
    vmrghw v8,v6,v7
    vmrglw v9,v6,v7
    stvx   v8,r3,r10
    stvx   v9,r3,r11
.endif
    addi   r3,r3,32
    addi   r4,r4,16
    bdnz 1b
    sub    r3,r3,r5
    blr
.endm

#define M_SQRT1_2      0.70710678118654752440  /* 1/sqrt(2) */

#define WORD_0  0x00,0x01,0x02,0x03
#define WORD_1  0x04,0x05,0x06,0x07
#define WORD_2  0x08,0x09,0x0a,0x0b
#define WORD_3  0x0c,0x0d,0x0e,0x0f
#define WORD_s0 0x10,0x11,0x12,0x13
#define WORD_s1 0x14,0x15,0x16,0x17
#define WORD_s2 0x18,0x19,0x1a,0x1b
#define WORD_s3 0x1c,0x1d,0x1e,0x1f

#define vcprm(a, b, c, d) .byte WORD_##a, WORD_##b, WORD_##c, WORD_##d

    .rodata
    .align 4
fft_data:
    .float  0, 0, 0, 0
    .float  1, 0.92387953, M_SQRT1_2, 0.38268343
    .float  0, 0.38268343, M_SQRT1_2, 0.92387953
    .float  -M_SQRT1_2, M_SQRT1_2, M_SQRT1_2,-M_SQRT1_2
    .float   M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, M_SQRT1_2
    vcprm(s0,3,2,1)
    vcprm(0,1,s2,s1)
    vcprm(2,3,s0,s3)
    vcprm(2,s3,3,s2)
    vcprm(0,1,s0,s1)
    vcprm(2,3,s2,s3)
    vcprm(2,3,0,1)
    vcprm(1,2,s3,s0)
    vcprm(0,3,s2,s1)
    vcprm(0,2,s1,s3)
    vcprm(1,3,s0,s2)

.macro lvm  b, r, regs:vararg
    lvx     \r, 0, \b
    addi    \b, \b, 16
  .ifnb \regs
    lvm     \b, \regs
  .endif
.endm

.macro stvm b, r, regs:vararg
    stvx    \r, 0, \b
    addi    \b, \b, 16
  .ifnb \regs
    stvm    \b, \regs
  .endif
.endm

.macro fft_calc interleave
extfunc ff_fft_calc\interleave\()_altivec
    mflr    r0
    stp     r0, 2*PS(r1)
    stpu    r1, -(160+16*PS)(r1)
    get_got r11
    addi    r6, r1, 16*PS
    stvm    r6, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29
    mfvrsave r0
    stw     r0, 15*PS(r1)
    li      r6, 0xfffffffc
    mtvrsave r6

    movrel  r6, fft_data, r11
    lvm     r6, v14, v15, v16, v17, v18, v19, v20, v21
    lvm     r6, v22, v23, v24, v25, v26, v27, v28, v29

    li      r9, 16
    movrel  r12, X(ff_cos_tabs), r11

    movrel  r6, fft_dispatch_tab\interleave\()_altivec, r11
    lwz     r3, 0(r3)
    subi    r3, r3, 2
    slwi    r3, r3, 2+ARCH_PPC64
    lpx     r3, r3, r6
    mtctr   r3
    mr      r3, r4
    bctrl

    addi    r6, r1, 16*PS
    lvm     r6, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29
    lwz     r6, 15*PS(r1)
    mtvrsave r6
    lp      r1, 0(r1)
    lp      r0, 2*PS(r1)
    mtlr    r0
    blr
.endm

.macro DECL_FFT suffix, bits, n, n2, n4
fft\n\suffix\()_altivec:
    mflr  r0
    stp   r0,PS*(\bits-3)(r1)
    bl    fft\n2\()_altivec
    addi2 r3,\n*4
    bl    fft\n4\()_altivec
    addi2 r3,\n*2
    bl    fft\n4\()_altivec
    addi2 r3,\n*-6
    lp    r0,PS*(\bits-3)(r1)
    lp    r4,\bits*PS(r12)
    mtlr  r0
    li    r5,\n/16
    b     fft_pass\suffix\()_altivec
.endm

.macro DECL_FFTS interleave, suffix
    .text
    def_fft4  \suffix
    def_fft8  \suffix
    def_fft16 \suffix
    PASS \interleave, \suffix
    DECL_FFT \suffix, 5,   32,   16,    8
    DECL_FFT \suffix, 6,   64,   32,   16
    DECL_FFT \suffix, 7,  128,   64,   32
    DECL_FFT \suffix, 8,  256,  128,   64
    DECL_FFT \suffix, 9,  512,  256,  128
    DECL_FFT \suffix,10, 1024,  512,  256
    DECL_FFT \suffix,11, 2048, 1024,  512
    DECL_FFT \suffix,12, 4096, 2048, 1024
    DECL_FFT \suffix,13, 8192, 4096, 2048
    DECL_FFT \suffix,14,16384, 8192, 4096
    DECL_FFT \suffix,15,32768,16384, 8192
    DECL_FFT \suffix,16,65536,32768,16384

    fft_calc \suffix

    .rodata
    .align 3
fft_dispatch_tab\suffix\()_altivec:
    PTR fft4\suffix\()_altivec
    PTR fft8\suffix\()_altivec
    PTR fft16\suffix\()_altivec
    PTR fft32\suffix\()_altivec
    PTR fft64\suffix\()_altivec
    PTR fft128\suffix\()_altivec
    PTR fft256\suffix\()_altivec
    PTR fft512\suffix\()_altivec
    PTR fft1024\suffix\()_altivec
    PTR fft2048\suffix\()_altivec
    PTR fft4096\suffix\()_altivec
    PTR fft8192\suffix\()_altivec
    PTR fft16384\suffix\()_altivec
    PTR fft32768\suffix\()_altivec
    PTR fft65536\suffix\()_altivec
.endm

DECL_FFTS 0
DECL_FFTS 1, _interleave

#endif /* HAVE_GNU_AS && HAVE_ALTIVEC */
