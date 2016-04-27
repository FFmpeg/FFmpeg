/*
 * Copyright (c) 2014 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/arm/asm.S"

.macro loadregoffsh2  group, index, base, offgroup, offindex
       .altmacro
       loadregoffsh2_ \group, %(\index), \base, \offgroup, %(\offindex)
       .noaltmacro
.endm

.macro loadregoffsh2_ group, index, base, offgroup, offindex
        ldr     \group\index, [\base, \offgroup\offindex, lsl #2]
.endm

.macro eorlslreg  check, data, group, index
        .altmacro
        eorlslreg_ \check, \data, \group, %(\index)
        .noaltmacro
.endm

.macro eorlslreg_ check, data, group, index
        eor     \check, \check, \data, lsl \group\index
.endm

.macro decr_modulo var, by, modulus
 .set \var, \var - \by
 .if \var == 0
  .set \var, \modulus
 .endif
.endm

 .macro load_group1  size, channels, r0, r1, r2, r3, pointer_dead=0
  .if \size == 2
        ldrd    \r0, \r1, [IN], #(\size + 8 - \channels) * 4
  .else // size == 4
   .if IDX1 > 4 || \channels==8
        ldm     IN!, {\r0, \r1, \r2, \r3}
   .else
        ldm     IN, {\r0, \r1, \r2, \r3}
    .if !\pointer_dead
        add     IN, IN, #(4 + 8 - \channels) * 4
     .endif
   .endif
  .endif
        decr_modulo IDX1, \size, \channels
 .endm

 .macro load_group2  size, channels, r0, r1, r2, r3, pointer_dead=0
  .if \size == 2
   .if IDX1 > 2
        ldm     IN!, {\r2, \r3}
   .else
//A   .ifc \r2, ip
//A    .if \pointer_dead
//A       ldm     IN, {\r2, \r3}
//A    .else
//A       ldr     \r2, [IN], #4
//A       ldr     \r3, [IN], #(\size - 1 + 8 - \channels) * 4
//A    .endif
//A   .else
        ldrd    \r2, \r3, [IN], #(\size + 8 - \channels) * 4
//A   .endif
   .endif
  .endif
        decr_modulo IDX1, \size, \channels
 .endm

.macro implement_pack  inorder, channels, shift
.if \inorder
.ifc \shift, mixed

CHECK   .req    a1
COUNT   .req    a2
IN      .req    a3
OUT     .req    a4
DAT0    .req    v1
DAT1    .req    v2
DAT2    .req    v3
DAT3    .req    v4
SHIFT0  .req    v5
SHIFT1  .req    v6
SHIFT2  .req    sl
SHIFT3  .req    fp
SHIFT4  .req    ip
SHIFT5  .req    lr

 .macro output4words
  .set SIZE_GROUP1, IDX1
  .if SIZE_GROUP1 > 4
   .set SIZE_GROUP1, 4
  .endif
  .set SIZE_GROUP2, 4 - SIZE_GROUP1
        load_group1  SIZE_GROUP1, \channels, DAT0, DAT1, DAT2, DAT3
        load_group2  SIZE_GROUP2, \channels, DAT0, DAT1, DAT2, DAT3
   .if \channels == 2
        lsl     DAT0, SHIFT0
        lsl     DAT1, SHIFT1
        lsl     DAT2, SHIFT0
        lsl     DAT3, SHIFT1
   .elseif \channels == 6
    .if IDX2 == 6
        lsl     DAT0, SHIFT0
        lsl     DAT1, SHIFT1
        lsl     DAT2, SHIFT2
        lsl     DAT3, SHIFT3
    .elseif IDX2 == 2
        lsl     DAT0, SHIFT4
        lsl     DAT1, SHIFT5
        lsl     DAT2, SHIFT0
        lsl     DAT3, SHIFT1
    .else // IDX2 == 4
        lsl     DAT0, SHIFT2
        lsl     DAT1, SHIFT3
        lsl     DAT2, SHIFT4
        lsl     DAT3, SHIFT5
    .endif
   .elseif \channels == 8
    .if IDX2 == 8
        uxtb    SHIFT0, SHIFT4, ror #0
        uxtb    SHIFT1, SHIFT4, ror #8
        uxtb    SHIFT2, SHIFT4, ror #16
        uxtb    SHIFT3, SHIFT4, ror #24
    .else
        uxtb    SHIFT0, SHIFT5, ror #0
        uxtb    SHIFT1, SHIFT5, ror #8
        uxtb    SHIFT2, SHIFT5, ror #16
        uxtb    SHIFT3, SHIFT5, ror #24
    .endif
        lsl     DAT0, SHIFT0
        lsl     DAT1, SHIFT1
        lsl     DAT2, SHIFT2
        lsl     DAT3, SHIFT3
   .endif
        eor     CHECK, CHECK, DAT0, lsr #8 - (\channels - IDX2)
        eor     CHECK, CHECK, DAT1, lsr #7 - (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
        eor     CHECK, CHECK, DAT2, lsr #8 - (\channels - IDX2)
        eor     CHECK, CHECK, DAT3, lsr #7 - (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
        stm     OUT!, {DAT0 - DAT3}
 .endm

 .set WORDS_PER_LOOP, \channels  // calculate LCM (channels, 4)
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .set WORDS_PER_LOOP, WORDS_PER_LOOP * 4
 .set SAMPLES_PER_LOOP, WORDS_PER_LOOP / \channels

function ff_mlp_pack_output_inorder_\channels\()ch_mixedshift_armv6, export=1
 .if SAMPLES_PER_LOOP > 1
        tst     COUNT, #SAMPLES_PER_LOOP - 1  // always seems to be in practice
        it      ne
        bne     X(ff_mlp_pack_output)         // but just in case, branch to C implementation if not
 .endif
        teq     COUNT, #0
        it      eq
        bxeq    lr
        push    {v1-v6,sl,fp,lr}
        ldr     SHIFT0, [sp, #(9+1)*4]  // get output_shift from stack
        ldr     SHIFT1, =0x08080808
        ldr     SHIFT4, [SHIFT0]
 .if \channels == 2
        uadd8   SHIFT4, SHIFT4, SHIFT1 // increase all shifts by 8
        uxtb    SHIFT0, SHIFT4, ror #0
        uxtb    SHIFT1, SHIFT4, ror #8
 .else
        ldr     SHIFT5, [SHIFT0, #4]
        uadd8   SHIFT4, SHIFT4, SHIFT1 // increase all shifts by 8
        uadd8   SHIFT5, SHIFT5, SHIFT1
  .if \channels == 6
        uxtb    SHIFT0, SHIFT4, ror #0
        uxtb    SHIFT1, SHIFT4, ror #8
        uxtb    SHIFT2, SHIFT4, ror #16
        uxtb    SHIFT3, SHIFT4, ror #24
        uxtb    SHIFT4, SHIFT5, ror #0
        uxtb    SHIFT5, SHIFT5, ror #8
  .endif
 .endif
 .set IDX1, \channels
 .set IDX2, \channels
0:
 .rept WORDS_PER_LOOP / 4
        output4words
 .endr
        subs    COUNT, COUNT, #SAMPLES_PER_LOOP
        bne     0b
        pop     {v1-v6,sl,fp,pc}
        .ltorg
endfunc
 .purgem output4words

        .unreq  CHECK
        .unreq  COUNT
        .unreq  IN
        .unreq  OUT
        .unreq  DAT0
        .unreq  DAT1
        .unreq  DAT2
        .unreq  DAT3
        .unreq  SHIFT0
        .unreq  SHIFT1
        .unreq  SHIFT2
        .unreq  SHIFT3
        .unreq  SHIFT4
        .unreq  SHIFT5

.else // not mixed

CHECK   .req    a1
COUNT   .req    a2
IN      .req    a3
OUT     .req    a4
DAT0    .req    v1
DAT1    .req    v2
DAT2    .req    v3
DAT3    .req    v4
DAT4    .req    v5
DAT5    .req    v6
DAT6    .req    sl // use these rather than the otherwise unused
DAT7    .req    fp // ip and lr so that we can load them using LDRD

 .macro output4words  tail, head, r0, r1, r2, r3, r4, r5, r6, r7, pointer_dead=0
  .if \head
   .set SIZE_GROUP1, IDX1
   .if SIZE_GROUP1 > 4
    .set SIZE_GROUP1, 4
   .endif
   .set SIZE_GROUP2, 4 - SIZE_GROUP1
        load_group1  SIZE_GROUP1, \channels, \r0, \r1, \r2, \r3, \pointer_dead
  .endif
  .if \tail
        eor     CHECK, CHECK, \r4, lsr #8 - (\channels - IDX2)
        eor     CHECK, CHECK, \r5, lsr #7 - (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
  .endif
  .if \head
        load_group2  SIZE_GROUP2, \channels, \r0, \r1, \r2, \r3, \pointer_dead
  .endif
  .if \tail
        eor     CHECK, CHECK, \r6, lsr #8 - (\channels - IDX2)
        eor     CHECK, CHECK, \r7, lsr #7 - (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
        stm     OUT!, {\r4, \r5, \r6, \r7}
  .endif
  .if \head
        lsl     \r0, #8 + \shift
        lsl     \r1, #8 + \shift
        lsl     \r2, #8 + \shift
        lsl     \r3, #8 + \shift
  .endif
 .endm

 .set WORDS_PER_LOOP, \channels  // calculate LCM (channels, 8)
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .set WORDS_PER_LOOP, WORDS_PER_LOOP * 8
 .set SAMPLES_PER_LOOP, WORDS_PER_LOOP / \channels

function ff_mlp_pack_output_inorder_\channels\()ch_\shift\()shift_armv6, export=1
 .if SAMPLES_PER_LOOP > 1
        tst     COUNT, #SAMPLES_PER_LOOP - 1  // always seems to be in practice
        it      ne
        bne     X(ff_mlp_pack_output)         // but just in case, branch to C implementation if not
 .endif
        subs    COUNT, COUNT, #SAMPLES_PER_LOOP
        it      lo
        bxlo    lr
        push    {v1-v6,sl,fp,lr}
 .set IDX1, \channels
 .set IDX2, \channels
        output4words  0, 1, DAT0, DAT1, DAT2, DAT3, DAT4, DAT5, DAT6, DAT7
0:      beq     1f
 .rept WORDS_PER_LOOP / 8
        output4words  1, 1, DAT4, DAT5, DAT6, DAT7, DAT0, DAT1, DAT2, DAT3
        output4words  1, 1, DAT0, DAT1, DAT2, DAT3, DAT4, DAT5, DAT6, DAT7
 .endr
        subs    COUNT, COUNT, #SAMPLES_PER_LOOP
        bne     0b
1:
 .rept WORDS_PER_LOOP / 8 - 1
        output4words  1, 1, DAT4, DAT5, DAT6, DAT7, DAT0, DAT1, DAT2, DAT3
        output4words  1, 1, DAT0, DAT1, DAT2, DAT3, DAT4, DAT5, DAT6, DAT7
 .endr
        output4words  1, 1, DAT4, DAT5, DAT6, DAT7, DAT0, DAT1, DAT2, DAT3, pointer_dead=1
        output4words  1, 0, DAT0, DAT1, DAT2, DAT3, DAT4, DAT5, DAT6, DAT7
        pop     {v1-v6,sl,fp,pc}
endfunc
 .purgem output4words

        .unreq  CHECK
        .unreq  COUNT
        .unreq  IN
        .unreq  OUT
        .unreq  DAT0
        .unreq  DAT1
        .unreq  DAT2
        .unreq  DAT3
        .unreq  DAT4
        .unreq  DAT5
        .unreq  DAT6
        .unreq  DAT7

.endif // mixed
.else // not inorder
.ifc \shift, mixed

// This case not currently handled

.else // not mixed

#if !CONFIG_THUMB

CHECK   .req    a1
COUNT   .req    a2
IN      .req    a3
OUT     .req    a4
DAT0    .req    v1
DAT1    .req    v2
DAT2    .req    v3
DAT3    .req    v4
CHAN0   .req    v5
CHAN1   .req    v6
CHAN2   .req    sl
CHAN3   .req    fp
CHAN4   .req    ip
CHAN5   .req    lr

 .macro output4words
  .if \channels == 8
   .if IDX1 == 8
        uxtb    CHAN0, CHAN4, ror #0
        uxtb    CHAN1, CHAN4, ror #8
        uxtb    CHAN2, CHAN4, ror #16
        uxtb    CHAN3, CHAN4, ror #24
   .else
        uxtb    CHAN0, CHAN5, ror #0
        uxtb    CHAN1, CHAN5, ror #8
        uxtb    CHAN2, CHAN5, ror #16
        uxtb    CHAN3, CHAN5, ror #24
   .endif
        ldr     DAT0, [IN, CHAN0, lsl #2]
        ldr     DAT1, [IN, CHAN1, lsl #2]
        ldr     DAT2, [IN, CHAN2, lsl #2]
        ldr     DAT3, [IN, CHAN3, lsl #2]
   .if IDX1 == 4
        add     IN, IN, #8*4
   .endif
        decr_modulo IDX1, 4, \channels
  .else
   .set SIZE_GROUP1, IDX1
   .if SIZE_GROUP1 > 4
    .set SIZE_GROUP1, 4
   .endif
   .set SIZE_GROUP2, 4 - SIZE_GROUP1
   .if SIZE_GROUP1 == 2
        loadregoffsh2  DAT, 0, IN, CHAN, 0 + (\channels - IDX1)
        loadregoffsh2  DAT, 1, IN, CHAN, 1 + (\channels - IDX1)
        add     IN, IN, #8*4
   .else // SIZE_GROUP1 == 4
        loadregoffsh2  DAT, 0, IN, CHAN, 0 + (\channels - IDX1)
        loadregoffsh2  DAT, 1, IN, CHAN, 1 + (\channels - IDX1)
        loadregoffsh2  DAT, 2, IN, CHAN, 2 + (\channels - IDX1)
        loadregoffsh2  DAT, 3, IN, CHAN, 3 + (\channels - IDX1)
    .if IDX1 == 4
        add     IN, IN, #8*4
    .endif
   .endif
        decr_modulo IDX1, SIZE_GROUP1, \channels
   .if SIZE_GROUP2 == 2
        loadregoffsh2  DAT, 2, IN, CHAN, 0 + (\channels - IDX1)
        loadregoffsh2  DAT, 3, IN, CHAN, 1 + (\channels - IDX1)
    .if IDX1 == 2
        add     IN, IN, #8*4
    .endif
   .endif
        decr_modulo IDX1, SIZE_GROUP2, \channels
  .endif
  .if \channels == 8 // in this case we can corrupt CHAN0-3
        rsb     CHAN0, CHAN0, #8
        rsb     CHAN1, CHAN1, #8
        rsb     CHAN2, CHAN2, #8
        rsb     CHAN3, CHAN3, #8
        lsl     DAT0, #8 + \shift
        lsl     DAT1, #8 + \shift
        lsl     DAT2, #8 + \shift
        lsl     DAT3, #8 + \shift
        eor     CHECK, CHECK, DAT0, lsr CHAN0
        eor     CHECK, CHECK, DAT1, lsr CHAN1
        eor     CHECK, CHECK, DAT2, lsr CHAN2
        eor     CHECK, CHECK, DAT3, lsr CHAN3
  .else
   .if \shift != 0
        lsl     DAT0, #\shift
        lsl     DAT1, #\shift
        lsl     DAT2, #\shift
        lsl     DAT3, #\shift
   .endif
        bic     DAT0, DAT0, #0xff000000
        bic     DAT1, DAT1, #0xff000000
        bic     DAT2, DAT2, #0xff000000
        bic     DAT3, DAT3, #0xff000000
        eorlslreg CHECK, DAT0, CHAN, 0 + (\channels - IDX2)
        eorlslreg CHECK, DAT1, CHAN, 1 + (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
        eorlslreg CHECK, DAT2, CHAN, 0 + (\channels - IDX2)
        eorlslreg CHECK, DAT3, CHAN, 1 + (\channels - IDX2)
   decr_modulo IDX2, 2, \channels
        lsl     DAT0, #8
        lsl     DAT1, #8
        lsl     DAT2, #8
        lsl     DAT3, #8
  .endif
        stm     OUT!, {DAT0 - DAT3}
 .endm

 .set WORDS_PER_LOOP, \channels  // calculate LCM (channels, 4)
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .if (WORDS_PER_LOOP % 2) == 0
  .set WORDS_PER_LOOP, WORDS_PER_LOOP / 2
 .endif
 .set WORDS_PER_LOOP, WORDS_PER_LOOP * 4
 .set SAMPLES_PER_LOOP, WORDS_PER_LOOP / \channels

function ff_mlp_pack_output_outoforder_\channels\()ch_\shift\()shift_armv6, export=1
 .if SAMPLES_PER_LOOP > 1
        tst     COUNT, #SAMPLES_PER_LOOP - 1  // always seems to be in practice
        it      ne
        bne     X(ff_mlp_pack_output)         // but just in case, branch to C implementation if not
 .endif
        teq     COUNT, #0
        it      eq
        bxeq    lr
        push    {v1-v6,sl,fp,lr}
        ldr     CHAN0, [sp, #(9+0)*4]  // get ch_assign from stack
        ldr     CHAN4, [CHAN0]
 .if \channels == 2
        uxtb    CHAN0, CHAN4, ror #0
        uxtb    CHAN1, CHAN4, ror #8
 .else
        ldr     CHAN5, [CHAN0, #4]
  .if \channels == 6
        uxtb    CHAN0, CHAN4, ror #0
        uxtb    CHAN1, CHAN4, ror #8
        uxtb    CHAN2, CHAN4, ror #16
        uxtb    CHAN3, CHAN4, ror #24
        uxtb    CHAN4, CHAN5, ror #0
        uxtb    CHAN5, CHAN5, ror #8
  .endif
 .endif
 .set IDX1, \channels
 .set IDX2, \channels
0:
 .rept WORDS_PER_LOOP / 4
        output4words
 .endr
        subs    COUNT, COUNT, #SAMPLES_PER_LOOP
        bne     0b
        pop     {v1-v6,sl,fp,pc}
        .ltorg
endfunc
 .purgem output4words

        .unreq  CHECK
        .unreq  COUNT
        .unreq  IN
        .unreq  OUT
        .unreq  DAT0
        .unreq  DAT1
        .unreq  DAT2
        .unreq  DAT3
        .unreq  CHAN0
        .unreq  CHAN1
        .unreq  CHAN2
        .unreq  CHAN3
        .unreq  CHAN4
        .unreq  CHAN5

#endif // !CONFIG_THUMB

.endif // mixed
.endif // inorder
.endm // implement_pack

.macro pack_channels  inorder, channels
        implement_pack  \inorder, \channels, 0
        implement_pack  \inorder, \channels, 1
        implement_pack  \inorder, \channels, 2
        implement_pack  \inorder, \channels, 3
        implement_pack  \inorder, \channels, 4
        implement_pack  \inorder, \channels, 5
        implement_pack  \inorder, \channels, mixed
.endm

.macro pack_order  inorder
        pack_channels  \inorder, 2
        pack_channels  \inorder, 6
        pack_channels  \inorder, 8
.endm

        pack_order  0
        pack_order  1
