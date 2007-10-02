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


/**
 * @file check_altivec.c
 * Checks for AltiVec presence.
 */

#ifdef __APPLE__
#include <sys/sysctl.h>
#elif __AMIGAOS4__
#include <exec/exec.h>
#include <interfaces/exec.h>
#include <proto/exec.h>
#else
#include <signal.h>
#include <setjmp.h>

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump = 0;

static void sigill_handler (int sig)
{
    if (!canjump) {
        signal (sig, SIG_DFL);
        raise (sig);
    }

    canjump = 0;
    siglongjmp (jmpbuf, 1);
}
#endif /* __APPLE__ */

/**
 * This function MAY rely on signal() or fork() in order to make sure altivec
 * is present
 */

int has_altivec(void)
{
#ifdef __AMIGAOS4__
    ULONG result = 0;
    extern struct ExecIFace *IExec;

    IExec->GetCPUInfoTags(GCIT_VectorUnit, &result, TAG_DONE);
    if (result == VECTORTYPE_ALTIVEC) return 1;
    return 0;
#elif __APPLE__
    int sels[2] = {CTL_HW, HW_VECTORUNIT};
    int has_vu = 0;
    size_t len = sizeof(has_vu);
    int err;

    err = sysctl(sels, 2, &has_vu, &len, NULL, 0);

    if (err == 0) return (has_vu != 0);
    return 0;
#else
/* Do it the brute-force way, borrowed from the libmpeg2 library. */
    {
      signal (SIGILL, sigill_handler);
      if (sigsetjmp (jmpbuf, 1)) {
        signal (SIGILL, SIG_DFL);
      } else {
        canjump = 1;

        asm volatile ("mtspr 256, %0\n\t"
                      "vand %%v0, %%v0, %%v0"
                      :
                      : "r" (-1));

        signal (SIGILL, SIG_DFL);
        return 1;
      }
    }
    return 0;
#endif /* __AMIGAOS4__ */
}

