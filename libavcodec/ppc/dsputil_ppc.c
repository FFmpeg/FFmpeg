/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
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

#include "../dsputil.h"

#include "dsputil_ppc.h"

#ifdef HAVE_ALTIVEC
#include "dsputil_altivec.h"
#endif

extern void idct_put_altivec(uint8_t *dest, int line_size, int16_t *block);
extern void idct_add_altivec(uint8_t *dest, int line_size, int16_t *block);

int mm_flags = 0;

int mm_support(void)
{
    int result = 0;
#if HAVE_ALTIVEC
    if (has_altivec()) {
        result |= MM_ALTIVEC;
    }
#endif /* result */
    return result;
}

#ifdef POWERPC_TBL_PERFORMANCE_REPORT
unsigned long long perfdata[powerpc_perf_total][powerpc_data_total];
/* list below must match enum in dsputil_ppc.h */
static unsigned char* perfname[] = {
  "fft_calc_altivec",
  "gmc1_altivec",
  "dct_unquantize_h263_altivec",
  "idct_add_altivec",
  "idct_put_altivec",
  "put_pixels16_altivec",
  "avg_pixels16_altivec",
  "avg_pixels8_altivec",
  "put_pixels8_xy2_altivec",
  "put_no_rnd_pixels8_xy2_altivec",
  "put_pixels16_xy2_altivec",
  "put_no_rnd_pixels16_xy2_altivec",
  "clear_blocks_dcbz32_ppc"
};
#ifdef POWERPC_PERF_USE_PMC
unsigned long long perfdata_miss[powerpc_perf_total][powerpc_data_total];
#endif
#include <stdio.h>
#endif

#ifdef POWERPC_TBL_PERFORMANCE_REPORT
void powerpc_display_perf_report(void)
{
  int i;
#ifndef POWERPC_PERF_USE_PMC
  fprintf(stderr, "PowerPC performance report\n Values are from the Time Base register, and represent 4 bus cycles.\n");
#else /* POWERPC_PERF_USE_PMC */
  fprintf(stderr, "PowerPC performance report\n Values are from the PMC registers, and represent whatever the registers are set to record.\n");
#endif /* POWERPC_PERF_USE_PMC */
  for(i = 0 ; i < powerpc_perf_total ; i++)
  {
    if (perfdata[i][powerpc_data_num] != (unsigned long long)0)
      fprintf(stderr, " Function \"%s\" (pmc1):\n\tmin: %llu\n\tmax: %llu\n\tavg: %1.2lf (%llu)\n",
              perfname[i],
              perfdata[i][powerpc_data_min],
              perfdata[i][powerpc_data_max],
              (double)perfdata[i][powerpc_data_sum] /
              (double)perfdata[i][powerpc_data_num],
              perfdata[i][powerpc_data_num]);
#ifdef POWERPC_PERF_USE_PMC
    if (perfdata_miss[i][powerpc_data_num] != (unsigned long long)0)
      fprintf(stderr, " Function \"%s\" (pmc2):\n\tmin: %llu\n\tmax: %llu\n\tavg: %1.2lf (%llu)\n",
              perfname[i],
              perfdata_miss[i][powerpc_data_min],
              perfdata_miss[i][powerpc_data_max],
              (double)perfdata_miss[i][powerpc_data_sum] /
              (double)perfdata_miss[i][powerpc_data_num],
              perfdata_miss[i][powerpc_data_num]);
#endif
  }
}
#endif /* POWERPC_TBL_PERFORMANCE_REPORT */

/* ***** WARNING ***** WARNING ***** WARNING ***** */
/*
  clear_blocks_dcbz32_ppc will not work properly
  on PowerPC processors with a cache line size
  not equal to 32 bytes.
  Fortunately all processor used by Apple up to
  at least the 7450 (aka second generation G4)
  use 32 bytes cache line.
  This is due to the use of the 'dcbz' instruction.
  It simply clear to zero a single cache line,
  so you need to know the cache line size to use it !
  It's absurd, but it's fast...
*/
void clear_blocks_dcbz32_ppc(DCTELEM *blocks)
{
POWERPC_TBL_DECLARE(powerpc_clear_blocks_dcbz32, 1);
    register int misal = ((unsigned long)blocks & 0x00000010);
    register int i = 0;
POWERPC_TBL_START_COUNT(powerpc_clear_blocks_dcbz32, 1);
#if 1
    if (misal) {
      ((unsigned long*)blocks)[0] = 0L;
      ((unsigned long*)blocks)[1] = 0L;
      ((unsigned long*)blocks)[2] = 0L;
      ((unsigned long*)blocks)[3] = 0L;
      i += 16;
    }
    for ( ; i < sizeof(DCTELEM)*6*64 ; i += 32) {
      asm volatile("dcbz %0,%1" : : "r" (blocks), "r" (i) : "memory");
    }
    if (misal) {
      ((unsigned long*)blocks)[188] = 0L;
      ((unsigned long*)blocks)[189] = 0L;
      ((unsigned long*)blocks)[190] = 0L;
      ((unsigned long*)blocks)[191] = 0L;
      i += 16;
    }
#else
    memset(blocks, 0, sizeof(DCTELEM)*6*64);
#endif
POWERPC_TBL_STOP_COUNT(powerpc_clear_blocks_dcbz32, 1);
}

