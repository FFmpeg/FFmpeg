/**
 * @file
 * high precision timer, useful to profile code
 *
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVUTIL_TIMER_H
#define AVUTIL_TIMER_H

#include <stdlib.h>
#include <stdint.h>
#include "config.h"

#if   ARCH_ARM
#   include "arm/timer.h"
#elif ARCH_BFIN
#   include "bfin/timer.h"
#elif ARCH_PPC
#   include "ppc/timer.h"
#elif ARCH_X86
#   include "x86/timer.h"
#endif

#if !defined(AV_READ_TIME) && HAVE_GETHRTIME
#   define AV_READ_TIME gethrtime
#endif

#ifdef AV_READ_TIME
#define START_TIMER \
uint64_t tend;\
uint64_t tstart= AV_READ_TIME();\

#define STOP_TIMER(id) \
tend= AV_READ_TIME();\
{\
    static uint64_t tsum=0;\
    static int tcount=0;\
    static int tskip_count=0;\
    if(tcount<2 || tend - tstart < 8*tsum/tcount || tend - tstart < 2000){\
        tsum+= tend - tstart;\
        tcount++;\
    }else\
        tskip_count++;\
    if(((tcount+tskip_count)&(tcount+tskip_count-1))==0){\
        av_log(NULL, AV_LOG_ERROR, "%"PRIu64" dezicycles in %s, %d runs, %d skips\n",\
               tsum*10/tcount, id, tcount, tskip_count);\
    }\
}
#else
#define START_TIMER
#define STOP_TIMER(id) {}
#endif

#endif /* AVUTIL_TIMER_H */
