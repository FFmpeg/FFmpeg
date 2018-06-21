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
