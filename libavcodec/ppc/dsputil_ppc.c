/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

extern void fdct_altivec(int16_t *block);
extern void idct_put_altivec(uint8_t *dest, int line_size, int16_t *block);
extern void idct_add_altivec(uint8_t *dest, int line_size, int16_t *block);

int mm_flags = 0;

int mm_support(void)
{
    int result = 0;
#ifdef HAVE_ALTIVEC
    if (has_altivec()) {
        result |= MM_ALTIVEC;
    }
#endif /* result */
    return result;
}

#ifdef POWERPC_PERFORMANCE_REPORT
unsigned long long perfdata[POWERPC_NUM_PMC_ENABLED][powerpc_perf_total][powerpc_data_total];
/* list below must match enum in dsputil_ppc.h */
static unsigned char* perfname[] = {
  "ff_fft_calc_altivec",
  "gmc1_altivec",
  "dct_unquantize_h263_altivec",
  "fdct_altivec",
  "idct_add_altivec",
  "idct_put_altivec",
  "put_pixels16_altivec",
  "avg_pixels16_altivec",
  "avg_pixels8_altivec",
  "put_pixels8_xy2_altivec",
  "put_no_rnd_pixels8_xy2_altivec",
  "put_pixels16_xy2_altivec",
  "put_no_rnd_pixels16_xy2_altivec",
  "hadamard8_diff8x8_altivec",
  "hadamard8_diff16_altivec",
  "avg_pixels8_xy2_altivec",
  "clear_blocks_dcbz32_ppc",
  "clear_blocks_dcbz128_ppc",
  "put_h264_chroma_mc8_altivec",
  "avg_h264_chroma_mc8_altivec",
  "put_h264_qpel16_h_lowpass_altivec",
  "avg_h264_qpel16_h_lowpass_altivec",
  "put_h264_qpel16_v_lowpass_altivec",
  "avg_h264_qpel16_v_lowpass_altivec",
  "put_h264_qpel16_hv_lowpass_altivec",
  "avg_h264_qpel16_hv_lowpass_altivec",
  ""
};
#include <stdio.h>
#endif

#ifdef POWERPC_PERFORMANCE_REPORT
void powerpc_display_perf_report(void)
{
  int i, j;
  av_log(NULL, AV_LOG_INFO, "PowerPC performance report\n Values are from the PMC registers, and represent whatever the registers are set to record.\n");
  for(i = 0 ; i < powerpc_perf_total ; i++)
  {
    for (j = 0; j < POWERPC_NUM_PMC_ENABLED ; j++)
      {
	if (perfdata[j][i][powerpc_data_num] != (unsigned long long)0)
	  av_log(NULL, AV_LOG_INFO,
		  " Function \"%s\" (pmc%d):\n\tmin: %llu\n\tmax: %llu\n\tavg: %1.2lf (%llu)\n",
		  perfname[i],
		  j+1,
		  perfdata[j][i][powerpc_data_min],
		  perfdata[j][i][powerpc_data_max],
		  (double)perfdata[j][i][powerpc_data_sum] /
		  (double)perfdata[j][i][powerpc_data_num],
		  perfdata[j][i][powerpc_data_num]);
      }
  }
}
#endif /* POWERPC_PERFORMANCE_REPORT */

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

  update 24/06/2003 : Apple released yesterday the G5,
  with a PPC970. cache line size : 128 bytes. Oups.
  The semantic of dcbz was changed, it always clear
  32 bytes. so the function below will work, but will
  be slow. So I fixed check_dcbz_effect to use dcbzl,
  which is defined to clear a cache line (as dcbz before).
  So we still can distinguish, and use dcbz (32 bytes)
  or dcbzl (one cache line) as required.

  see <http://developer.apple.com/technotes/tn/tn2087.html>
  and <http://developer.apple.com/technotes/tn/tn2086.html>
