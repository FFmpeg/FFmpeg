/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * audio resampling
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#if    defined(TEMPLATE_RESAMPLE_DBL)     \
    || defined(TEMPLATE_RESAMPLE_DBL_SSE2)

#    define FILTER_SHIFT 0
#    define DELEM  double
#    define FELEM  double
#    define FELEM2 double
#    define FELEML double
#    define OUT(d, v) d = v

#    if defined(TEMPLATE_RESAMPLE_DBL)
#        define RENAME(N) N ## _double
#    elif defined(TEMPLATE_RESAMPLE_DBL_SSE2)
#        define COMMON_CORE COMMON_CORE_DBL_SSE2
#        define LINEAR_CORE LINEAR_CORE_DBL_SSE2
#        define RENAME(N) N ## _double_sse2
#    endif

#elif    defined(TEMPLATE_RESAMPLE_FLT)     \
      || defined(TEMPLATE_RESAMPLE_FLT_SSE) \
      || defined(TEMPLATE_RESAMPLE_FLT_AVX)

#    define FILTER_SHIFT 0
#    define DELEM  float
#    define FELEM  float
#    define FELEM2 float
#    define FELEML float
#    define OUT(d, v) d = v

#    if defined(TEMPLATE_RESAMPLE_FLT)
#        define RENAME(N) N ## _float
#    elif defined(TEMPLATE_RESAMPLE_FLT_SSE)
#        define COMMON_CORE COMMON_CORE_FLT_SSE
#        define LINEAR_CORE LINEAR_CORE_FLT_SSE
#        define RENAME(N) N ## _float_sse
#    elif defined(TEMPLATE_RESAMPLE_FLT_AVX)
#        define COMMON_CORE COMMON_CORE_FLT_AVX
#        define LINEAR_CORE LINEAR_CORE_FLT_AVX
#        define RENAME(N) N ## _float_avx
#    endif

#elif defined(TEMPLATE_RESAMPLE_S32)
#    define RENAME(N) N ## _int32
#    define FILTER_SHIFT 30
#    define DELEM  int32_t
#    define FELEM  int32_t
#    define FELEM2 int64_t
#    define FELEML int64_t
#    define FELEM_MAX INT32_MAX
#    define FELEM_MIN INT32_MIN
#    define OUT(d, v) v = (v + (1<<(FILTER_SHIFT-1)))>>FILTER_SHIFT;\
                      d = (uint64_t)(v + 0x80000000) > 0xFFFFFFFF ? (v>>63) ^ 0x7FFFFFFF : v

#elif    defined(TEMPLATE_RESAMPLE_S16)      \
      || defined(TEMPLATE_RESAMPLE_S16_MMX2) \
      || defined(TEMPLATE_RESAMPLE_S16_SSE2)

#    define FILTER_SHIFT 15
#    define DELEM  int16_t
#    define FELEM  int16_t
#    define FELEM2 int32_t
#    define FELEML int64_t
#    define FELEM_MAX INT16_MAX
#    define FELEM_MIN INT16_MIN
#    define OUT(d, v) v = (v + (1<<(FILTER_SHIFT-1)))>>FILTER_SHIFT;\
                      d = (unsigned)(v + 32768) > 65535 ? (v>>31) ^ 32767 : v

#    if defined(TEMPLATE_RESAMPLE_S16)
#        define RENAME(N) N ## _int16
#    elif defined(TEMPLATE_RESAMPLE_S16_MMX2)
#        define COMMON_CORE COMMON_CORE_INT16_MMX2
#        define LINEAR_CORE LINEAR_CORE_INT16_MMX2
#        define RENAME(N) N ## _int16_mmx2
#    elif defined(TEMPLATE_RESAMPLE_S16_SSE2)
#        define COMMON_CORE COMMON_CORE_INT16_SSE2
#        define LINEAR_CORE LINEAR_CORE_INT16_SSE2
#        define RENAME(N) N ## _int16_sse2
#    endif

#endif

#if DO_RESAMPLE_ONE
static void RENAME(resample_one)(DELEM *dst, const DELEM *src,
                                 int dst_size, int64_t index2, int64_t incr)
{
    int dst_index;

    for (dst_index = 0; dst_index < dst_size; dst_index++) {
        dst[dst_index] = src[index2 >> 32];
        index2 += incr;
    }
}
#endif

int RENAME(swri_resample_common)(ResampleContext *c,
                                 DELEM *dst, const DELEM *src,
                                 int n, int update_ctx)
{
    int dst_index;
    int index= c->index;
    int frac= c->frac;
    int sample_index = index >> c->phase_shift;

    index &= c->phase_mask;
    for (dst_index = 0; dst_index < n; dst_index++) {
        FELEM *filter = ((FELEM *) c->filter_bank) + c->filter_alloc * index;

#ifdef COMMON_CORE
        COMMON_CORE
#else
        FELEM2 val=0;
        int i;
        for (i = 0; i < c->filter_length; i++) {
            val += src[sample_index + i] * (FELEM2)filter[i];
        }
        OUT(dst[dst_index], val);
#endif

        frac  += c->dst_incr_mod;
        index += c->dst_incr_div;
        if (frac >= c->src_incr) {
            frac -= c->src_incr;
            index++;
        }
        sample_index += index >> c->phase_shift;
        index &= c->phase_mask;
    }

    if(update_ctx){
        c->frac= frac;
        c->index= index;
    }

    return sample_index;
}

int RENAME(swri_resample_linear)(ResampleContext *c,
                                 DELEM *dst, const DELEM *src,
                                 int n, int update_ctx)
{
    int dst_index;
    int index= c->index;
    int frac= c->frac;
    int dst_incr_frac= c->dst_incr % c->src_incr;
    int dst_incr=      c->dst_incr / c->src_incr;
    int sample_index = index >> c->phase_shift;

    index &= c->phase_mask;
    for (dst_index = 0; dst_index < n; dst_index++) {
        FELEM *filter = ((FELEM *) c->filter_bank) + c->filter_alloc * index;
        FELEM2 val=0, v2 = 0;

#ifdef LINEAR_CORE
        LINEAR_CORE
#else
        int i;
        for (i = 0; i < c->filter_length; i++) {
            val += src[sample_index + i] * (FELEM2)filter[i];
            v2  += src[sample_index + i] * (FELEM2)filter[i + c->filter_alloc];
        }
#endif
        val += (v2 - val) * (FELEML) frac / c->src_incr;
        OUT(dst[dst_index], val);

        frac += dst_incr_frac;
        index += dst_incr;
        if (frac >= c->src_incr) {
            frac -= c->src_incr;
            index++;
        }
        sample_index += index >> c->phase_shift;
        index &= c->phase_mask;
    }

    if(update_ctx){
        c->frac= frac;
        c->index= index;
    }

    return sample_index;
}

#undef COMMON_CORE
#undef LINEAR_CORE
#undef RENAME
#undef FILTER_SHIFT
#undef DELEM
#undef FELEM
#undef FELEM2
#undef FELEML
#undef FELEM_MAX
#undef FELEM_MIN
#undef OUT
