/*
 * Copyright (c) 2005 Luca Barbato <lu_zero@gentoo.org>
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

#ifndef AVUTIL_PPC_TIMER_H
#define AVUTIL_PPC_TIMER_H

#include <stdint.h>

#define AV_READ_TIME read_time

static inline uint64_t read_time(void)
{
    uint32_t tbu, tbl, temp;

     /* from section 2.2.1 of the 32-bit PowerPC PEM */
     __asm__ volatile(
         "1:\n"
         "mftbu  %2\n"
         "mftb   %0\n"
         "mftbu  %1\n"
         "cmpw   %2,%1\n"
         "bne    1b\n"
     : "=r"(tbl), "=r"(tbu), "=r"(temp)
     :
     : "cc");

     return (((uint64_t)tbu)<<32) | (uint64_t)tbl;
}

#endif /* AVUTIL_PPC_TIMER_H */