*/
void clear_blocks_dcbz32_ppc(DCTELEM *blocks)
{
POWERPC_PERF_DECLARE(powerpc_clear_blocks_dcbz32, 1);
    register int misal = ((unsigned long)blocks & 0x00000010);
    register int i = 0;
POWERPC_PERF_START_COUNT(powerpc_clear_blocks_dcbz32, 1);
#if 1
    if (misal) {
      ((unsigned long*)blocks)[0] = 0L;
      ((unsigned long*)blocks)[1] = 0L;
      ((unsigned long*)blocks)[2] = 0L;
      ((unsigned long*)blocks)[3] = 0L;
      i += 16;
    }
    for ( ; i < sizeof(DCTELEM)*6*64-31 ; i += 32) {
#ifndef __MWERKS__
      asm volatile("dcbz %0,%1" : : "b" (blocks), "r" (i) : "memory");
#else
      __dcbz( blocks, i );
#endif
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
POWERPC_PERF_STOP_COUNT(powerpc_clear_blocks_dcbz32, 1);
}

/* same as above, when dcbzl clear a whole 128B cache line
   i.e. the PPC970 aka G5 */
#ifndef NO_DCBZL
void clear_blocks_dcbz128_ppc(DCTELEM *blocks)
{
POWERPC_PERF_DECLARE(powerpc_clear_blocks_dcbz128, 1);
    register int misal = ((unsigned long)blocks & 0x0000007f);
    register int i = 0;
POWERPC_PERF_START_COUNT(powerpc_clear_blocks_dcbz128, 1);
#if 1
 if (misal) {
   // we could probably also optimize this case,
   // but there's not much point as the machines
   // aren't available yet (2003-06-26)
      memset(blocks, 0, sizeof(DCTELEM)*6*64);
    }
    else
      for ( ; i < sizeof(DCTELEM)*6*64 ; i += 128) {
	asm volatile("dcbzl %0,%1" : : "b" (blocks), "r" (i) : "memory");
      }
#else
    memset(blocks, 0, sizeof(DCTELEM)*6*64);
#endif
POWERPC_PERF_STOP_COUNT(powerpc_clear_blocks_dcbz128, 1);
}
#else
void clear_blocks_dcbz128_ppc(DCTELEM *blocks)
{
  memset(blocks, 0, sizeof(DCTELEM)*6*64);
}
#endif

#ifndef NO_DCBZL
/* check dcbz report how many bytes are set to 0 by dcbz */
/* update 24/06/2003 : replace dcbz by dcbzl to get
   the intended effect (Apple "fixed" dcbz)
   unfortunately this cannot be used unless the assembler
   knows about dcbzl ... */
long check_dcbzl_effect(void)
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

  /* below the constraint "b" seems to mean "Address base register"
     in gcc-3.3 / RS/6000 speaks. seems to avoid using r0, so.... */
  asm volatile("dcbzl %0, %1" : : "b" (fakedata_middle), "r" (zero));

  for (i = 0; i < 1024 ; i ++)
  {
    if (fakedata[i] == (char)0)
      count++;
  }

  av_free(fakedata);
  
  return count;
}
#else
long check_dcbzl_effect(void)
{
  return 0;
}
#endif


void dsputil_h264_init_ppc(DSPContext* c, AVCodecContext *avctx);

void dsputil_init_ppc(DSPContext* c, AVCodecContext *avctx)
{
    // Common optimizations whether Altivec is available or not

  switch (check_dcbzl_effect()) {
  case 32:
    c->clear_blocks = clear_blocks_dcbz32_ppc;
    break;
  case 128:
    c->clear_blocks = clear_blocks_dcbz128_ppc;
    break;
  default:
    break;
  }

#ifdef HAVE_ALTIVEC
  dsputil_h264_init_ppc(c, avctx);
  
    if (has_altivec()) {
        mm_flags |= MM_ALTIVEC;
        
        // Altivec specific optimisations
        c->pix_abs[0][1] = sad16_x2_altivec;
        c->pix_abs[0][2] = sad16_y2_altivec;
        c->pix_abs[0][3] = sad16_xy2_altivec;
        c->pix_abs[0][0] = sad16_altivec;
        c->pix_abs[1][0] = sad8_altivec;
        c->sad[0]= sad16_altivec;
        c->sad[1]= sad8_altivec;
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
        /* the two functions do the same thing, so use the same code */
        c->put_no_rnd_pixels_tab[0][0] = put_pixels16_altivec;
        c->avg_pixels_tab[0][0] = avg_pixels16_altivec;
        c->avg_pixels_tab[1][0] = avg_pixels8_altivec;
	c->avg_pixels_tab[1][3] = avg_pixels8_xy2_altivec;
        c->put_pixels_tab[1][3] = put_pixels8_xy2_altivec;
        c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_altivec;
        c->put_pixels_tab[0][3] = put_pixels16_xy2_altivec;
        c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_altivec;
        
	c->gmc1 = gmc1_altivec;

#ifdef CONFIG_DARWIN // ATM gcc-3.3 and gcc-3.4 fail to compile these in linux...
	c->hadamard8_diff[0] = hadamard8_diff16_altivec;
	c->hadamard8_diff[1] = hadamard8_diff8x8_altivec;
#endif

#ifdef CONFIG_ENCODERS
	if (avctx->dct_algo == FF_DCT_AUTO ||
	    avctx->dct_algo == FF_DCT_ALTIVEC)
	{
	    c->fdct = fdct_altivec;
	}
#endif //CONFIG_ENCODERS

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
        
#ifdef POWERPC_PERFORMANCE_REPORT
        {
          int i, j;
          for (i = 0 ; i < powerpc_perf_total ; i++)
          {
	    for (j = 0; j < POWERPC_NUM_PMC_ENABLED ; j++)
	      {
		perfdata[j][i][powerpc_data_min] = 0xFFFFFFFFFFFFFFFFULL;
		perfdata[j][i][powerpc_data_max] = 0x0000000000000000ULL;
		perfdata[j][i][powerpc_data_sum] = 0x0000000000000000ULL;
		perfdata[j][i][powerpc_data_num] = 0x0000000000000000ULL;
	      }
	  }
        }
#endif /* POWERPC_PERFORMANCE_REPORT */
    } else
#endif /* HAVE_ALTIVEC */
    {
        // Non-AltiVec PPC optimisations

        // ... pending ...
    }
}
