/*
 * Copyright (C) 2010-2011 x264 project
 *
 * Authors: Steven Walters <kemuri9@gmail.com>
 *          Pegasys Inc. <http://www.pegasys-inc.com>
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
 * w32threads to pthreads wrapper
 */

#ifndef FFMPEG_COMPAT_W32PTHREADS_H
#define FFMPEG_COMPAT_W32PTHREADS_H

/* Build up a pthread-like API using underlying Windows API. Have only static
 * methods so as to not conflict with a potentially linked in pthread-win32
 * library.
 * As most functions here are used without checking return values,
 * only implement return values as necessary. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#if _WIN32_WINNT < 0x0600 && defined(__MINGW32__)
#undef MemoryBarrier
#define MemoryBarrier __sync_synchronize
#endif

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

typedef struct pthread_t {
    void *handle;
    void *(*func)(void* arg);
    void *arg;
    void *ret;
} pthread_t;

/* the conditional variable api for windows 6.0+ uses critical sections and
 * not mutexes */
typedef CRITICAL_SECTION pthread_mutex_t;

/* This is the CONDITION_VARIABLE typedef for using Windows' native
 * conditional variables on kernels 6.0+. */
#if HAVE_CONDITION_VARIABLE_PTR
typedef CONDITION_VARIABLE pthread_cond_t;
#else
typedef struct pthread_cond_t {
    void *Ptr;
} pthread_cond_t;
#endif

#if _WIN32_WINNT >= 0x0600
#define InitializeCriticalSection(x) InitializeCriticalSectionEx(x, 0, 0)
#define WaitForSingleObject(a, b) WaitForSingleObjectEx(a, b, FALSE)
#endif

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
#if HAVE_WINRT
    thread->handle = (void*)CreateThread(NULL, 0, win32thread_worker, thread,
                                           0, NULL);
#else
    thread->handle = (void*)_beginthreadex(NULL, 0, win32thread_worker, thread,
                                           0, NULL);
#endif
    return !thread->handle;
}

static av_unused int pthread_join(pthread_t thread, void **value_ptr)
{
    DWORD ret = WaitForSingleObject(thread.handle, INFINITE);
    if (ret != WAIT_OBJECT_0) {
        if (ret == WAIT_ABANDONED)
            return EINVAL;
        else
            return EDEADLK;
    }
    if (value_ptr)
        *value_ptr = thread.ret;
    CloseHandle(thread.handle);
    return 0;
}

static inline int pthread_mutex_init(pthread_mutex_t *m, void* attr)
{
    InitializeCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    DeleteCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    EnterCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    LeaveCriticalSection(m);
    return 0;
}

#if _WIN32_WINNT >= 0x0600
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

static inline int pthread_cond_init(pthread_cond_t *cond, const void *unused_attr)
{
    InitializeConditionVariable(cond);
    return 0;
}

/* native condition variables do not destroy */
static inline int pthread_cond_destroy(pthread_cond_t *cond)
{
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(cond);
    return 0;
}

#else // _WIN32_WINNT < 0x0600

/* atomic init state of dynamically loaded functions */
static LONG w32thread_init_state = 0;
static av_unused void w32thread_init(void);

/* for pre-Windows 6.0 platforms, define INIT_ONCE struct,
 * compatible to the one used in the native API */

typedef union pthread_once_t  {
    void * Ptr;    ///< For the Windows 6.0+ native functions
    LONG state;    ///< For the pre-Windows 6.0 compat code
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0}

/* function pointers to init once API on windows 6.0+ kernels */
static BOOL (WINAPI *initonce_begin)(pthread_once_t *lpInitOnce, DWORD dwFlags, BOOL *fPending, void **lpContext);
static BOOL (WINAPI *initonce_complete)(pthread_once_t *lpInitOnce, DWORD dwFlags, void *lpContext);

/* pre-Windows 6.0 compat using a spin-lock */
static inline void w32thread_once_fallback(LONG volatile *state, void (*init_routine)(void))
{
    switch (InterlockedCompareExchange(state, 1, 0)) {
    /* Initial run */
    case 0:
        init_routine();
        InterlockedExchange(state, 2);
        break;
    /* Another thread is running init */
    case 1:
        while (1) {
            MemoryBarrier();
            if (*state == 2)
                break;
            Sleep(0);
        }
        break;
    /* Initialization complete */
    case 2:
        break;
    }
}

static av_unused int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    w32thread_once_fallback(&w32thread_init_state, w32thread_init);

    /* Use native functions on Windows 6.0+ */
    if (initonce_begin && initonce_complete) {
        BOOL pending = FALSE;
        initonce_begin(once_control, 0, &pending, NULL);
        if (pending)
            init_routine();
        initonce_complete(once_control, 0, NULL);
        return 0;
    }

    w32thread_once_fallback(&once_control->state, init_routine);
    return 0;
}

/* for pre-Windows 6.0 platforms we need to define and use our own condition
 * variable and api */

typedef struct  win32_cond_t {
    pthread_mutex_t mtx_broadcast;
    pthread_mutex_t mtx_waiter_count;
    volatile int waiter_count;
    HANDLE semaphore;
    HANDLE waiters_done;
    volatile int is_broadcast;
} win32_cond_t;

/* function pointers to conditional variable API on windows 6.0+ kernels */
static void (WINAPI *cond_broadcast)(pthread_cond_t *cond);
static void (WINAPI *cond_init)(pthread_cond_t *cond);
static void (WINAPI *cond_signal)(pthread_cond_t *cond);
static BOOL (WINAPI *cond_wait)(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                DWORD milliseconds);

