/*
 * copyright (C) 2001 Arpad Gereoffy
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// a52_resample_init should find the requested converter (from type flags ->
// given number of channels) and set up some function pointers...

// a52_resample() should do the conversion.

#include "a52.h"
#include "mm_accel.h"
#include "config.h"
#include "../../libpostproc/mangle.h"

int (* a52_resample) (float * _f, int16_t * s16)=NULL;

#include "resample_c.c"

#ifdef ARCH_X86
#include "resample_mmx.c"
#endif

void* a52_resample_init(uint32_t mm_accel,int flags,int chans){
void* tmp;

#ifdef ARCH_X86
    if(mm_accel&MM_ACCEL_X86_MMX){
	tmp=a52_resample_MMX(flags,chans);
	if(tmp){
	    if(a52_resample==NULL) av_log(NULL, AV_LOG_INFO, "Using MMX optimized resampler\n");
	    a52_resample=tmp;
	    return tmp;
	}
    }
#endif

    tmp=a52_resample_C(flags,chans);
    if(tmp){
	if(a52_resample==NULL) av_log(NULL, AV_LOG_INFO, "No accelerated resampler found\n");
	a52_resample=tmp;
	return tmp;
    }

    av_log(NULL, AV_LOG_ERROR, "Unimplemented resampler for mode 0x%X -> %d channels conversion - Contact MPlayer developers!\n", flags, chans);
    return NULL;
}
