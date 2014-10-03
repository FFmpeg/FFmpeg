/*
 * MLP DSP functions x86-optimized
 * Copyright (c) 2009 Ramiro Polla
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/mlpdsp.h"
#include "libavcodec/mlp.h"

#define REMATRIX_CHANNEL_FUNC(opt) \
void ff_mlp_rematrix_channel_##opt(int32_t *samples, \
                                   const int32_t *coeffs, \
                                   const uint8_t *bypassed_lsbs, \
                                   const int8_t *noise_buffer, \
                                   int index, \
                                   unsigned int dest_ch, \
                                   uint16_t blockpos, \
                                   unsigned int maxchan, \
                                   int matrix_noise_shift, \
                                   int access_unit_size_pow2, \
                                   int32_t mask);

REMATRIX_CHANNEL_FUNC(sse4)
REMATRIX_CHANNEL_FUNC(avx2_bmi2)

#if HAVE_7REGS && HAVE_INLINE_ASM && HAVE_INLINE_ASM_NONLOCAL_LABELS

extern char ff_mlp_firorder_8;
extern char ff_mlp_firorder_7;
extern char ff_mlp_firorder_6;
extern char ff_mlp_firorder_5;
extern char ff_mlp_firorder_4;
extern char ff_mlp_firorder_3;
extern char ff_mlp_firorder_2;
extern char ff_mlp_firorder_1;
extern char ff_mlp_firorder_0;

extern char ff_mlp_iirorder_4;
extern char ff_mlp_iirorder_3;
extern char ff_mlp_iirorder_2;
extern char ff_mlp_iirorder_1;
extern char ff_mlp_iirorder_0;

static const void * const firtable[9] = { &ff_mlp_firorder_0, &ff_mlp_firorder_1,
                                   &ff_mlp_firorder_2, &ff_mlp_firorder_3,
                                   &ff_mlp_firorder_4, &ff_mlp_firorder_5,
                                   &ff_mlp_firorder_6, &ff_mlp_firorder_7,
                                   &ff_mlp_firorder_8 };
static const void * const iirtable[5] = { &ff_mlp_iirorder_0, &ff_mlp_iirorder_1,
                                   &ff_mlp_iirorder_2, &ff_mlp_iirorder_3,
                                   &ff_mlp_iirorder_4 };

#if ARCH_X86_64

#define MLPMUL(label, offset, offs, offc)   \
    LABEL_MANGLE(label)":             \n\t" \
    "movslq "offset"+"offs"(%0), %%rax\n\t" \
    "movslq "offset"+"offc"(%1), %%rdx\n\t" \
    "imul                 %%rdx, %%rax\n\t" \
    "add                  %%rax, %%rsi\n\t"

#define FIRMULREG(label, offset, firc)\
    LABEL_MANGLE(label)":       \n\t" \
    "movslq "#offset"(%0), %%rax\n\t" \
    "imul        %"#firc", %%rax\n\t" \
    "add            %%rax, %%rsi\n\t"

#define CLEAR_ACCUM                   \
    "xor            %%rsi, %%rsi\n\t"

#define SHIFT_ACCUM                   \
    "shr     %%cl,         %%rsi\n\t"

#define ACCUM    "%%rdx"
#define RESULT   "%%rsi"
#define RESULT32 "%%esi"

#else /* if ARCH_X86_32 */

#define MLPMUL(label, offset, offs, offc)  \
    LABEL_MANGLE(label)":            \n\t" \
    "mov   "offset"+"offs"(%0), %%eax\n\t" \
    "imull "offset"+"offc"(%1)       \n\t" \
    "add                %%eax , %%esi\n\t" \
    "adc                %%edx , %%ecx\n\t"