static av_unused int pthread_cond_init(pthread_cond_t *cond, const void *unused_attr)
{
    win32_cond_t *win32_cond = NULL;

    w32thread_once_fallback(&w32thread_init_state, w32thread_init);

    if (cond_init) {
        cond_init(cond);
        return 0;
    }

    /* non native condition variables */
    win32_cond = av_mallocz(sizeof(win32_cond_t));
    if (!win32_cond)
        return ENOMEM;
    cond->Ptr = win32_cond;
    win32_cond->semaphore = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
    if (!win32_cond->semaphore)
        return ENOMEM;
    win32_cond->waiters_done = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!win32_cond->waiters_done)
        return ENOMEM;

    pthread_mutex_init(&win32_cond->mtx_waiter_count, NULL);
    pthread_mutex_init(&win32_cond->mtx_broadcast, NULL);
    return 0;
}

static av_unused int pthread_cond_destroy(pthread_cond_t *cond)
{
    win32_cond_t *win32_cond = cond->Ptr;
    /* native condition variables do not destroy */
    if (cond_init)
        return 0;

    /* non native condition variables */
    CloseHandle(win32_cond->semaphore);
    CloseHandle(win32_cond->waiters_done);
    pthread_mutex_destroy(&win32_cond->mtx_waiter_count);
    pthread_mutex_destroy(&win32_cond->mtx_broadcast);
    av_freep(&win32_cond);
    cond->Ptr = NULL;
    return 0;
}

static av_unused int pthread_cond_broadcast(pthread_cond_t *cond)
{
    win32_cond_t *win32_cond = cond->Ptr;
    int have_waiter;

    if (cond_broadcast) {
        cond_broadcast(cond);
        return 0;
    }

    /* non native condition variables */
    pthread_mutex_lock(&win32_cond->mtx_broadcast);
    pthread_mutex_lock(&win32_cond->mtx_waiter_count);
    have_waiter = 0;

    if (win32_cond->waiter_count) {
        win32_cond->is_broadcast = 1;
        have_waiter = 1;
    }

    if (have_waiter) {
        ReleaseSemaphore(win32_cond->semaphore, win32_cond->waiter_count, NULL);
        pthread_mutex_unlock(&win32_cond->mtx_waiter_count);
        WaitForSingleObject(win32_cond->waiters_done, INFINITE);
        ResetEvent(win32_cond->waiters_done);
        win32_cond->is_broadcast = 0;
    } else
        pthread_mutex_unlock(&win32_cond->mtx_waiter_count);
    pthread_mutex_unlock(&win32_cond->mtx_broadcast);
    return 0;
}

static av_unused int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    win32_cond_t *win32_cond = cond->Ptr;
    int last_waiter;
    if (cond_wait) {
        cond_wait(cond, mutex, INFINITE);
        return 0;
    }

    /* non native condition variables */
    pthread_mutex_lock(&win32_cond->mtx_broadcast);
    pthread_mutex_lock(&win32_cond->mtx_waiter_count);
    win32_cond->waiter_count++;
    pthread_mutex_unlock(&win32_cond->mtx_waiter_count);
    pthread_mutex_unlock(&win32_cond->mtx_broadcast);

    // unlock the external mutex
    pthread_mutex_unlock(mutex);
    WaitForSingleObject(win32_cond->semaphore, INFINITE);

    pthread_mutex_lock(&win32_cond->mtx_waiter_count);
    win32_cond->waiter_count--;
    last_waiter = !win32_cond->waiter_count || !win32_cond->is_broadcast;
    pthread_mutex_unlock(&win32_cond->mtx_waiter_count);

    if (last_waiter)
        SetEvent(win32_cond->waiters_done);

    // lock the external mutex
    return pthread_mutex_lock(mutex);
}

static av_unused int pthread_cond_signal(pthread_cond_t *cond)
{
    win32_cond_t *win32_cond = cond->Ptr;
    int have_waiter;
    if (cond_signal) {
        cond_signal(cond);
        return 0;
    }

    pthread_mutex_lock(&win32_cond->mtx_broadcast);

    /* non-native condition variables */
    pthread_mutex_lock(&win32_cond->mtx_waiter_count);
    have_waiter = win32_cond->waiter_count;
    pthread_mutex_unlock(&win32_cond->mtx_waiter_count);

    if (have_waiter) {
        ReleaseSemaphore(win32_cond->semaphore, 1, NULL);
        WaitForSingleObject(win32_cond->waiters_done, INFINITE);
        ResetEvent(win32_cond->waiters_done);
    }

    pthread_mutex_unlock(&win32_cond->mtx_broadcast);
    return 0;
}
#endif

static av_unused void w32thread_init(void)
{
#if _WIN32_WINNT < 0x0600
    HANDLE kernel_dll = GetModuleHandle(TEXT("kernel32.dll"));
    /* if one is available, then they should all be available */
    cond_init      =
        (void*)GetProcAddress(kernel_dll, "InitializeConditionVariable");
    cond_broadcast =
        (void*)GetProcAddress(kernel_dll, "WakeAllConditionVariable");
    cond_signal    =
        (void*)GetProcAddress(kernel_dll, "WakeConditionVariable");
    cond_wait      =
        (void*)GetProcAddress(kernel_dll, "SleepConditionVariableCS");
    initonce_begin =
        (void*)GetProcAddress(kernel_dll, "InitOnceBeginInitialize");
    initonce_complete =
        (void*)GetProcAddress(kernel_dll, "InitOnceComplete");
#endif

}

#endif /* FFMPEG_COMPAT_W32PTHREADS_H */
