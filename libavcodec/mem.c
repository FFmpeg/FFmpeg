/*
 * default memory allocator for libavcodec
 * Copyright (c) 2002 Fabrice Bellard.
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
 */
#include "avcodec.h"
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

/* you can redefine av_malloc and av_free in your project to use your
   memory allocator. You do not need to suppress this file because the
   linker will do it automatically */

/* memory alloc */
void *av_malloc(unsigned int size)
{
    void *ptr;
#if defined (HAVE_MEMALIGN)
    ptr = memalign(16,size);
    /* Why 64? 
       Indeed, we should align it:
         on 4 for 386
         on 16 for 486
	 on 32 for 586, PPro - k6-III
	 on 64 for K7 (maybe for P3 too).
       Because L1 and L2 caches are aligned on those values.
       But I don't want to code such logic here!
     */
     /* Why 16?
        because some cpus need alignment, for example SSE2 on P4, & most RISC cpus
        it will just trigger an exception and the unaligned load will be done in the
        exception handler or it will just segfault (SSE2 on P4)
        Why not larger? because i didnt see a difference in benchmarks ...
     */
     /* benchmarks with p3
        memalign(64)+1		3071,3051,3032
        memalign(64)+2		3051,3032,3041
        memalign(64)+4		2911,2896,2915
        memalign(64)+8		2545,2554,2550
        memalign(64)+16		2543,2572,2563
        memalign(64)+32		2546,2545,2571
        memalign(64)+64		2570,2533,2558
        
        btw, malloc seems to do 8 byte alignment by default here
     */
#else
    ptr = malloc(size);
#endif
    if (!ptr)
        return NULL;
//fprintf(stderr, "%X %d\n", (int)ptr, size);
    /* NOTE: this memset should not be present */
    memset(ptr, 0, size);
    return ptr;
}

/* NOTE: ptr = NULL is explicetly allowed */
void av_free(void *ptr)
{
    /* XXX: this test should not be needed on most libcs */
    if (ptr)
        free(ptr);
}

