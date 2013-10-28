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

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "config.h"
#include "timefilter.h"

struct TimeFilter {
    // Delay Locked Loop data. These variables refer to mathematical
    // concepts described in: http://www.kokkinizita.net/papers/usingdll.pdf
    double cycle_time;
    double feedback2_factor;
    double feedback3_factor;
    double clock_period;
    int count;
};

/* 1 - exp(-x) using a 3-order power series */
static double qexpneg(double x)
{
    return 1 - 1 / (1 + x * (1 + x / 2 * (1 + x / 3)));
}

TimeFilter *ff_timefilter_new(double time_base,
                              double period,
                              double bandwidth)
{
    TimeFilter *self       = av_mallocz(sizeof(TimeFilter));
    double o               = 2 * M_PI * bandwidth * period * time_base;

    if (!self)
        return NULL;

    self->clock_period     = time_base;
    self->feedback2_factor = qexpneg(M_SQRT2 * o);
    self->feedback3_factor = qexpneg(o * o) / period;
    return self;
}

void ff_timefilter_destroy(TimeFilter *self)
{
    av_freep(&self);
}

void ff_timefilter_reset(TimeFilter *self)
{
    self->count = 0;
}

double ff_timefilter_update(TimeFilter *self, double system_time, double period)
{
    self->count++;
    if (self->count == 1) {
        self->cycle_time = system_time;
    } else {
        double loop_error;
        self->cycle_time += self->clock_period * period;
        loop_error = system_time - self->cycle_time;

        self->cycle_time   += FFMAX(self->feedback2_factor, 1.0 / self->count) * loop_error;
        self->clock_period += self->feedback3_factor * loop_error;
    }
    return self->cycle_time;
}

double ff_timefilter_eval(TimeFilter *self, double delta)
{
    return self->cycle_time + self->clock_period * delta;
}

#ifdef TEST
#include "libavutil/lfg.h"
#define LFG_MAX ((1LL << 32) - 1)

int main(void)
{
    AVLFG prng;
    double n0, n1;
#define SAMPLES 1000
    double ideal[SAMPLES];
    double samples[SAMPLES];
    double samplet[SAMPLES];
    for (n0 = 0; n0 < 40; n0 = 2 * n0 + 1) {
        for (n1 = 0; n1 < 10; n1 = 2 * n1 + 1) {
            double best_error = 1000000000;
            double bestpar0   = n0 ? 1 : 100000;
            double bestpar1   = 1;
            int better, i;

            av_lfg_init(&prng, 123);
            for (i = 0; i < SAMPLES; i++) {
                samplet[i] = 10 + i + (av_lfg_get(&prng) < LFG_MAX/2 ? 0 : 0.999);
                ideal[i]   = samplet[i] + n1 * i / (1000);
                samples[i] = ideal[i] + n0 * (av_lfg_get(&prng) - LFG_MAX / 2) / (LFG_MAX * 10LL);
                if(i && samples[i]<samples[i-1])
                    samples[i]=samples[i-1]+0.001;
            }

            do {
                double par0, par1;
                better = 0;
                for (par0 = bestpar0 * 0.8; par0 <= bestpar0 * 1.21; par0 += bestpar0 * 0.05) {
                    for (par1 = bestpar1 * 0.8; par1 <= bestpar1 * 1.21; par1 += bestpar1 * 0.05) {
                        double error   = 0;
                        TimeFilter *tf = ff_timefilter_new(1, par0, par1);
                        if (!tf) {
                            printf("Could not alocate memory for timefilter.\n");
                            exit(1);
                        }
                        for (i = 0; i < SAMPLES; i++) {
                            double filtered;
                            filtered = ff_timefilter_update(tf, samples[i], i ? (samplet[i] - samplet[i-1]) : 1);
                            if(filtered < 0 || filtered > 1000000000)
                                printf("filter is unstable\n");
                            error   += (filtered - ideal[i]) * (filtered - ideal[i]);
                        }
                        ff_timefilter_destroy(tf);
                        if (error < best_error) {
                            best_error = error;
                            bestpar0   = par0;
                            bestpar1   = par1;
                            better     = 1;
                        }
                    }
                }
            } while (better);
#if 0
            double lastfil = 9;
            TimeFilter *tf = ff_timefilter_new(1, bestpar0, bestpar1);
            for (i = 0; i < SAMPLES; i++) {
                double filtered;
                filtered = ff_timefilter_update(tf, samples[i], 1);
                printf("%f %f %f %f\n", i - samples[i] + 10, filtered - samples[i],
                       samples[FFMAX(i, 1)] - samples[FFMAX(i - 1, 0)], filtered - lastfil);
                lastfil = filtered;
            }
            ff_timefilter_destroy(tf);
#else
            printf(" [%12f %11f %9f]", bestpar0, bestpar1, best_error);
#endif
        }
        printf("\n");
    }
    return 0;
}
#endif
