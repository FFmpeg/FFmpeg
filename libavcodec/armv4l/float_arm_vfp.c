/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "libavcodec/dsputil.h"

/*
 * VFP is a floating point coprocessor used in some ARM cores. VFP11 has 1 cycle
 * throughput for almost all the instructions (except for double precision
 * arithmetics), but rather high latency. Latency is 4 cycles for loads and 8 cycles
 * for arithmetic operations. Scheduling code to avoid pipeline stalls is very
 * important for performance. One more interesting feature is that VFP has
 * independent load/store and arithmetics pipelines, so it is possible to make
 * them work simultaneously and get more than 1 operation per cycle. Load/store
 * pipeline can process 2 single precision floating point values per cycle and
 * supports bulk loads and stores for large sets of registers. Arithmetic operations
 * can be done on vectors, which allows to keep the arithmetics pipeline busy,
 * while the processor may issue and execute other instructions. Detailed
 * optimization manuals can be found at http://www.arm.com
 */

/**
 * ARM VFP optimized implementation of 'vector_fmul_c' function.
 * Assume that len is a positive number and is multiple of 8
 */
static void vector_fmul_vfp(float *dst, const float *src, int len)
{
    int tmp;
    __asm__ volatile(
        "fmrx       %[tmp], fpscr\n\t"
        "orr        %[tmp], %[tmp], #(3 << 16)\n\t" /* set vector size to 4 */
        "fmxr       fpscr, %[tmp]\n\t"

        "fldmias    %[dst_r]!, {s0-s3}\n\t"
        "fldmias    %[src]!, {s8-s11}\n\t"
        "fldmias    %[dst_r]!, {s4-s7}\n\t"
        "fldmias    %[src]!, {s12-s15}\n\t"
        "fmuls      s8, s0, s8\n\t"
    "1:\n\t"
        "subs       %[len], %[len], #16\n\t"
        "fmuls      s12, s4, s12\n\t"
        "fldmiasge  %[dst_r]!, {s16-s19}\n\t"
        "fldmiasge  %[src]!, {s24-s27}\n\t"
        "fldmiasge  %[dst_r]!, {s20-s23}\n\t"
        "fldmiasge  %[src]!, {s28-s31}\n\t"
        "fmulsge    s24, s16, s24\n\t"
        "fstmias    %[dst_w]!, {s8-s11}\n\t"
        "fstmias    %[dst_w]!, {s12-s15}\n\t"
        "fmulsge    s28, s20, s28\n\t"
        "fldmiasgt  %[dst_r]!, {s0-s3}\n\t"
        "fldmiasgt  %[src]!, {s8-s11}\n\t"
        "fldmiasgt  %[dst_r]!, {s4-s7}\n\t"
        "fldmiasgt  %[src]!, {s12-s15}\n\t"
        "fmulsge    s8, s0, s8\n\t"
        "fstmiasge  %[dst_w]!, {s24-s27}\n\t"
        "fstmiasge  %[dst_w]!, {s28-s31}\n\t"
        "bgt        1b\n\t"

        "bic        %[tmp], %[tmp], #(7 << 16)\n\t" /* set vector size back to 1 */
        "fmxr       fpscr, %[tmp]\n\t"
        : [dst_w] "+&r" (dst), [dst_r] "+&r" (dst), [src] "+&r" (src), [len] "+&r" (len), [tmp] "=&r" (tmp)
        :
        : "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
          "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15",
          "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
          "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",
          "cc", "memory");
}

/**
 * ARM VFP optimized implementation of 'vector_fmul_reverse_c' function.
 * Assume that len is a positive number and is multiple of 8
 */
