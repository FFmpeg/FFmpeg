/*
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _DSPUTIL_PPC_
#define _DSPUTIL_PPC_

#ifdef CONFIG_DARWIN
/* The Apple assembler shipped w/ gcc-3.3 knows about DCBZL, previous assemblers don't
   We assume here that the Darwin GCC is from Apple.... */
#if (__GNUC__ * 100 + __GNUC_MINOR__ < 303)
#define NO_DCBZL
#endif
#else /* CONFIG_DARWIN */
/* I don't think any non-Apple assembler knows about DCBZL */
#define NO_DCBZL
#endif /* CONFIG_DARWIN */

#ifdef POWERPC_TBL_PERFORMANCE_REPORT
void powerpc_display_perf_report(void);
/* if you add to the enum below, also add to the perfname array
   in dsputil_ppc.c */
enum powerpc_perf_index {
  altivec_fft_num = 0,
  altivec_gmc1_num,
  altivec_dct_unquantize_h263_num,
  altivec_idct_add_num,
  altivec_idct_put_num,
  altivec_put_pixels16_num,
  altivec_avg_pixels16_num,
  altivec_avg_pixels8_num,
  altivec_put_pixels8_xy2_num,
  altivec_put_no_rnd_pixels8_xy2_num,
  altivec_put_pixels16_xy2_num,
  altivec_put_no_rnd_pixels16_xy2_num,
  powerpc_clear_blocks_dcbz32,
  powerpc_clear_blocks_dcbz128,
  powerpc_perf_total
};
enum powerpc_data_index {
  powerpc_data_min = 0,
  powerpc_data_max,
  powerpc_data_sum,
  powerpc_data_num,
  powerpc_data_total
};
extern unsigned long long perfdata[powerpc_perf_total][powerpc_data_total];
#ifdef POWERPC_PERF_USE_PMC
extern unsigned long long perfdata_pmc2[powerpc_perf_total][powerpc_data_total];
extern unsigned long long perfdata_pmc3[powerpc_perf_total][powerpc_data_total];
#endif

#ifndef POWERPC_PERF_USE_PMC
#define POWERPC_GET_CYCLES(a) asm volatile("mftb %0" : "=r" (a))
#define POWERPC_TBL_DECLARE(a, cond) register unsigned long tbl_start, tbl_stop
#define POWERPC_TBL_START_COUNT(a, cond) do { POWERPC_GET_CYCLES(tbl_start); } while (0)
#define POWERPC_TBL_STOP_COUNT(a, cond) do {     \
  POWERPC_GET_CYCLES(tbl_stop);                  \
  if (tbl_stop > tbl_start)                      \
  {                                              \
    unsigned long diff =  tbl_stop - tbl_start;  \
    if (cond)                                    \
    {                                            \
      if (diff < perfdata[a][powerpc_data_min])  \
        perfdata[a][powerpc_data_min] = diff;    \
      if (diff > perfdata[a][powerpc_data_max])  \
        perfdata[a][powerpc_data_max] = diff;    \
      perfdata[a][powerpc_data_sum] += diff;     \
      perfdata[a][powerpc_data_num] ++;          \
    }                                            \
  }                                              \
} while (0)

#else /* POWERPC_PERF_USE_PMC */
#define POWERPC_GET_CYCLES(a) asm volatile("mfspr %0, 937" : "=r" (a))
#define POWERPC_GET_PMC2(a) asm volatile("mfspr %0, 938" : "=r" (a))
#define POWERPC_GET_PMC3(a) asm volatile("mfspr %0, 941" : "=r" (a))
#define POWERPC_TBL_DECLARE(a, cond) register unsigned long cycles_start, cycles_stop, pmc2_start, pmc2_stop, pmc3_start, pmc3_stop
#define POWERPC_TBL_START_COUNT(a, cond) do {    \
  POWERPC_GET_PMC3(pmc3_start);                  \
  POWERPC_GET_PMC2(pmc2_start);                  \
  POWERPC_GET_CYCLES(cycles_start); } while (0)
#define POWERPC_TBL_STOP_COUNT(a, cond) do {     \
  POWERPC_GET_CYCLES(cycles_stop);               \
  POWERPC_GET_PMC2(pmc2_stop);                   \
  POWERPC_GET_PMC3(pmc3_stop);                   \
  if (cycles_stop >= cycles_start)               \
  {                                              \
    unsigned long diff =                         \
                cycles_stop - cycles_start;      \
    if (cond)                                    \
    {                                            \
      if (diff < perfdata[a][powerpc_data_min])  \
        perfdata[a][powerpc_data_min] = diff;    \
      if (diff > perfdata[a][powerpc_data_max])  \
        perfdata[a][powerpc_data_max] = diff;    \
      perfdata[a][powerpc_data_sum] += diff;     \
      perfdata[a][powerpc_data_num] ++;          \
    }                                            \
  }                                              \
  if (pmc2_stop >= pmc2_start)                   \
  {                                              \
    unsigned long diff =                         \
                pmc2_stop - pmc2_start;          \
    if (cond)                                    \
    {                                            \
      if (diff < perfdata_pmc2[a][powerpc_data_min]) \
        perfdata_pmc2[a][powerpc_data_min] = diff;   \
      if (diff > perfdata_pmc2[a][powerpc_data_max]) \
        perfdata_pmc2[a][powerpc_data_max] = diff;   \
      perfdata_pmc2[a][powerpc_data_sum] += diff;    \
      perfdata_pmc2[a][powerpc_data_num] ++;         \
    }                                            \
  }                                              \
  if (pmc3_stop >= pmc3_start)                   \
  {                                              \
    unsigned long diff =                         \
                pmc3_stop - pmc3_start;          \
    if (cond)                                    \
    {                                            \
      if (diff < perfdata_pmc3[a][powerpc_data_min]) \
        perfdata_pmc3[a][powerpc_data_min] = diff;   \
      if (diff > perfdata_pmc3[a][powerpc_data_max]) \
        perfdata_pmc3[a][powerpc_data_max] = diff;   \
      perfdata_pmc3[a][powerpc_data_sum] += diff;    \
      perfdata_pmc3[a][powerpc_data_num] ++;         \
    }                                            \
  }                                              \
} while (0)

#endif /* POWERPC_PERF_USE_PMC */


#else /* POWERPC_TBL_PERFORMANCE_REPORT */
// those are needed to avoid empty statements.
#define POWERPC_TBL_DECLARE(a, cond)        int altivec_placeholder __attribute__ ((unused))
#define POWERPC_TBL_START_COUNT(a, cond)    do {} while (0)
#define POWERPC_TBL_STOP_COUNT(a, cond)     do {} while (0)
#endif /* POWERPC_TBL_PERFORMANCE_REPORT */

#endif /*  _DSPUTIL_PPC_ */
