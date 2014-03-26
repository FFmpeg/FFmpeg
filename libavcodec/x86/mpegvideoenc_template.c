/*
 * MPEG video MMX templates
 *
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdint.h>

#include "libavutil/internal.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/mpegvideo.h"

#undef MMREG_WIDTH
#undef MM
#undef MOVQ
#undef SPREADW
#undef PMAXW
#undef PMAX
#undef SAVE_SIGN
#undef RESTORE_SIGN

#if COMPILE_TEMPLATE_SSE2
#define MMREG_WIDTH "16"
#define MM "%%xmm"
#define MOVQ "movdqa"
#define SPREADW(a) \
            "pshuflw $0, "a", "a"       \n\t"\
            "punpcklwd "a", "a"         \n\t"
#define PMAXW(a,b) "pmaxsw "a", "b"     \n\t"
#define PMAX(a,b) \
            "movhlps "a", "b"           \n\t"\
            PMAXW(b, a)\
            "pshuflw $0x0E, "a", "b"    \n\t"\
            PMAXW(b, a)\
            "pshuflw $0x01, "a", "b"    \n\t"\
            PMAXW(b, a)
#else
#define MMREG_WIDTH "8"
#define MM "%%mm"
#define MOVQ "movq"
#if COMPILE_TEMPLATE_MMXEXT
#define SPREADW(a) "pshufw $0, "a", "a" \n\t"
#define PMAXW(a,b) "pmaxsw "a", "b"     \n\t"
#define PMAX(a,b) \
            "pshufw $0x0E, "a", "b"     \n\t"\
            PMAXW(b, a)\
            "pshufw $0x01, "a", "b"     \n\t"\
            PMAXW(b, a)
#else
#define SPREADW(a) \
            "punpcklwd "a", "a"         \n\t"\
            "punpcklwd "a", "a"         \n\t"
#define PMAXW(a,b) \
            "psubusw "a", "b"           \n\t"\
            "paddw "a", "b"             \n\t"
#define PMAX(a,b)  \
            "movq "a", "b"              \n\t"\
            "psrlq $32, "a"             \n\t"\
            PMAXW(b, a)\
            "movq "a", "b"              \n\t"\
            "psrlq $16, "a"             \n\t"\
            PMAXW(b, a)

#endif
#endif

#if COMPILE_TEMPLATE_SSSE3
#define SAVE_SIGN(a,b) \
            "movdqa "b", "a"            \n\t"\
            "pabsw  "b", "b"            \n\t"
#define RESTORE_SIGN(a,b) \
            "psignw "a", "b"            \n\t"
#else
#define SAVE_SIGN(a,b) \
            "pxor "a", "a"              \n\t"\
            "pcmpgtw "b", "a"           \n\t" /* block[i] <= 0 ? 0xFF : 0x00 */\
            "pxor "a", "b"              \n\t"\
            "psubw "a", "b"             \n\t" /* ABS(block[i]) */
#define RESTORE_SIGN(a,b) \
            "pxor "a", "b"              \n\t"\
            "psubw "a", "b"             \n\t" // out=((ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16)*sign(block[i])
#endif

