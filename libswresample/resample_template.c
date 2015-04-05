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
#    define OUT(d, v) d = v

#elif    defined(TEMPLATE_RESAMPLE_FLT)

#    define RENAME(N) N ## _float
#    define FILTER_SHIFT 0
#    define DELEM  float
#    define FELEM  float
#    define FELEM2 float
#    define OUT(d, v) d = v

#elif defined(TEMPLATE_RESAMPLE_S32)

#    define RENAME(N) N ## _int32
#    define FILTER_SHIFT 30
#    define DELEM  int32_t
#    define FELEM  int32_t
#    define FELEM2 int64_t
#    define FELEM_MAX INT32_MAX
#    define FELEM_MIN INT32_MIN
#    define OUT(d, v) (v) = ((v) + (1<<(FILTER_SHIFT-1)))>>FILTER_SHIFT;\
                      (d) = av_clipl_int32(v)

#elif    defined(TEMPLATE_RESAMPLE_S16)

#    define RENAME(N) N ## _int16
#    define FILTER_SHIFT 15
#    define DELEM  int16_t
#    define FELEM  int16_t
#    define FELEM2 int32_t
#    define FELEML int64_t
#    define FELEM_MAX INT16_MAX
#    define FELEM_MIN INT16_MIN
#    define OUT(d, v) (v) = ((v) + (1<<(FILTER_SHIFT-1)))>>FILTER_SHIFT;\
                      (d) = av_clip_int16(v)

#endif

static void RENAME(resample_one)(void *dest, const void *source,
                                 int dst_size, int64_t index2, int64_t incr)
{
    DELEM *dst = dest;
    const DELEM *src = source;
    int dst_index;

    for (dst_index = 0; dst_index < dst_size; dst_index++) {
        dst[dst_index] = src[index2 >> 32];
        index2 += incr;
    }
}

static int RENAME(resample_common)(ResampleContext *c,
                                   void *dest, const void *source,
                                   int n, int update_ctx)
{
    DELEM *dst = dest;
    const DELEM *src = source;
    int dst_index;
    int index= c->index;
    int frac= c->frac;
    int sample_index = index >> c->phase_shift;

    index &= c->phase_mask;
    for (dst_index = 0; dst_index < n; dst_index++) {
        FELEM *filter = ((FELEM *) c->filter_bank) + c->filter_alloc * index;

        FELEM2 val=0;
        int i;
        for (i = 0; i < c->filter_length; i++) {
            val += src[sample_index + i] * (FELEM2)filter[i];
        }
        OUT(dst[dst_index], val);

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

static int RENAME(resample_linear)(ResampleContext *c,
                                   void *dest, const void *source,
                                   int n, int update_ctx)
{
    DELEM *dst = dest;
    const DELEM *src = source;
    int dst_index;
    int index= c->index;
    int frac= c->frac;
    int sample_index = index >> c->phase_shift;
#if FILTER_SHIFT == 0
    double inv_src_incr = 1.0 / c->src_incr;
#endif

    index &= c->phase_mask;
    for (dst_index = 0; dst_index < n; dst_index++) {
        FELEM *filter = ((FELEM *) c->filter_bank) + c->filter_alloc * index;
        FELEM2 val=0, v2 = 0;

        int i;
        for (i = 0; i < c->filter_length; i++) {
            val += src[sample_index + i] * (FELEM2)filter[i];
            v2  += src[sample_index + i] * (FELEM2)filter[i + c->filter_alloc];
        }
#ifdef FELEML
        val += (v2 - val) * (FELEML) frac / c->src_incr;
#else
#    if FILTER_SHIFT == 0
        val += (v2 - val) * inv_src_incr * frac;
#    else
        val += (v2 - val) / c->src_incr * frac;
#    endif
#endif
        OUT(dst[dst_index], val);

        frac += c->dst_incr_mod;
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

#undef RENAME
#undef FILTER_SHIFT
#undef DELEM
#undef FELEM
#undef FELEM2
#undef FELEML
#undef FELEM_MAX
#undef FELEM_MIN
#undef OUT
