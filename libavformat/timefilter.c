/*
 * Delay Locked Loop based time filter
 * Copyright (c) 2009 Samalyse
 * Copyright (c) 2009 Michael Niedermayer
 * Author: Olivier Guilyardi <olivier samalyse com>
 *         Michael Niedermayer <michaelni gmx at>
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


#include "config.h"
#include "avformat.h"
#include "timefilter.h"

struct TimeFilter {
    /// Delay Locked Loop data. These variables refer to mathematical
    /// concepts described in: http://www.kokkinizita.net/papers/usingdll.pdf
    double cycle_time;
    double feedback2_factor;
    double feedback3_factor;
    double clock_period;
    int count;
};

TimeFilter * ff_timefilter_new(double clock_period, double feedback2_factor, double feedback3_factor)
{
    TimeFilter *self        = av_mallocz(sizeof(TimeFilter));
    self->clock_period      = clock_period;
    self->feedback2_factor  = feedback2_factor;
    self->feedback3_factor  = feedback3_factor;
    return self;
}

void ff_timefilter_destroy(TimeFilter *self)
{
    av_freep(&self);
}

void ff_timefilter_reset(TimeFilter *self)
{
    self->count      = 0;
}

double ff_timefilter_update(TimeFilter *self, double system_time, double period)
{
    self->count++;
    if (self->count==1) {
        /// init loop
        self->cycle_time    = system_time;
    } else {
        double loop_error;
        self->cycle_time   += self->clock_period * period;
        /// calculate loop error
        loop_error          = system_time - self->cycle_time;

        /// update loop
        self->cycle_time   += FFMAX(self->feedback2_factor, 1.0/(self->count)) * loop_error;
        self->clock_period += self->feedback3_factor * loop_error / period;
    }
    return self->cycle_time;
}

#ifdef TEST
#undef rand
int main(void)
{
    double n0,n1;
#define SAMPLES 1000
    double ideal[SAMPLES];
    double samples[SAMPLES];
#if 1
    for(n0= 0; n0<40; n0=2*n0+1){
        for(n1= 0; n1<10; n1=2*n1+1){
#else
    {{
        n0=7;
        n1=1;
#endif
            double best_error= 1000000000;
            double bestpar0=1;
            double bestpar1=0.001;
            int better, i;

            srandom(123);
            for(i=0; i<SAMPLES; i++){
                ideal[i]  = 10 + i + n1*i/(1000);
                samples[i]= ideal[i] + n0*(rand()-RAND_MAX/2)/(RAND_MAX*10LL);
            }

            do{
                double par0, par1;
                better=0;
                for(par0= bestpar0*0.8; par0<=bestpar0*1.21; par0+=bestpar0*0.05){
                    for(par1= bestpar1*0.8; par1<=bestpar1*1.21; par1+=bestpar1*0.05){
                        double error=0;
                        TimeFilter *tf= ff_timefilter_new(1, par0, par1);
                        for(i=0; i<SAMPLES; i++){
                            double filtered;
                            filtered=  ff_timefilter_update(tf, samples[i], 1);
                            error += (filtered - ideal[i]) * (filtered - ideal[i]);
                        }
                        ff_timefilter_destroy(tf);
                        if(error < best_error){
                            best_error= error;
                            bestpar0= par0;
                            bestpar1= par1;
                            better=1;
                        }
                    }
                }
            }while(better);
#if 0
            double lastfil=9;
            TimeFilter *tf= ff_timefilter_new(1, bestpar0, bestpar1);
            for(i=0; i<SAMPLES; i++){
                double filtered;
                filtered=  ff_timefilter_update(tf, samples[i], 1);
                printf("%f %f %f %f\n", i - samples[i] + 10, filtered - samples[i], samples[FFMAX(i, 1)] - samples[FFMAX(i-1, 0)], filtered - lastfil);
                lastfil= filtered;
            }
            ff_timefilter_destroy(tf);
#else
            printf(" [%f %f %f]", bestpar0, bestpar1, best_error);
#endif
        }
        printf("\n");
    }
    return 0;
}
#endif