static int RENAME(dct_quantize)(MpegEncContext *s,
                            int16_t *block, int n,
                            int qscale, int *overflow)
{
    x86_reg last_non_zero_p1;
    int level=0, q; //=0 is because gcc says uninitialized ...
    const uint16_t *qmat, *bias;
    LOCAL_ALIGNED_16(int16_t, temp_block, [64]);

    av_assert2((7&(int)(&temp_block[0])) == 0); //did gcc align it correctly?

    //s->fdct (block);
    RENAMEl(ff_fdct) (block); //cannot be anything else ...

    if(s->dct_error_sum)
        s->denoise_dct(s, block);

    if (s->mb_intra) {
        int dummy;
        if (n < 4){
            q = s->y_dc_scale;
            bias = s->q_intra_matrix16[qscale][1];
            qmat = s->q_intra_matrix16[qscale][0];
        }else{
            q = s->c_dc_scale;
            bias = s->q_chroma_intra_matrix16[qscale][1];
            qmat = s->q_chroma_intra_matrix16[qscale][0];
        }
        /* note: block[0] is assumed to be positive */
        if (!s->h263_aic) {
        __asm__ volatile (
                "mul %%ecx                \n\t"
                : "=d" (level), "=a"(dummy)
                : "a" ((block[0]>>2) + q), "c" (ff_inverse[q<<1])
        );
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            level = (block[0] + 4)>>3;

        block[0]=0; //avoid fake overflow
//        temp_block[0] = (block[0] + (q >> 1)) / q;
        last_non_zero_p1 = 1;
    } else {
        last_non_zero_p1 = 0;
        bias = s->q_inter_matrix16[qscale][1];
        qmat = s->q_inter_matrix16[qscale][0];
    }

    if((s->out_format == FMT_H263 || s->out_format == FMT_H261) && s->mpeg_quant==0){

        __asm__ volatile(
            "movd %%"REG_a", "MM"3              \n\t" // last_non_zero_p1
            SPREADW(MM"3")
            "pxor "MM"7, "MM"7                  \n\t" // 0
            "pxor "MM"4, "MM"4                  \n\t" // 0
            MOVQ" (%2), "MM"5                   \n\t" // qmat[0]
            "pxor "MM"6, "MM"6                  \n\t"
            "psubw (%3), "MM"6                  \n\t" // -bias[0]
            "mov $-128, %%"REG_a"               \n\t"
            ".p2align 4                         \n\t"
            "1:                                 \n\t"
            MOVQ" (%1, %%"REG_a"), "MM"0        \n\t" // block[i]
            SAVE_SIGN(MM"1", MM"0")                   // ABS(block[i])
            "psubusw "MM"6, "MM"0               \n\t" // ABS(block[i]) + bias[0]
            "pmulhw "MM"5, "MM"0                \n\t" // (ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16
            "por "MM"0, "MM"4                   \n\t"
            RESTORE_SIGN(MM"1", MM"0")                // out=((ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16)*sign(block[i])
            MOVQ" "MM"0, (%5, %%"REG_a")        \n\t"
            "pcmpeqw "MM"7, "MM"0               \n\t" // out==0 ? 0xFF : 0x00
            MOVQ" (%4, %%"REG_a"), "MM"1        \n\t"
            MOVQ" "MM"7, (%1, %%"REG_a")        \n\t" // 0
            "pandn "MM"1, "MM"0                 \n\t"
            PMAXW(MM"0", MM"3")
            "add $"MMREG_WIDTH", %%"REG_a"      \n\t"
            " js 1b                             \n\t"
            PMAX(MM"3", MM"0")
            "movd "MM"3, %%"REG_a"              \n\t"
            "movzbl %%al, %%eax                 \n\t" // last_non_zero_p1
            : "+a" (last_non_zero_p1)
            : "r" (block+64), "r" (qmat), "r" (bias),
              "r" (inv_zigzag_direct16 + 64), "r" (temp_block + 64)
              XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                "%xmm4", "%xmm5", "%xmm6", "%xmm7")
        );
    }else{ // FMT_H263
        __asm__ volatile(
            "movd %%"REG_a", "MM"3              \n\t" // last_non_zero_p1
            SPREADW(MM"3")
            "pxor "MM"7, "MM"7                  \n\t" // 0
            "pxor "MM"4, "MM"4                  \n\t" // 0
            "mov $-128, %%"REG_a"               \n\t"
            ".p2align 4                         \n\t"
            "1:                                 \n\t"
            MOVQ" (%1, %%"REG_a"), "MM"0        \n\t" // block[i]
            SAVE_SIGN(MM"1", MM"0")                   // ABS(block[i])
            MOVQ" (%3, %%"REG_a"), "MM"6        \n\t" // bias[0]
            "paddusw "MM"6, "MM"0               \n\t" // ABS(block[i]) + bias[0]
            MOVQ" (%2, %%"REG_a"), "MM"5        \n\t" // qmat[i]
            "pmulhw "MM"5, "MM"0                \n\t" // (ABS(block[i])*qmat[0] + bias[0]*qmat[0])>>16
            "por "MM"0, "MM"4                   \n\t"
            RESTORE_SIGN(MM"1", MM"0")                // out=((ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16)*sign(block[i])
            MOVQ" "MM"0, (%5, %%"REG_a")        \n\t"
            "pcmpeqw "MM"7, "MM"0               \n\t" // out==0 ? 0xFF : 0x00
            MOVQ" (%4, %%"REG_a"), "MM"1        \n\t"
            MOVQ" "MM"7, (%1, %%"REG_a")        \n\t" // 0
            "pandn "MM"1, "MM"0                 \n\t"
            PMAXW(MM"0", MM"3")
            "add $"MMREG_WIDTH", %%"REG_a"      \n\t"
            " js 1b                             \n\t"
            PMAX(MM"3", MM"0")
            "movd "MM"3, %%"REG_a"              \n\t"
            "movzbl %%al, %%eax                 \n\t" // last_non_zero_p1
            : "+a" (last_non_zero_p1)
            : "r" (block+64), "r" (qmat+64), "r" (bias+64),
              "r" (inv_zigzag_direct16 + 64), "r" (temp_block + 64)
              XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                "%xmm4", "%xmm5", "%xmm6", "%xmm7")
        );
    }
    __asm__ volatile(
        "movd %1, "MM"1                     \n\t" // max_qcoeff
        SPREADW(MM"1")
        "psubusw "MM"1, "MM"4               \n\t"
        "packuswb "MM"4, "MM"4              \n\t"
#if COMPILE_TEMPLATE_SSE2
        "packsswb "MM"4, "MM"4              \n\t"
#endif
        "movd "MM"4, %0                     \n\t" // *overflow
        : "=g" (*overflow)
        : "g" (s->max_qcoeff)
    );

    if(s->mb_intra) block[0]= level;
    else            block[0]= temp_block[0];

    if(s->dsp.idct_permutation_type == FF_SIMPLE_IDCT_PERM){
        if(last_non_zero_p1 <= 1) goto end;
        block[0x08] = temp_block[0x01]; block[0x10] = temp_block[0x08];
        block[0x20] = temp_block[0x10];
        if(last_non_zero_p1 <= 4) goto end;
        block[0x18] = temp_block[0x09]; block[0x04] = temp_block[0x02];
        block[0x09] = temp_block[0x03];
        if(last_non_zero_p1 <= 7) goto end;
        block[0x14] = temp_block[0x0A]; block[0x28] = temp_block[0x11];
        block[0x12] = temp_block[0x18]; block[0x02] = temp_block[0x20];
        if(last_non_zero_p1 <= 11) goto end;
        block[0x1A] = temp_block[0x19]; block[0x24] = temp_block[0x12];
        block[0x19] = temp_block[0x0B]; block[0x01] = temp_block[0x04];
        block[0x0C] = temp_block[0x05];
        if(last_non_zero_p1 <= 16) goto end;
        block[0x11] = temp_block[0x0C]; block[0x29] = temp_block[0x13];
        block[0x16] = temp_block[0x1A]; block[0x0A] = temp_block[0x21];
        block[0x30] = temp_block[0x28]; block[0x22] = temp_block[0x30];
        block[0x38] = temp_block[0x29]; block[0x06] = temp_block[0x22];
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1B] = temp_block[0x1B]; block[0x21] = temp_block[0x14];
        block[0x1C] = temp_block[0x0D]; block[0x05] = temp_block[0x06];
        block[0x0D] = temp_block[0x07]; block[0x15] = temp_block[0x0E];
        block[0x2C] = temp_block[0x15]; block[0x13] = temp_block[0x1C];
        if(last_non_zero_p1 <= 32) goto end;
        block[0x0B] = temp_block[0x23]; block[0x34] = temp_block[0x2A];
        block[0x2A] = temp_block[0x31]; block[0x32] = temp_block[0x38];
        block[0x3A] = temp_block[0x39]; block[0x26] = temp_block[0x32];
        block[0x39] = temp_block[0x2B]; block[0x03] = temp_block[0x24];
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1E] = temp_block[0x1D]; block[0x25] = temp_block[0x16];
        block[0x1D] = temp_block[0x0F]; block[0x2D] = temp_block[0x17];
        block[0x17] = temp_block[0x1E]; block[0x0E] = temp_block[0x25];
        block[0x31] = temp_block[0x2C]; block[0x2B] = temp_block[0x33];
        if(last_non_zero_p1 <= 48) goto end;
        block[0x36] = temp_block[0x3A]; block[0x3B] = temp_block[0x3B];
        block[0x23] = temp_block[0x34]; block[0x3C] = temp_block[0x2D];
        block[0x07] = temp_block[0x26]; block[0x1F] = temp_block[0x1F];
        block[0x0F] = temp_block[0x27]; block[0x35] = temp_block[0x2E];
        if(last_non_zero_p1 <= 56) goto end;
        block[0x2E] = temp_block[0x35]; block[0x33] = temp_block[0x3C];
        block[0x3E] = temp_block[0x3D]; block[0x27] = temp_block[0x36];
        block[0x3D] = temp_block[0x2F]; block[0x2F] = temp_block[0x37];
        block[0x37] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }else if(s->dsp.idct_permutation_type == FF_LIBMPEG2_IDCT_PERM){
        if(last_non_zero_p1 <= 1) goto end;
        block[0x04] = temp_block[0x01];
        block[0x08] = temp_block[0x08]; block[0x10] = temp_block[0x10];
        if(last_non_zero_p1 <= 4) goto end;
        block[0x0C] = temp_block[0x09]; block[0x01] = temp_block[0x02];
        block[0x05] = temp_block[0x03];
        if(last_non_zero_p1 <= 7) goto end;
        block[0x09] = temp_block[0x0A]; block[0x14] = temp_block[0x11];
        block[0x18] = temp_block[0x18]; block[0x20] = temp_block[0x20];
        if(last_non_zero_p1 <= 11) goto end;
        block[0x1C] = temp_block[0x19];
        block[0x11] = temp_block[0x12]; block[0x0D] = temp_block[0x0B];
        block[0x02] = temp_block[0x04]; block[0x06] = temp_block[0x05];
        if(last_non_zero_p1 <= 16) goto end;
        block[0x0A] = temp_block[0x0C]; block[0x15] = temp_block[0x13];
        block[0x19] = temp_block[0x1A]; block[0x24] = temp_block[0x21];
        block[0x28] = temp_block[0x28]; block[0x30] = temp_block[0x30];
        block[0x2C] = temp_block[0x29]; block[0x21] = temp_block[0x22];
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1D] = temp_block[0x1B]; block[0x12] = temp_block[0x14];
        block[0x0E] = temp_block[0x0D]; block[0x03] = temp_block[0x06];
        block[0x07] = temp_block[0x07]; block[0x0B] = temp_block[0x0E];
        block[0x16] = temp_block[0x15]; block[0x1A] = temp_block[0x1C];
        if(last_non_zero_p1 <= 32) goto end;
        block[0x25] = temp_block[0x23]; block[0x29] = temp_block[0x2A];
        block[0x34] = temp_block[0x31]; block[0x38] = temp_block[0x38];
        block[0x3C] = temp_block[0x39]; block[0x31] = temp_block[0x32];
        block[0x2D] = temp_block[0x2B]; block[0x22] = temp_block[0x24];
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1E] = temp_block[0x1D]; block[0x13] = temp_block[0x16];
        block[0x0F] = temp_block[0x0F]; block[0x17] = temp_block[0x17];
        block[0x1B] = temp_block[0x1E]; block[0x26] = temp_block[0x25];
        block[0x2A] = temp_block[0x2C]; block[0x35] = temp_block[0x33];
        if(last_non_zero_p1 <= 48) goto end;
        block[0x39] = temp_block[0x3A]; block[0x3D] = temp_block[0x3B];
        block[0x32] = temp_block[0x34]; block[0x2E] = temp_block[0x2D];
            block[0x23] = temp_block[0x26]; block[0x1F] = temp_block[0x1F];
        block[0x27] = temp_block[0x27]; block[0x2B] = temp_block[0x2E];
        if(last_non_zero_p1 <= 56) goto end;
        block[0x36] = temp_block[0x35]; block[0x3A] = temp_block[0x3C];
        block[0x3E] = temp_block[0x3D]; block[0x33] = temp_block[0x36];
        block[0x2F] = temp_block[0x2F]; block[0x37] = temp_block[0x37];
        block[0x3B] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }else{
        if(last_non_zero_p1 <= 1) goto end;
        block[0x01] = temp_block[0x01];
        block[0x08] = temp_block[0x08]; block[0x10] = temp_block[0x10];
        if(last_non_zero_p1 <= 4) goto end;
        block[0x09] = temp_block[0x09]; block[0x02] = temp_block[0x02];
        block[0x03] = temp_block[0x03];
        if(last_non_zero_p1 <= 7) goto end;
        block[0x0A] = temp_block[0x0A]; block[0x11] = temp_block[0x11];
        block[0x18] = temp_block[0x18]; block[0x20] = temp_block[0x20];
        if(last_non_zero_p1 <= 11) goto end;
        block[0x19] = temp_block[0x19];
        block[0x12] = temp_block[0x12]; block[0x0B] = temp_block[0x0B];
        block[0x04] = temp_block[0x04]; block[0x05] = temp_block[0x05];
        if(last_non_zero_p1 <= 16) goto end;
        block[0x0C] = temp_block[0x0C]; block[0x13] = temp_block[0x13];
        block[0x1A] = temp_block[0x1A]; block[0x21] = temp_block[0x21];
        block[0x28] = temp_block[0x28]; block[0x30] = temp_block[0x30];
        block[0x29] = temp_block[0x29]; block[0x22] = temp_block[0x22];
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1B] = temp_block[0x1B]; block[0x14] = temp_block[0x14];
        block[0x0D] = temp_block[0x0D]; block[0x06] = temp_block[0x06];
        block[0x07] = temp_block[0x07]; block[0x0E] = temp_block[0x0E];
        block[0x15] = temp_block[0x15]; block[0x1C] = temp_block[0x1C];
        if(last_non_zero_p1 <= 32) goto end;
        block[0x23] = temp_block[0x23]; block[0x2A] = temp_block[0x2A];
        block[0x31] = temp_block[0x31]; block[0x38] = temp_block[0x38];
        block[0x39] = temp_block[0x39]; block[0x32] = temp_block[0x32];
        block[0x2B] = temp_block[0x2B]; block[0x24] = temp_block[0x24];
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1D] = temp_block[0x1D]; block[0x16] = temp_block[0x16];
        block[0x0F] = temp_block[0x0F]; block[0x17] = temp_block[0x17];
        block[0x1E] = temp_block[0x1E]; block[0x25] = temp_block[0x25];
        block[0x2C] = temp_block[0x2C]; block[0x33] = temp_block[0x33];
        if(last_non_zero_p1 <= 48) goto end;
        block[0x3A] = temp_block[0x3A]; block[0x3B] = temp_block[0x3B];
        block[0x34] = temp_block[0x34]; block[0x2D] = temp_block[0x2D];
        block[0x26] = temp_block[0x26]; block[0x1F] = temp_block[0x1F];
        block[0x27] = temp_block[0x27]; block[0x2E] = temp_block[0x2E];
        if(last_non_zero_p1 <= 56) goto end;
        block[0x35] = temp_block[0x35]; block[0x3C] = temp_block[0x3C];
        block[0x3D] = temp_block[0x3D]; block[0x36] = temp_block[0x36];
        block[0x2F] = temp_block[0x2F]; block[0x37] = temp_block[0x37];
        block[0x3E] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }
    end:
    return last_non_zero_p1 - 1;
}
