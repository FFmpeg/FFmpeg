/*
 * FFT transform with Altivec optimizations
 * Copyright (c) 2009 Loren Merritt
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
#include "asm.S"

.text

/* Apple gas doesn't support this shorthand */
.macro mtctr rx
    mtspr 9, \rx
.endm

.macro addi2 ra, imm // add 32-bit immediate
.if \imm & 0xffff
    addi \ra, \ra, \imm@l
.endif
.if (\imm+0x8000)>>16
    addis \ra, \ra, \imm@ha
.endif
.endm

#if ARCH_PPC64
#define PTR .quad
.macro LOAD_PTR ra, rbase, offset
    ld  \ra,(\offset)*8(\rbase)
.endm
.macro STORE_PTR ra, rbase, offset
    std \ra,(\offset)*8(\rbase)
.endm
#else
#define PTR .int
.macro LOAD_PTR ra, rbase, offset
    lwz \ra,(\offset)*4(\rbase)
.endm
.macro STORE_PTR ra, rbase, offset
    stw \ra,(\offset)*4(\rbase)
.endm
#endif

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

fft4_altivec:
    lvx    v0, 0,r3
    lvx    v1,r9,r3
    FFT4   v0,v1,v2,v3
    stvx   v2, 0,r3
    stvx   v3,r9,r3
    blr

fft8_altivec:
    addi   r4,r3,32
    lvx    v0, 0,r3
    lvx    v1,r9,r3
    lvx    v2, 0,r4
    lvx    v3,r9,r4
    FFT8   v0,v1,v2,v3,v4,v5,v6,v7,v8
    stvx   v0, 0,r3
    stvx   v1,r9,r3
    stvx   v2, 0,r4
    stvx   v3,r9,r4
    blr

fft16_altivec:
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
    stvx   v0, 0,r3
    stvx   v4, 0,r5
    stvx   v3,r9,r4
    stvx   v7,r9,r6
    BF     v1,v5,v1,v11
    BF     v2,v6,v2,v13
    stvx   v1,r9,r3
    stvx   v5,r9,r5
    stvx   v2, 0,r4
    stvx   v6, 0,r6
    blr

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

.macro DECL_FFT suffix, bits, n, n2, n4
fft\n\suffix\()_altivec:
    mflr  r0
    STORE_PTR r0,r1,\bits-5
    bl    fft\n2\()_altivec
    addi2 r3,\n*4
    bl    fft\n4\()_altivec
    addi2 r3,\n*2
    bl    fft\n4\()_altivec
    addi2 r3,\n*-6
    LOAD_PTR r0,r1,\bits-5
    LOAD_PTR r4,r12,\bits
    mtlr  r0
    li    r5,\n/16
    b     fft_pass\suffix\()_altivec
.endm

.macro DECL_FFTS interleave, suffix
    .text
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

    .rodata
    .global EXTERN_ASM\()ff_fft_dispatch\suffix\()_altivec
EXTERN_ASM\()ff_fft_dispatch\suffix\()_altivec:
    PTR fft4_altivec
    PTR fft8_altivec
    PTR fft16_altivec
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
