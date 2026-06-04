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

#if HAVE_LINUX_PERF

  #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
  #endif

  #include <linux/perf_event.h>
  #include <sys/ioctl.h>
  #include <sys/syscall.h>
  #include <unistd.h>

  #include "internal.h"

static int perf_sysfd = -1;

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
        perf_sysfd = (int) syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
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
