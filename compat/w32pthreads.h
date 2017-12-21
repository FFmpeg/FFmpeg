/*
 * Copyright (C) 2010-2011 x264 project
 *
 * Authors: Steven Walters <kemuri9@gmail.com>
 *          Pegasys Inc. <http://www.pegasys-inc.com>
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

/**
 * @file
 * w32threads to pthreads wrapper
 */

#ifndef LIBAV_COMPAT_W32PTHREADS_H
#define LIBAV_COMPAT_W32PTHREADS_H

/* Build up a pthread-like API using underlying Windows API. Have only static
 * methods so as to not conflict with a potentially linked in pthread-win32
 * library.
 * As most functions here are used without checking return values,
 * only implement return values as necessary. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

typedef struct pthread_t {
    void *handle;
    void *(*func)(void* arg);
    void *arg;
    void *ret;
} pthread_t;

/* use lightweight mutex/condition variable API for Windows Vista and later */
typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

#define InitializeCriticalSection(x) InitializeCriticalSectionEx(x, 0, 0)
#define WaitForSingleObject(a, b) WaitForSingleObjectEx(a, b, FALSE)

static av_unused unsigned __stdcall attribute_align_arg win32thread_worker(void *arg)
{
    pthread_t *h = arg;
    h->ret = h->func(h->arg);
    return 0;
}

static av_unused int pthread_create(pthread_t *thread, const void *unused_attr,
                                    void *(*start_routine)(void*), void *arg)
{
    thread->func   = start_routine;
    thread->arg    = arg;
    thread->handle = (void*)_beginthreadex(NULL, 0, win32thread_worker, thread,
                                           0, NULL);
    return !thread->handle;
}

static av_unused void pthread_join(pthread_t thread, void **value_ptr)
{
    DWORD ret = WaitForSingleObject(thread.handle, INFINITE);
    if (ret != WAIT_OBJECT_0)
        return;
    if (value_ptr)
        *value_ptr = thread.ret;
    CloseHandle(thread.handle);
}

static inline int pthread_mutex_init(pthread_mutex_t *m, void* attr)
{
    InitializeSRWLock(m);
    return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    /* Unlocked SWR locks use no resources */
    return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    AcquireSRWLockExclusive(m);
    return 0;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    ReleaseSRWLockExclusive(m);
    return 0;
}


typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT

static av_unused int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    BOOL pending = FALSE;
    InitOnceBeginInitialize(once_control, 0, &pending, NULL);
    if (pending)
        init_routine();
    InitOnceComplete(once_control, 0, NULL);
    return 0;
}

static inline void pthread_cond_init(pthread_cond_t *cond, const void *unused_attr)
{
    InitializeConditionVariable(cond);
}

/* native condition variables do not destroy */
static inline void pthread_cond_destroy(pthread_cond_t *cond)
{
    return;
}

static inline void pthread_cond_broadcast(pthread_cond_t *cond)
{
    WakeAllConditionVariable(cond);
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    SleepConditionVariableSRW(cond, mutex, INFINITE, 0);
    return 0;
}

static inline void pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(cond);
}

#endif /* LIBAV_COMPAT_W32PTHREADS_H */
