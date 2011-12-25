/*
 * default memory allocator for libavutil
 * Copyright (c) 2002 Fabrice Bellard
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

/**
 * @file
 * default memory allocator for libavutil
 */

#define _XOPEN_SOURCE 600

#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "avutil.h"
#include "mem.h"

/* here we can use OS-dependent allocation functions */
#undef free
#undef malloc
#undef realloc

#ifdef MALLOC_PREFIX

#define malloc         AV_JOIN(MALLOC_PREFIX, malloc)
#define memalign       AV_JOIN(MALLOC_PREFIX, memalign)
#define posix_memalign AV_JOIN(MALLOC_PREFIX, posix_memalign)
#define realloc        AV_JOIN(MALLOC_PREFIX, realloc)
#define free           AV_JOIN(MALLOC_PREFIX, free)

void *malloc(size_t size);
void *memalign(size_t align, size_t size);
int   posix_memalign(void **ptr, size_t align, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

#endif /* MALLOC_PREFIX */

#define ALIGN (HAVE_AVX ? 32 : 16)

/* You can redefine av_malloc and av_free in your project to use your
   memory allocator. You do not need to suppress this file because the
   linker will do it automatically. */

static size_t max_alloc_size= INT_MAX;

void av_max_alloc(size_t max){
    max_alloc_size = max;
}

void *av_malloc(size_t size)
{
    void *ptr = NULL;
#if CONFIG_MEMALIGN_HACK
    long diff;
#endif

    /* let's disallow possible ambiguous cases */
    if (size > (max_alloc_size-32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    ptr = malloc(size+ALIGN);
    if(!ptr)
        return ptr;
    diff= ((-(long)ptr - 1)&(ALIGN-1)) + 1;
    ptr = (char*)ptr + diff;
    ((char*)ptr)[-1]= diff;
#elif HAVE_POSIX_MEMALIGN
    if (size) //OS X on SDK 10.6 has a broken posix_memalign implementation
    if (posix_memalign(&ptr,ALIGN,size))
        ptr = NULL;
#elif HAVE_MEMALIGN
    ptr = memalign(ALIGN,size);
    /* Why 64?
       Indeed, we should align it:
         on 4 for 386
         on 16 for 486
         on 32 for 586, PPro - K6-III
         on 64 for K7 (maybe for P3 too).
       Because L1 and L2 caches are aligned on those values.
       But I don't want to code such logic here!
     */
     /* Why 32?
        For AVX ASM. SSE / NEON needs only 16.
        Why not larger? Because I did not see a difference in benchmarks ...
     */
     /* benchmarks with P3
        memalign(64)+1          3071,3051,3032
        memalign(64)+2          3051,3032,3041
        memalign(64)+4          2911,2896,2915
        memalign(64)+8          2545,2554,2550
        memalign(64)+16         2543,2572,2563
        memalign(64)+32         2546,2545,2571
        memalign(64)+64         2570,2533,2558

        BTW, malloc seems to do 8-byte alignment by default here.
     */
#else
    ptr = malloc(size);
#endif
    if(!ptr && !size)
        ptr= av_malloc(1);
    return ptr;
}

void *av_realloc(void *ptr, size_t size)
{
#if CONFIG_MEMALIGN_HACK
    int diff;
#endif

    /* let's disallow possible ambiguous cases */
    if (size > (max_alloc_size-32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    //FIXME this isn't aligned correctly, though it probably isn't needed
    if(!ptr) return av_malloc(size);
    diff= ((char*)ptr)[-1];
    ptr= realloc((char*)ptr - diff, size + diff);
    if(ptr) ptr = (char*)ptr + diff;
    return ptr;
#else
    return realloc(ptr, size + !size);
#endif
}

void *av_realloc_f(void *ptr, size_t nelem, size_t elsize)
{
    size_t size;
    void *r;

    if (av_size_mult(elsize, nelem, &size)) {
        av_free(ptr);
        return NULL;
    }
    r = av_realloc(ptr, size);
    if (!r && size)
        av_free(ptr);
    return r;
}

void av_free(void *ptr)
{
#if CONFIG_MEMALIGN_HACK
    if (ptr)
        free((char*)ptr - ((char*)ptr)[-1]);
#else
    free(ptr);
#endif
}

void av_freep(void *arg)
{
    void **ptr= (void**)arg;
    av_free(*ptr);
    *ptr = NULL;
}

void *av_mallocz(size_t size)
{
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void *av_calloc(size_t nmemb, size_t size)
{
    if (size <= 0 || nmemb >= INT_MAX / size)
        return NULL;
    return av_mallocz(nmemb * size);
}

char *av_strdup(const char *s)
{
    char *ptr= NULL;
    if(s){
        int len = strlen(s) + 1;
        ptr = av_malloc(len);
        if (ptr)
            memcpy(ptr, s, len);
    }
    return ptr;
}

/* add one element to a dynamic array */
void av_dynarray_add(void *tab_ptr, int *nb_ptr, void *elem)
{
    /* see similar ffmpeg.c:grow_array() */
    int nb, nb_alloc;
    intptr_t *tab;

    nb = *nb_ptr;
    tab = *(intptr_t**)tab_ptr;
    if ((nb & (nb - 1)) == 0) {
        if (nb == 0)
            nb_alloc = 1;
        else
            nb_alloc = nb * 2;
        tab = av_realloc(tab, nb_alloc * sizeof(intptr_t));
        *(intptr_t**)tab_ptr = tab;
    }
    tab[nb++] = (intptr_t)elem;
    *nb_ptr = nb;
}
