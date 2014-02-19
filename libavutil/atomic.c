/*
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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
#include "atomic.h"

#if !HAVE_ATOMICS_NATIVE

#if HAVE_PTHREADS

#include <pthread.h>

static pthread_mutex_t atomic_lock = PTHREAD_MUTEX_INITIALIZER;

int avpriv_atomic_int_get(volatile int *ptr)
{
    int res;

    pthread_mutex_lock(&atomic_lock);
    res = *ptr;
    pthread_mutex_unlock(&atomic_lock);

    return res;
}

void avpriv_atomic_int_set(volatile int *ptr, int val)
{
    pthread_mutex_lock(&atomic_lock);
    *ptr = val;
    pthread_mutex_unlock(&atomic_lock);
}

int avpriv_atomic_int_add_and_fetch(volatile int *ptr, int inc)
{
    int res;

    pthread_mutex_lock(&atomic_lock);
    *ptr += inc;
    res = *ptr;
    pthread_mutex_unlock(&atomic_lock);

    return res;
}

void *avpriv_atomic_ptr_cas(void * volatile *ptr, void *oldval, void *newval)
{
    void *ret;
    pthread_mutex_lock(&atomic_lock);
    ret = *ptr;
    if (*ptr == oldval)
        *ptr = newval;
    pthread_mutex_unlock(&atomic_lock);
    return ret;
}

#elif !HAVE_THREADS

int avpriv_atomic_int_get(volatile int *ptr)
{
    return *ptr;
}

void avpriv_atomic_int_set(volatile int *ptr, int val)
{
    *ptr = val;
}

int avpriv_atomic_int_add_and_fetch(volatile int *ptr, int inc)
{
    *ptr += inc;
    return *ptr;
}

void *avpriv_atomic_ptr_cas(void * volatile *ptr, void *oldval, void *newval)
{
    if (*ptr == oldval) {
        *ptr = newval;
        return oldval;
    }
    return *ptr;
}

#else /* HAVE_THREADS */

/* This should never trigger, unless a new threading implementation
 * without correct atomics dependencies in configure or a corresponding
 * atomics implementation is added. */
#error "Threading is enabled, but there is no implementation of atomic operations available"

#endif /* HAVE_PTHREADS */

#endif /* !HAVE_ATOMICS_NATIVE */

#ifdef TEST
#include "avassert.h"

int main(void)
{
    volatile int val = 1;
    int res;

    res = avpriv_atomic_int_add_and_fetch(&val, 1);
    av_assert0(res == 2);
    avpriv_atomic_int_set(&val, 3);
    res = avpriv_atomic_int_get(&val);
    av_assert0(res == 3);

    return 0;
}
#endif
