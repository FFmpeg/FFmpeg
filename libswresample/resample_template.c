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

#if defined(TEMPLATE_RESAMPLE_DBL)
#    define RENAME(N) N ## _double
#    define FILTER_SHIFT 0
#    define DELEM  double
#    define FELEM  double
#    define FELEM2 double
#    define FELEML double
#    define OUT(d, v) d = v

#elif    defined(TEMPLATE_RESAMPLE_FLT)     \
      || defined(TEMPLATE_RESAMPLE_FLT_SSE)

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

int RENAME(swri_resample)(ResampleContext *c, DELEM *dst, const DELEM *src, int *consumed, int src_size, int dst_size, int update_ctx){
    int dst_index, i;
    int index= c->index;
    int frac= c->frac;
    int dst_incr_frac= c->dst_incr % c->src_incr;
    int dst_incr=      c->dst_incr / c->src_incr;
    int compensation_distance= c->compensation_distance;

    av_assert1(c->filter_shift == FILTER_SHIFT);
    av_assert1(c->felem_size == sizeof(FELEM));

    if(compensation_distance == 0 && c->filter_length == 1 && c->phase_shift==0){
        int64_t index2= ((int64_t)index)<<32;
        int64_t incr= (1LL<<32) * c->dst_incr / c->src_incr;
        dst_size= FFMIN(dst_size, (src_size-1-index) * (int64_t)c->src_incr / c->dst_incr);

        for(dst_index=0; dst_index < dst_size; dst_index++){
            dst[dst_index] = src[index2>>32];
            index2 += incr;
        }
        index += dst_index * dst_incr;
        index += (frac + dst_index * (int64_t)dst_incr_frac) / c->src_incr;
        frac   = (frac + dst_index * (int64_t)dst_incr_frac) % c->src_incr;
        av_assert2(index >= 0);
        *consumed= index >> c->phase_shift;
        index &= c->phase_mask;
    }else if(compensation_distance == 0 && !c->linear && index >= 0){
        int sample_index = 0;
        for(dst_index=0; dst_index < dst_size; dst_index++){
            FELEM *filter;
            sample_index += index >> c->phase_shift;
            index &= c->phase_mask;
            filter= ((FELEM*)c->filter_bank) + c->filter_alloc*index;

            if(sample_index + c->filter_length > src_size){
                break;
            }else{
#ifdef COMMON_CORE
                COMMON_CORE
#else
                FELEM2 val=0;
                for(i=0; i<c->filter_length; i++){
                    val += src[sample_index + i] * (FELEM2)filter[i];
                }
                OUT(dst[dst_index], val);
#endif
            }

            frac += dst_incr_frac;
            index += dst_incr;
            if(frac >= c->src_incr){
                frac -= c->src_incr;
                index++;
            }
        }
        *consumed = sample_index;
    }else{
        int sample_index = 0;
        for(dst_index=0; dst_index < dst_size; dst_index++){
            FELEM *filter;
            FELEM2 val=0;

            sample_index += index >> c->phase_shift;
            index &= c->phase_mask;
            filter = ((FELEM*)c->filter_bank) + c->filter_alloc*index;

            if(sample_index + c->filter_length > src_size || -sample_index >= src_size){
                break;
            }else if(sample_index < 0){
                for(i=0; i<c->filter_length; i++)
                    val += src[FFABS(sample_index + i)] * (FELEM2)filter[i];
                OUT(dst[dst_index], val);
            }else if(c->linear){
                FELEM2 v2=0;
#ifdef LINEAR_CORE
                LINEAR_CORE
#else
                for(i=0; i<c->filter_length; i++){
                    val += src[sample_index + i] * (FELEM2)filter[i];
                    v2  += src[sample_index + i] * (FELEM2)filter[i + c->filter_alloc];
                }
#endif
                val+=(v2-val)*(FELEML)frac / c->src_incr;
                OUT(dst[dst_index], val);
            }else{
#ifdef COMMON_CORE
                COMMON_CORE
#else
                for(i=0; i<c->filter_length; i++){
                    val += src[sample_index + i] * (FELEM2)filter[i];
                }
                OUT(dst[dst_index], val);
#endif
            }

            frac += dst_incr_frac;
            index += dst_incr;
            if(frac >= c->src_incr){
                frac -= c->src_incr;
                index++;
            }

            if(dst_index + 1 == compensation_distance){
                compensation_distance= 0;
                dst_incr_frac= c->ideal_dst_incr % c->src_incr;
                dst_incr=      c->ideal_dst_incr / c->src_incr;
            }
        }
        *consumed= FFMAX(sample_index, 0);
        index += FFMIN(sample_index, 0) << c->phase_shift;

        if(compensation_distance){
            compensation_distance -= dst_index;
            av_assert1(compensation_distance > 0);
        }
    }

    if(update_ctx){
        c->frac= frac;
        c->index= index;
        c->dst_incr= dst_incr_frac + c->src_incr*dst_incr;
        c->compensation_distance= compensation_distance;
    }

    return dst_index;
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
