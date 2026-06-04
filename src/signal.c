/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <signal.h>
#include <stdarg.h>

#include "checkasm_config.h"

#include "checkasm/test.h"
#include "internal.h"

#ifdef _WIN32
  #include <windows.h>
  #ifndef SIGBUS
    /* non-standard, use the same value as mingw-w64 */
    #define SIGBUS 10
  #endif
#endif

checkasm_jmp_buf checkasm_context;

static volatile sig_atomic_t sig; // SIG_ATOMIC_MAX = signal handling enabled

volatile sig_atomic_t checkasm_interrupted;

void checkasm_set_signal_handler_state(const int enabled)
{
    sig = enabled ? SIG_ATOMIC_MAX : 0;
}

static void interrupt_handler(const int s)
{
    checkasm_interrupted = s;

    /* If we happen to be currently executing a test function, we should jump
     * directly out of it; since it may take an arbitrary amount of time */
    if (sig == SIG_ATOMIC_MAX) {
        sig = s;
        checkasm_load_context(checkasm_context);
    }

    /* Don't re-set the signal handler, to let the next signal trigger the
     * default action */
}

#ifdef _WIN32

  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
static LONG NTAPI signal_handler(EXCEPTION_POINTERS *const e)
{
    if (sig == SIG_ATOMIC_MAX) {
        int s;
        switch (e->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    s = SIGFPE; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:      s = SIGILL; break;
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_STACK_OVERFLOW:        s = SIGSEGV; break;
        case EXCEPTION_IN_PAGE_ERROR:         s = SIGBUS; break;
        default:                              return EXCEPTION_CONTINUE_SEARCH;
        }
        sig = s;
        checkasm_load_context(checkasm_context);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
  #endif

#elif HAVE_SIGACTION && defined(SA_RESETHAND)

static void signal_handler(int s);

static const struct sigaction interrupt_handler_act = {
    .sa_handler = interrupt_handler,
    .sa_flags   = SA_RESETHAND,
};

static const struct sigaction signal_handler_act = {
    .sa_handler = signal_handler,
    .sa_flags   = SA_RESETHAND,
};

static void signal_handler(const int s)
{
    if (sig == SIG_ATOMIC_MAX) {
        sig = s;
        sigaction(s, &signal_handler_act, NULL);
        checkasm_load_context(checkasm_context);
    }
}
#else

static void signal_handler(const int s)
{
    if (sig == SIG_ATOMIC_MAX) {
        sig = s;
        signal(s, signal_handler);
        checkasm_load_context(checkasm_context);
    }
}
#endif

COLD void checkasm_set_signal_handlers(void)
{
    static int handlers_set;
    if (handlers_set)
        return;

#ifdef _WIN32
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    AddVectoredExceptionHandler(0, signal_handler);
  #endif
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);
#elif HAVE_SIGACTION && defined(SA_RESETHAND)
#ifdef SIGBUS
    sigaction(SIGBUS, &signal_handler_act, NULL);
#endif
    sigaction(SIGFPE, &signal_handler_act, NULL);
    sigaction(SIGILL, &signal_handler_act, NULL);
    sigaction(SIGSEGV, &signal_handler_act, NULL);
    sigaction(SIGINT, &interrupt_handler_act, NULL);
    sigaction(SIGTERM, &interrupt_handler_act, NULL);
#else
#ifdef SIGBUS
    signal(SIGBUS, signal_handler);
#endif
    signal(SIGFPE, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);
#endif

    handlers_set = 1;
}

const char *checkasm_get_last_signal_desc(void)
{
    switch (sig) {
    case SIGFPE:  return "fatal arithmetic error";
    case SIGILL:  return "illegal instruction";
#ifdef SIGBUS
    case SIGBUS:  return "bus error";
#endif
    case SIGSEGV: return "segmentation fault";
    case SIGINT:  return "interrupted";
    case SIGTERM: return "terminated";
    default:      return NULL;
    }
}