#define FIRMULREG(label, offset, firc)  \
    MLPMUL(label, #offset, "0", "0")

#define CLEAR_ACCUM                  \
    "xor           %%esi, %%esi\n\t" \
    "xor           %%ecx, %%ecx\n\t"

#define SHIFT_ACCUM                  \
    "mov           %%ecx, %%edx\n\t" \
    "mov           %%esi, %%eax\n\t" \
    "movzbl        %7   , %%ecx\n\t" \
    "shrd    %%cl, %%edx, %%eax\n\t" \

#define ACCUM    "%%edx"
#define RESULT   "%%eax"
#define RESULT32 "%%eax"

#endif /* !ARCH_X86_64 */

#define BINC  AV_STRINGIFY(4* MAX_CHANNELS)
#define IOFFS AV_STRINGIFY(4*(MAX_FIR_ORDER + MAX_BLOCKSIZE))
#define IOFFC AV_STRINGIFY(4* MAX_FIR_ORDER)

#define FIRMUL(label, offset) MLPMUL(label, #offset,   "0",   "0")
#define IIRMUL(label, offset) MLPMUL(label, #offset, IOFFS, IOFFC)

static void mlp_filter_channel_x86(int32_t *state, const int32_t *coeff,
                                   int firorder, int iirorder,
                                   unsigned int filter_shift, int32_t mask,
                                   int blocksize, int32_t *sample_buffer)
{
    const void *firjump = firtable[firorder];
    const void *iirjump = iirtable[iirorder];

    blocksize = -blocksize;

    __asm__ volatile(
        "1:                           \n\t"
        CLEAR_ACCUM
        "jmp  *%5                     \n\t"
        FIRMUL   (ff_mlp_firorder_8, 0x1c   )
        FIRMUL   (ff_mlp_firorder_7, 0x18   )
        FIRMUL   (ff_mlp_firorder_6, 0x14   )
        FIRMUL   (ff_mlp_firorder_5, 0x10   )
        FIRMUL   (ff_mlp_firorder_4, 0x0c   )
        FIRMULREG(ff_mlp_firorder_3, 0x08,10)
        FIRMULREG(ff_mlp_firorder_2, 0x04, 9)
        FIRMULREG(ff_mlp_firorder_1, 0x00, 8)
        LABEL_MANGLE(ff_mlp_firorder_0)":\n\t"
        "jmp  *%6                     \n\t"
        IIRMUL   (ff_mlp_iirorder_4, 0x0c   )
        IIRMUL   (ff_mlp_iirorder_3, 0x08   )
        IIRMUL   (ff_mlp_iirorder_2, 0x04   )
        IIRMUL   (ff_mlp_iirorder_1, 0x00   )
        LABEL_MANGLE(ff_mlp_iirorder_0)":\n\t"
        SHIFT_ACCUM
        "mov  "RESULT"  ,"ACCUM"      \n\t"
        "add  (%2)      ,"RESULT"     \n\t"
        "and   %4       ,"RESULT"     \n\t"
        "sub   $4       ,  %0         \n\t"
        "mov  "RESULT32", (%0)        \n\t"
        "mov  "RESULT32", (%2)        \n\t"
        "add $"BINC"    ,  %2         \n\t"
        "sub  "ACCUM"   ,"RESULT"     \n\t"
        "mov  "RESULT32","IOFFS"(%0)  \n\t"
        "incl              %3         \n\t"
        "js 1b                        \n\t"
        : /* 0*/"+r"(state),
          /* 1*/"+r"(coeff),
          /* 2*/"+r"(sample_buffer),
#if ARCH_X86_64
          /* 3*/"+r"(blocksize)
        : /* 4*/"r"((x86_reg)mask), /* 5*/"r"(firjump),
          /* 6*/"r"(iirjump)      , /* 7*/"c"(filter_shift)
        , /* 8*/"r"((int64_t)coeff[0])
        , /* 9*/"r"((int64_t)coeff[1])
        , /*10*/"r"((int64_t)coeff[2])
        : "rax", "rdx", "rsi"
#else /* ARCH_X86_32 */
          /* 3*/"+m"(blocksize)
        : /* 4*/"m"(         mask), /* 5*/"m"(firjump),
          /* 6*/"m"(iirjump)      , /* 7*/"m"(filter_shift)
        : "eax", "edx", "esi", "ecx"
#endif /* !ARCH_X86_64 */
    );
}

#endif /* HAVE_7REGS && HAVE_INLINE_ASM */

av_cold void ff_mlpdsp_init_x86(MLPDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();
#if HAVE_7REGS && HAVE_INLINE_ASM && HAVE_INLINE_ASM_NONLOCAL_LABELS
    if (INLINE_MMX(cpu_flags))
        c->mlp_filter_channel = mlp_filter_channel_x86;
#endif
    if (ARCH_X86_64 && EXTERNAL_SSE4(cpu_flags))
        c->mlp_rematrix_channel = ff_mlp_rematrix_channel_sse4;
    if (ARCH_X86_64 && EXTERNAL_AVX2(cpu_flags) && cpu_flags & AV_CPU_FLAG_BMI2)
        c->mlp_rematrix_channel = ff_mlp_rematrix_channel_avx2_bmi2;
}
