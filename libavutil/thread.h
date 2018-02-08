/*
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

// This header should only be used to simplify code where
// threading is optional, not as a generic threading abstraction.

#ifndef AVUTIL_THREAD_H
#define AVUTIL_THREAD_H

#include "config.h"

#if HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS2THREADS

#if HAVE_PTHREADS
#include <pthread.h>

#if defined(ASSERT_LEVEL) && ASSERT_LEVEL > 1

#include "log.h"

#define ASSERT_PTHREAD_NORET(func, ...) do {                            \
    int ret = func(__VA_ARGS__);                                        \
    if (ret) {                                                          \
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = "";                     \
        av_log(NULL, AV_LOG_FATAL, AV_STRINGIFY(func)                   \
               " failed with error: %s\n",                              \
               av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE,   \
                                    AVERROR(ret)));                     \
        abort();                                                        \
    }                                                                   \
} while (0)

#define ASSERT_PTHREAD(func, ...) do {                                  \
    ASSERT_PTHREAD_NORET(func, __VA_ARGS__);                            \
    return 0;                                                           \
} while (0)

static inline int strict_pthread_join(pthread_t thread, void **value_ptr)
{
    ASSERT_PTHREAD(pthread_join, thread, value_ptr);
}

static inline int strict_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (attr) {
        ASSERT_PTHREAD_NORET(pthread_mutex_init, mutex, attr);
    } else {
        pthread_mutexattr_t local_attr;
        ASSERT_PTHREAD_NORET(pthread_mutexattr_init, &local_attr);
        ASSERT_PTHREAD_NORET(pthread_mutexattr_settype, &local_attr, PTHREAD_MUTEX_ERRORCHECK);
        ASSERT_PTHREAD_NORET(pthread_mutex_init, mutex, &local_attr);
        ASSERT_PTHREAD_NORET(pthread_mutexattr_destroy, &local_attr);
    }
    return 0;
}

static inline int strict_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    ASSERT_PTHREAD(pthread_mutex_destroy, mutex);
}

static inline int strict_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    ASSERT_PTHREAD(pthread_mutex_lock, mutex);
}

static inline int strict_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    ASSERT_PTHREAD(pthread_mutex_unlock, mutex);
}

static inline int strict_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    ASSERT_PTHREAD(pthread_cond_init, cond, attr);
}

static inline int strict_pthread_cond_destroy(pthread_cond_t *cond)
{
    ASSERT_PTHREAD(pthread_cond_destroy, cond);
}

static inline int strict_pthread_cond_signal(pthread_cond_t *cond)
{
    ASSERT_PTHREAD(pthread_cond_signal, cond);
}

static inline int strict_pthread_cond_broadcast(pthread_cond_t *cond)
{
    ASSERT_PTHREAD(pthread_cond_broadcast, cond);
}

static inline int strict_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    ASSERT_PTHREAD(pthread_cond_wait, cond, mutex);
}

static inline int strict_pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    ASSERT_PTHREAD(pthread_once, once_control, init_routine);
}

#define pthread_join           strict_pthread_join
#define pthread_mutex_init     strict_pthread_mutex_init
#define pthread_mutex_destroy  strict_pthread_mutex_destroy
#define pthread_mutex_lock     strict_pthread_mutex_lock
#define pthread_mutex_unlock   strict_pthread_mutex_unlock
#define pthread_cond_init      strict_pthread_cond_init
#define pthread_cond_destroy   strict_pthread_cond_destroy
#define pthread_cond_signal    strict_pthread_cond_signal
#define pthread_cond_broadcast strict_pthread_cond_broadcast
#define pthread_cond_wait      strict_pthread_cond_wait
#define pthread_once           strict_pthread_once
#endif

#elif HAVE_OS2THREADS
#include "compat/os2threads.h"
#else
#include "compat/w32pthreads.h"
#endif

#define AVMutex pthread_mutex_t
#define AV_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

#define ff_mutex_init    pthread_mutex_init
#define ff_mutex_lock    pthread_mutex_lock
#define ff_mutex_unlock  pthread_mutex_unlock
#define ff_mutex_destroy pthread_mutex_destroy

#define AVOnce pthread_once_t
#define AV_ONCE_INIT PTHREAD_ONCE_INIT

#define ff_thread_once(control, routine) pthread_once(control, routine)

#else

#define AVMutex char
#define AV_MUTEX_INITIALIZER 0

static inline int ff_mutex_init(AVMutex *mutex, const void *attr){ return 0; }
static inline int ff_mutex_lock(AVMutex *mutex){ return 0; }
static inline int ff_mutex_unlock(AVMutex *mutex){ return 0; }
static inline int ff_mutex_destroy(AVMutex *mutex){ return 0; }

#define AVOnce char
#define AV_ONCE_INIT 0

static inline int ff_thread_once(char *control, void (*routine)(void))
{
    if (!*control) {
        routine();
        *control = 1;
    }
    return 0;
}

#endif

#endif /* AVUTIL_THREAD_H */
