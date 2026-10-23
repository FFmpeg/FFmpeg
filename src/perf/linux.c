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

#include "checkasm_config.h"

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include "internal.h"

#if HAVE_LINUX_PERF

  #include <linux/perf_event.h>
  #include <sys/ioctl.h>
  #include <sys/syscall.h>
  #include <unistd.h>

  #if HAVE_PTHREAD_SETAFFINITY_NP
    #include <pthread.h>
  #endif

static int perf_sysfd = -1;

/* The pinned CPU index, or -1 unless the thread is pinned to one CPU */
static int get_pinned_cpu(void)
{
#if HAVE_PTHREAD_SETAFFINITY_NP && defined(CPU_SET)
    cpu_set_t mask;
    int cpu = -1;

    if (pthread_getaffinity_np(pthread_self(), sizeof(mask), &mask))
        return -1;

    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (!CPU_ISSET(i, &mask))
            continue;
        if (cpu >= 0)
            return -1;
        cpu = i;
    }

    return cpu;
#else
    return -1;
#endif
}

static uint64_t perf_start(void)
{
    ioctl(perf_sysfd, PERF_EVENT_IOC_RESET, 0);
    ioctl(perf_sysfd, PERF_EVENT_IOC_ENABLE, 0);
    return 0;
}

static uint64_t perf_stop(uint64_t t)
{
    ioctl(perf_sysfd, PERF_EVENT_IOC_DISABLE, 0);
    long ret = read(perf_sysfd, &t, sizeof(t));
    (void) ret;
    return t;
}

COLD int checkasm_perf_init_linux(CheckasmPerf *perf)
{
    struct perf_event_attr attr = {
        .type           = PERF_TYPE_HARDWARE,
        .size           = sizeof(struct perf_event_attr),
        .config         = PERF_COUNT_HW_CPU_CYCLES,
        .disabled       = 1, // start counting only on demand
        .exclude_kernel = 1,
        .exclude_hv     = 1,
#if !ARCH_X86
        .exclude_guest = 1,
#endif
    };

    if (perf_sysfd == -1) {
        perf_sysfd = (int) syscall(SYS_perf_event_open, &attr, /*pid*/ 0,
                                   get_pinned_cpu(), /*group_fd*/ -1,
                                   /*flags*/ 0);
        if (perf_sysfd == -1) {
            perror("perf_event_open");
            return 1;
        }
    }

    perf->start = perf_start;
    perf->stop  = perf_stop;
    perf->name  = "linux (perf)";
    perf->unit  = "tick";
    return checkasm_perf_validate_start_stop(perf);
}

#endif /* HAVE_LINUX_PERF */
