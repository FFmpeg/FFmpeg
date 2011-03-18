/*
 * Copyright (C) 2007 Marc Hoffman
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_BFIN_TIMER_H
#define AVUTIL_BFIN_TIMER_H

#include <stdint.h>

#define AV_READ_TIME read_time

static inline uint64_t read_time(void)
{
    union {
        struct {
            unsigned lo;
            unsigned hi;
        } p;
        unsigned long long c;
    } t;
    __asm__ volatile ("%0=cycles; %1=cycles2;" : "=d" (t.p.lo), "=d" (t.p.hi));
    return t.c;
}

#endif /* AVUTIL_BFIN_TIMER_H */