/* check dcbz report how many bytes are set to 0 by dcbz */
long check_dcbz_effect(void)
{
  register char *fakedata = (char*)av_malloc(1024);
  register char *fakedata_middle;
  register long zero = 0;
  register long i = 0;
  long count = 0;

  if (!fakedata)
  {
    return 0L;
  }

  fakedata_middle = (fakedata + 512);

  memset(fakedata, 0xFF, 1024);

  asm volatile("dcbz %0, %1" : : "r" (fakedata_middle), "r" (zero));

  for (i = 0; i < 1024 ; i ++)
  {
    if (fakedata[i] == (char)0)
      count++;
  }

  av_free(fakedata);
  
  return count;
}

void dsputil_init_ppc(DSPContext* c, AVCodecContext *avctx)
{
    // Common optimisations whether Altivec or not

  switch (check_dcbz_effect()) {
  case 32:
    c->clear_blocks = clear_blocks_dcbz32_ppc;
    break;
  default:
    break;
  }
  
#if HAVE_ALTIVEC
    if (has_altivec()) {
        mm_flags |= MM_ALTIVEC;
        
        // Altivec specific optimisations
        c->pix_abs16x16_x2 = pix_abs16x16_x2_altivec;
        c->pix_abs16x16_y2 = pix_abs16x16_y2_altivec;
        c->pix_abs16x16_xy2 = pix_abs16x16_xy2_altivec;
        c->pix_abs16x16 = pix_abs16x16_altivec;
        c->pix_abs8x8 = pix_abs8x8_altivec;
        c->sad[0]= sad16x16_altivec;
        c->sad[1]= sad8x8_altivec;
        c->pix_norm1 = pix_norm1_altivec;
        c->sse[1]= sse8_altivec;
        c->sse[0]= sse16_altivec;
        c->pix_sum = pix_sum_altivec;
        c->diff_pixels = diff_pixels_altivec;
        c->get_pixels = get_pixels_altivec;
// next one disabled as it's untested.
#if 0
        c->add_bytes= add_bytes_altivec;
#endif /* 0 */
        c->put_pixels_tab[0][0] = put_pixels16_altivec;
        c->avg_pixels_tab[0][0] = avg_pixels16_altivec;
// next one disabled as it's untested.
#if 0
        c->avg_pixels_tab[1][0] = avg_pixels8_altivec;
#endif /* 0 */
        c->put_pixels_tab[1][3] = put_pixels8_xy2_altivec;
        c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_altivec;
        c->put_pixels_tab[0][3] = put_pixels16_xy2_altivec;
        c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_altivec;
        
	c->gmc1 = gmc1_altivec;

        if ((avctx->idct_algo == FF_IDCT_AUTO) ||
                (avctx->idct_algo == FF_IDCT_ALTIVEC))
        {
            c->idct_put = idct_put_altivec;
            c->idct_add = idct_add_altivec;
#ifndef ALTIVEC_USE_REFERENCE_C_CODE
            c->idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
#else /* ALTIVEC_USE_REFERENCE_C_CODE */
            c->idct_permutation_type = FF_NO_IDCT_PERM;
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
        }
        
#ifdef POWERPC_TBL_PERFORMANCE_REPORT
        {
          int i;
          for (i = 0 ; i < powerpc_perf_total ; i++)
          {
            perfdata[i][powerpc_data_min] = 0xFFFFFFFFFFFFFFFF;
            perfdata[i][powerpc_data_max] = 0x0000000000000000;
            perfdata[i][powerpc_data_sum] = 0x0000000000000000;
            perfdata[i][powerpc_data_num] = 0x0000000000000000;
#ifdef POWERPC_PERF_USE_PMC
            perfdata_miss[i][powerpc_data_min] = 0xFFFFFFFFFFFFFFFF;
            perfdata_miss[i][powerpc_data_max] = 0x0000000000000000;
            perfdata_miss[i][powerpc_data_sum] = 0x0000000000000000;
            perfdata_miss[i][powerpc_data_num] = 0x0000000000000000;
#endif /* POWERPC_PERF_USE_PMC */
          }
        }
#endif /* POWERPC_TBL_PERFORMANCE_REPORT */
    } else
#endif /* HAVE_ALTIVEC */
    {
        // Non-AltiVec PPC optimisations

        // ... pending ...
    }
}
