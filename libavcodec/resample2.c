/*
 * audio resampling
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 */
 
/**
 * @file resample2.c
 * audio resampling
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "common.h"
#include "dsputil.h"

#define PHASE_SHIFT 10
#define PHASE_COUNT (1<<PHASE_SHIFT)
#define PHASE_MASK (PHASE_COUNT-1)
#define FILTER_SHIFT 15

typedef struct AVResampleContext{
    short *filter_bank;
    int filter_length;
    int ideal_dst_incr;
    int dst_incr;
    int index;
    int frac;
    int src_incr;
    int compensation_distance;
}AVResampleContext;

/**
 * 0th order modified bessel function of the first kind.
 */
double bessel(double x){
    double v=1;
    double t=1;
    int i;
    
    for(i=1; i<50; i++){
        t *= i;
        v += pow(x*x/4, i)/(t*t);
    }
    return v;
}

/**
 * builds a polyphase filterbank.
 * @param factor resampling factor
 * @param scale wanted sum of coefficients for each filter
 * @param type 0->cubic, 1->blackman nuttall windowed sinc, 2->kaiser windowed sinc beta=16
 */
void av_build_filter(int16_t *filter, double factor, int tap_count, int phase_count, int scale, int type){
    int ph, i, v;
    double x, y, w, tab[tap_count];
    const int center= (tap_count-1)/2;

    /* if upsampling, only need to interpolate, no filter */
    if (factor > 1.0)
        factor = 1.0;

    for(ph=0;ph<phase_count;ph++) {
        double norm = 0;
        double e= 0;
        for(i=0;i<tap_count;i++) {
            x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
            if (x == 0) y = 1.0;
            else        y = sin(x) / x;
            switch(type){
            case 0:{
                const float d= -0.5; //first order derivative = -0.5
                x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
                if(x<1.0) y= 1 - 3*x*x + 2*x*x*x + d*(            -x*x + x*x*x);
                else      y=                       d*(-4 + 8*x - 5*x*x + x*x*x);
                break;}
            case 1:
                w = 2.0*x / (factor*tap_count) + M_PI;
                y *= 0.3635819 - 0.4891775 * cos(w) + 0.1365995 * cos(2*w) - 0.0106411 * cos(3*w);
                break;
            case 2:
                w = 2.0*x / (factor*tap_count*M_PI);
                y *= bessel(16*sqrt(FFMAX(1-w*w, 0)));
                break;
            }

            tab[i] = y;
            norm += y;
        }

        /* normalize so that an uniform color remains the same */
        for(i=0;i<tap_count;i++) {
            v = clip(lrintf(tab[i] * scale / norm) + e, -32768, 32767);
            filter[ph * tap_count + i] = v;
            e += tab[i] * scale / norm - v;
        }
    }
}

/**
 * initalizes a audio resampler.
 * note, if either rate is not a integer then simply scale both rates up so they are
 */
AVResampleContext *av_resample_init(int out_rate, int in_rate){
    AVResampleContext *c= av_mallocz(sizeof(AVResampleContext));
    double factor= FFMIN(out_rate / (double)in_rate, 1.0);

    memset(c, 0, sizeof(AVResampleContext));

    c->filter_length= ceil(16.0/factor);
    c->filter_bank= av_mallocz(c->filter_length*(PHASE_COUNT+1)*sizeof(short));
    av_build_filter(c->filter_bank, factor, c->filter_length, PHASE_COUNT, 1<<FILTER_SHIFT, 1);
    c->filter_bank[c->filter_length*PHASE_COUNT + (c->filter_length-1)/2 + 1]= (1<<FILTER_SHIFT)-1;
    c->filter_bank[c->filter_length*PHASE_COUNT + (c->filter_length-1)/2 + 2]= 1;

    c->src_incr= out_rate;
    c->ideal_dst_incr= c->dst_incr= in_rate * PHASE_COUNT;
    c->index= -PHASE_COUNT*((c->filter_length-1)/2);

    return c;
}

void av_resample_close(AVResampleContext *c){
    av_freep(&c->filter_bank);
    av_freep(&c);
}

void av_resample_compensate(AVResampleContext *c, int sample_delta, int compensation_distance){
//    sample_delta += (c->ideal_dst_incr - c->dst_incr)*(int64_t)c->compensation_distance / c->ideal_dst_incr;
    c->compensation_distance= compensation_distance;
    c->dst_incr = c->ideal_dst_incr - c->ideal_dst_incr * (int64_t)sample_delta / compensation_distance;
}

/**
 * resamples.
 * @param src an array of unconsumed samples
 * @param consumed the number of samples of src which have been consumed are returned here
 * @param src_size the number of unconsumed samples available
 * @param dst_size the amount of space in samples available in dst
 * @param update_ctx if this is 0 then the context wont be modified, that way several channels can be resampled with the same context
 * @return the number of samples written in dst or -1 if an error occured
 */
int av_resample(AVResampleContext *c, short *dst, short *src, int *consumed, int src_size, int dst_size, int update_ctx){
    int dst_index, i;
    int index= c->index;
    int frac= c->frac;
    int dst_incr_frac= c->dst_incr % c->src_incr;
    int dst_incr=      c->dst_incr / c->src_incr;
    
    if(c->compensation_distance && c->compensation_distance < dst_size)
        dst_size= c->compensation_distance;
    
    for(dst_index=0; dst_index < dst_size; dst_index++){
        short *filter= c->filter_bank + c->filter_length*(index & PHASE_MASK);
        int sample_index= index >> PHASE_SHIFT;
        int val=0;
        
        if(sample_index < 0){
            for(i=0; i<c->filter_length; i++)
                val += src[ABS(sample_index + i) % src_size] * filter[i];
        }else if(sample_index + c->filter_length > src_size){
            break;
        }else{
#if 0
            int64_t v=0;
            int sub_phase= (frac<<12) / c->src_incr;
            for(i=0; i<c->filter_length; i++){
                int64_t coeff= filter[i]*(4096 - sub_phase) + filter[i + c->filter_length]*sub_phase;
                v += src[sample_index + i] * coeff;
            }
            val= v>>12;
#else
            for(i=0; i<c->filter_length; i++){
                val += src[sample_index + i] * filter[i];
            }
#endif
        }

        val = (val + (1<<(FILTER_SHIFT-1)))>>FILTER_SHIFT;
        dst[dst_index] = (unsigned)(val + 32768) > 65535 ? (val>>31) ^ 32767 : val;

        frac += dst_incr_frac;
        index += dst_incr;
        if(frac >= c->src_incr){
            frac -= c->src_incr;
            index++;
        }
    }
    *consumed= FFMAX(index, 0) >> PHASE_SHIFT;
    index= FFMIN(index, 0);

    if(update_ctx){
        if(c->compensation_distance){
            c->compensation_distance -= dst_index;
            if(!c->compensation_distance)
                c->dst_incr= c->ideal_dst_incr;
        }
        c->frac= frac;
        c->index= index;
    }
#if 0    
    if(update_ctx && !c->compensation_distance){
#undef rand
        av_resample_compensate(c, rand() % (8000*2) - 8000, 8000*2);
av_log(NULL, AV_LOG_DEBUG, "%d %d %d\n", c->dst_incr, c->ideal_dst_incr, c->compensation_distance);
    }
#endif
    
    return dst_index;
}
