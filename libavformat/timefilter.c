/*
 * Delay Locked Loop based time filter
 * Copyright (c) 2009 Samalyse
 * Author: Olivier Guilyardi <olivier samalyse com>
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
    double next_cycle_time;
    double feedback2_factor;
    double feedback3_factor;
    double integrator2_state;
};

TimeFilter * ff_timefilter_new(double period, double feedback2_factor, double feedback3_factor)
{
    TimeFilter *self        = av_mallocz(sizeof(TimeFilter));
    self->integrator2_state = period;
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
    self->cycle_time = 0;
}

void ff_timefilter_update(TimeFilter *self, double system_time)
{
    if (!self->cycle_time) {
        /// init loop
        self->cycle_time        = system_time;
        self->next_cycle_time   = self->cycle_time + self->integrator2_state;
    } else {
        /// calculate loop error
        double loop_error = system_time - self->next_cycle_time;

        /// update loop
        self->cycle_time         = self->next_cycle_time;
        self->next_cycle_time   += self->feedback2_factor * loop_error + self->integrator2_state;
        self->integrator2_state += self->feedback3_factor * loop_error;
    }
}

double ff_timefilter_read(TimeFilter *self)
{
    return self->cycle_time;
}
