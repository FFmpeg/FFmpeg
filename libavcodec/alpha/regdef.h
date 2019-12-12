/*
 * Alpha optimized DSP utils
 * copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

/* Some BSDs don't seem to have regdef.h... sigh  */
#ifndef AVCODEC_ALPHA_REGDEF_H
#define AVCODEC_ALPHA_REGDEF_H

#define v0      $0      /* function return value */

#define t0      $1      /* temporary registers (caller-saved) */
#define t1      $2
#define t2      $3
#define t3      $4
#define t4      $5
#define t5      $6
#define t6      $7
#define t7      $8

#define s0      $9      /* saved-registers (callee-saved registers) */
#define s1      $10
#define s2      $11
#define s3      $12
#define s4      $13
#define s5      $14
#define s6      $15
#define fp      s6      /* frame-pointer (s6 in frame-less procedures) */

#define a0      $16     /* argument registers (caller-saved) */
#define a1      $17
#define a2      $18
#define a3      $19
#define a4      $20
#define a5      $21

#define t8      $22     /* more temps (caller-saved) */
#define t9      $23
#define t10     $24
#define t11     $25
#define ra      $26     /* return address register */
#define t12     $27

#define pv      t12     /* procedure-variable register */
#define AT      $at     /* assembler temporary */
#define gp      $29     /* global pointer */
#define sp      $30     /* stack pointer */
#define zero    $31     /* reads as zero, writes are noops */

/* Some nicer register names.  */
#define ta t10
#define tb t11
#define tc t12
#define td AT
/* Danger: these overlap with the argument list and the return value */
#define te a5
#define tf a4
#define tg a3
#define th v0

#endif /* AVCODEC_ALPHA_REGDEF_H */