static void vector_fmul_reverse_vfp(float *dst, const float *src0, const float *src1, int len)
{
    src1 += len;
    __asm__ volatile(
        "fldmdbs    %[src1]!, {s0-s3}\n\t"
        "fldmias    %[src0]!, {s8-s11}\n\t"
        "fldmdbs    %[src1]!, {s4-s7}\n\t"
        "fldmias    %[src0]!, {s12-s15}\n\t"
        "fmuls      s8, s3, s8\n\t"
        "fmuls      s9, s2, s9\n\t"
        "fmuls      s10, s1, s10\n\t"
        "fmuls      s11, s0, s11\n\t"
    "1:\n\t"
        "subs       %[len], %[len], #16\n\t"
        "fldmdbsge  %[src1]!, {s16-s19}\n\t"
        "fmuls      s12, s7, s12\n\t"
        "fldmiasge  %[src0]!, {s24-s27}\n\t"
        "fmuls      s13, s6, s13\n\t"
        "fldmdbsge  %[src1]!, {s20-s23}\n\t"
        "fmuls      s14, s5, s14\n\t"
        "fldmiasge  %[src0]!, {s28-s31}\n\t"
        "fmuls      s15, s4, s15\n\t"
        "fmulsge    s24, s19, s24\n\t"
        "fldmdbsgt  %[src1]!, {s0-s3}\n\t"
        "fmulsge    s25, s18, s25\n\t"
        "fstmias    %[dst]!, {s8-s13}\n\t"
        "fmulsge    s26, s17, s26\n\t"
        "fldmiasgt  %[src0]!, {s8-s11}\n\t"
        "fmulsge    s27, s16, s27\n\t"
        "fmulsge    s28, s23, s28\n\t"
        "fldmdbsgt  %[src1]!, {s4-s7}\n\t"
        "fmulsge    s29, s22, s29\n\t"
        "fstmias    %[dst]!, {s14-s15}\n\t"
        "fmulsge    s30, s21, s30\n\t"
        "fmulsge    s31, s20, s31\n\t"
        "fmulsge    s8, s3, s8\n\t"
        "fldmiasgt  %[src0]!, {s12-s15}\n\t"
        "fmulsge    s9, s2, s9\n\t"
        "fmulsge    s10, s1, s10\n\t"
        "fstmiasge  %[dst]!, {s24-s27}\n\t"
        "fmulsge    s11, s0, s11\n\t"
        "fstmiasge  %[dst]!, {s28-s31}\n\t"
        "bgt        1b\n\t"

        : [dst] "+&r" (dst), [src0] "+&r" (src0), [src1] "+&r" (src1), [len] "+&r" (len)
        :
        : "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
          "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15",
          "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
          "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",
          "cc", "memory");
}

#ifdef HAVE_ARMV6
/**
 * ARM VFP optimized float to int16 conversion.
 * Assume that len is a positive number and is multiple of 8, destination
 * buffer is at least 4 bytes aligned (8 bytes alignment is better for
 * performance), little endian byte sex
 */
void float_to_int16_vfp(int16_t *dst, const float *src, int len)
{
    __asm__ volatile(
        "fldmias    %[src]!, {s16-s23}\n\t"
        "ftosis     s0, s16\n\t"
        "ftosis     s1, s17\n\t"
        "ftosis     s2, s18\n\t"
        "ftosis     s3, s19\n\t"
        "ftosis     s4, s20\n\t"
        "ftosis     s5, s21\n\t"
        "ftosis     s6, s22\n\t"
        "ftosis     s7, s23\n\t"
    "1:\n\t"
        "subs       %[len], %[len], #8\n\t"
        "fmrrs      r3, r4, {s0, s1}\n\t"
        "fmrrs      r5, r6, {s2, s3}\n\t"
        "fmrrs      r7, r8, {s4, s5}\n\t"
        "fmrrs      ip, lr, {s6, s7}\n\t"
        "fldmiasgt  %[src]!, {s16-s23}\n\t"
        "ssat       r4, #16, r4\n\t"
        "ssat       r3, #16, r3\n\t"
        "ssat       r6, #16, r6\n\t"
        "ssat       r5, #16, r5\n\t"
        "pkhbt      r3, r3, r4, lsl #16\n\t"
        "pkhbt      r4, r5, r6, lsl #16\n\t"
        "ftosisgt   s0, s16\n\t"
        "ftosisgt   s1, s17\n\t"
        "ftosisgt   s2, s18\n\t"
        "ftosisgt   s3, s19\n\t"
        "ftosisgt   s4, s20\n\t"
        "ftosisgt   s5, s21\n\t"
        "ftosisgt   s6, s22\n\t"
        "ftosisgt   s7, s23\n\t"
        "ssat       r8, #16, r8\n\t"
        "ssat       r7, #16, r7\n\t"
        "ssat       lr, #16, lr\n\t"
        "ssat       ip, #16, ip\n\t"
        "pkhbt      r5, r7, r8, lsl #16\n\t"
        "pkhbt      r6, ip, lr, lsl #16\n\t"
        "stmia      %[dst]!, {r3-r6}\n\t"
        "bgt        1b\n\t"

        : [dst] "+&r" (dst), [src] "+&r" (src), [len] "+&r" (len)
        :
        : "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
          "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
          "r3", "r4", "r5", "r6", "r7", "r8", "ip", "lr",
          "cc", "memory");
}
#endif

void ff_float_init_arm_vfp(DSPContext* c, AVCodecContext *avctx)
{
    c->vector_fmul = vector_fmul_vfp;
    c->vector_fmul_reverse = vector_fmul_reverse_vfp;
#ifdef HAVE_ARMV6
    c->float_to_int16 = float_to_int16_vfp;
#endif
}
