/*
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#ifndef _DSPUTIL_PPC_
#define _DSPUTIL_PPC_

#ifdef CONFIG_POWERPC_PERF
void powerpc_display_perf_report(void);
/* the 604* have 2, the G3* have 4, the G4s have 6,
   and the G5 are completely different (they MUST use
   POWERPC_MODE_64BITS, and let's hope all future 64 bis PPC
   will use the same PMCs... */
#define POWERPC_NUM_PMC_ENABLED 6
/* if you add to the enum below, also add to the perfname array
   in dsputil_ppc.c */
enum powerpc_perf_index {
  altivec_fft_num = 0,
  altivec_gmc1_num,
  altivec_dct_unquantize_h263_num,
  altivec_fdct,
  altivec_idct_add_num,
  altivec_idct_put_num,
  altivec_put_pixels16_num,
  altivec_avg_pixels16_num,
  altivec_avg_pixels8_num,
  altivec_put_pixels8_xy2_num,
  altivec_put_no_rnd_pixels8_xy2_num,
  altivec_put_pixels16_xy2_num,
  altivec_put_no_rnd_pixels16_xy2_num,
  altivec_hadamard8_diff8x8_num,
  altivec_hadamard8_diff16_num,
  altivec_avg_pixels8_xy2_num,
  powerpc_clear_blocks_dcbz32,
  powerpc_clear_blocks_dcbz128,
  altivec_put_h264_chroma_mc8_num,
  altivec_avg_h264_chroma_mc8_num,
  altivec_put_h264_qpel16_h_lowpass_num,
  altivec_avg_h264_qpel16_h_lowpass_num,
  altivec_put_h264_qpel16_v_lowpass_num,
  altivec_avg_h264_qpel16_v_lowpass_num,
  altivec_put_h264_qpel16_hv_lowpass_num,
  altivec_avg_h264_qpel16_hv_lowpass_num,
  powerpc_perf_total
};
enum powerpc_data_index {
  powerpc_data_min = 0,
  powerpc_data_max,
  powerpc_data_sum,
  powerpc_data_num,
  powerpc_data_total
};
extern unsigned long long perfdata[POWERPC_NUM_PMC_ENABLED][powerpc_perf_total][powerpc_data_total];

#ifndef POWERPC_MODE_64BITS
#define POWERP_PMC_DATATYPE unsigned long
#define POWERPC_GET_PMC1(a) asm volatile("mfspr %0, 937" : "=r" (a))
#define POWERPC_GET_PMC2(a) asm volatile("mfspr %0, 938" : "=r" (a))
#if (POWERPC_NUM_PMC_ENABLED > 2)
#define POWERPC_GET_PMC3(a) asm volatile("mfspr %0, 941" : "=r" (a))
#define POWERPC_GET_PMC4(a) asm volatile("mfspr %0, 942" : "=r" (a))
#else
#define POWERPC_GET_PMC3(a) do {} while (0)
#define POWERPC_GET_PMC4(a) do {} while (0)
#endif
#if (POWERPC_NUM_PMC_ENABLED > 4)
#define POWERPC_GET_PMC5(a) asm volatile("mfspr %0, 929" : "=r" (a))
#define POWERPC_GET_PMC6(a) asm volatile("mfspr %0, 930" : "=r" (a))
#else
#define POWERPC_GET_PMC5(a) do {} while (0)
#define POWERPC_GET_PMC6(a) do {} while (0)
#endif
#else /* POWERPC_MODE_64BITS */
#define POWERP_PMC_DATATYPE unsigned long long
#define POWERPC_GET_PMC1(a) asm volatile("mfspr %0, 771" : "=r" (a))
#define POWERPC_GET_PMC2(a) asm volatile("mfspr %0, 772" : "=r" (a))
#if (POWERPC_NUM_PMC_ENABLED > 2)
#define POWERPC_GET_PMC3(a) asm volatile("mfspr %0, 773" : "=r" (a))
#define POWERPC_GET_PMC4(a) asm volatile("mfspr %0, 774" : "=r" (a))
#else
#define POWERPC_GET_PMC3(a) do {} while (0)
#define POWERPC_GET_PMC4(a) do {} while (0)
#endif
#if (POWERPC_NUM_PMC_ENABLED > 4)
#define POWERPC_GET_PMC5(a) asm volatile("mfspr %0, 775" : "=r" (a))
#define POWERPC_GET_PMC6(a) asm volatile("mfspr %0, 776" : "=r" (a))
#else
#define POWERPC_GET_PMC5(a) do {} while (0)
#define POWERPC_GET_PMC6(a) do {} while (0)
#endif
#endif /* POWERPC_MODE_64BITS */
#define POWERPC_PERF_DECLARE(a, cond)   \
  POWERP_PMC_DATATYPE                   \
    pmc_start[POWERPC_NUM_PMC_ENABLED], \
    pmc_stop[POWERPC_NUM_PMC_ENABLED],  \
    pmc_loop_index;
#define POWERPC_PERF_START_COUNT(a, cond) do { \
  POWERPC_GET_PMC6(pmc_start[5]); \
  POWERPC_GET_PMC5(pmc_start[4]); \
  POWERPC_GET_PMC4(pmc_start[3]); \
  POWERPC_GET_PMC3(pmc_start[2]); \
  POWERPC_GET_PMC2(pmc_start[1]); \
  POWERPC_GET_PMC1(pmc_start[0]); \
  } while (0)
#define POWERPC_PERF_STOP_COUNT(a, cond) do { \
  POWERPC_GET_PMC1(pmc_stop[0]); \
  POWERPC_GET_PMC2(pmc_stop[1]); \
  POWERPC_GET_PMC3(pmc_stop[2]); \
  POWERPC_GET_PMC4(pmc_stop[3]); \
  POWERPC_GET_PMC5(pmc_stop[4]); \
  POWERPC_GET_PMC6(pmc_stop[5]); \
  if (cond)                       \
  {                               \
    for(pmc_loop_index = 0;       \
        pmc_loop_index < POWERPC_NUM_PMC_ENABLED; \
        pmc_loop_index++)         \
    {                             \
      if (pmc_stop[pmc_loop_index] >= pmc_start[pmc_loop_index])  \
        {                                                         \
        POWERP_PMC_DATATYPE diff =                                \
          pmc_stop[pmc_loop_index] - pmc_start[pmc_loop_index];   \
        if (diff < perfdata[pmc_loop_index][a][powerpc_data_min]) \
          perfdata[pmc_loop_index][a][powerpc_data_min] = diff;   \
        if (diff > perfdata[pmc_loop_index][a][powerpc_data_max]) \
          perfdata[pmc_loop_index][a][powerpc_data_max] = diff;   \
        perfdata[pmc_loop_index][a][powerpc_data_sum] += diff;    \
        perfdata[pmc_loop_index][a][powerpc_data_num] ++;         \
      }                           \
    }                             \
  }                               \
} while (0)
#else /* CONFIG_POWERPC_PERF */
// those are needed to avoid empty statements.
#define POWERPC_PERF_DECLARE(a, cond)        int altivec_placeholder __attribute__ ((unused))
#define POWERPC_PERF_START_COUNT(a, cond)    do {} while (0)
#define POWERPC_PERF_STOP_COUNT(a, cond)     do {} while (0)
#endif /* CONFIG_POWERPC_PERF */

#endif /*  _DSPUTIL_PPC_ */
