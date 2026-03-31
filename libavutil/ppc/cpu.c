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

#include "config.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
#ifdef __FreeBSD__
#include <machine/cpu.h>
#endif
#include <sys/auxv.h>
#elif defined(__linux__)
#include <asm/cputable.h>
#include <linux/auxvec.h>
#include <fcntl.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#elif defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#elif defined(__AMIGAOS4__)
#include <exec/exec.h>
#include <interfaces/exec.h>
#include <proto/exec.h>
#endif /* __APPLE__ */

#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/cpu_internal.h"

#ifndef AT_HWCAP
#define AT_HWCAP             16
#endif
#ifndef AT_HWCAP2
#define AT_HWCAP2            26
#endif

#define HWCAP_PPC_VSX        (1U << 7)
#define HWCAP_PPC_ALTIVEC    (1U << 28)
#define HWCAP2_PPC_ARCH_2_07 (1U << 31)

/**
 * This function MAY rely on signal() or fork() in order to make sure AltiVec
 * is present.
 */
int ff_get_cpu_flags_ppc(void)
{
#if HAVE_ALTIVEC
#ifdef __AMIGAOS4__
    ULONG result = 0;
    extern struct ExecIFace *IExec;

    IExec->GetCPUInfoTags(GCIT_VectorUnit, &result, TAG_DONE);
    if (result == VECTORTYPE_ALTIVEC)
        return AV_CPU_FLAG_ALTIVEC;
    return 0;
#elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    int flags = 0;

    unsigned long hwcap = ff_getauxval(AT_HWCAP);
    unsigned long hwcap2 = ff_getauxval(AT_HWCAP2);

    if (hwcap & HWCAP_PPC_ALTIVEC)
       flags |= AV_CPU_FLAG_ALTIVEC;
    if (hwcap & HWCAP_PPC_VSX)
       flags |= AV_CPU_FLAG_VSX;
    if (hwcap2 & HWCAP2_PPC_ARCH_2_07)
       flags |= AV_CPU_FLAG_POWER8;

    return flags;
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
#if defined(__NetBSD__) || defined(__OpenBSD__)
    int sels[2] = {CTL_MACHDEP, CPU_ALTIVEC};
#else
    int sels[2] = {CTL_HW, HW_VECTORUNIT};
#endif
    int has_vu = 0;
    size_t len = sizeof(has_vu);
    int err;

    err = sysctl(sels, 2, &has_vu, &len, NULL, 0);

    if (err == 0)
        return has_vu ? AV_CPU_FLAG_ALTIVEC : 0;
    return 0;
#elif defined(__linux__)
    // The linux kernel could have the altivec support disabled
    // even if the cpu has it.
    int i, ret = 0;
    int fd = open("/proc/self/auxv", O_RDONLY);
    unsigned long buf[64] = { 0 };
    ssize_t count;

    if (fd < 0)
        return 0;

    while ((count = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < count / sizeof(*buf); i += 2) {
            if (buf[i] == AT_NULL)
                goto out;
            if (buf[i] == AT_HWCAP) {
                if (buf[i + 1] & HWCAP_PPC_ALTIVEC)
                    ret = AV_CPU_FLAG_ALTIVEC;
                if (buf[i + 1] & HWCAP_PPC_VSX)
                    ret |= AV_CPU_FLAG_VSX;
                if (ret & AV_CPU_FLAG_VSX)
                    av_assert0(ret & AV_CPU_FLAG_ALTIVEC);
            }
            else if (buf[i] == AT_HWCAP2) {
                if (buf[i + 1] & HWCAP2_PPC_ARCH_2_07)
                    ret |= AV_CPU_FLAG_POWER8;
            }
        }
    }

out:
    close(fd);
    return ret;
#else
    // Since we were compiled for AltiVec, just assume we have it
    // until someone comes up with a proper way (not involving signal hacks).
    return AV_CPU_FLAG_ALTIVEC;
#endif /* __AMIGAOS4__ */
#endif /* HAVE_ALTIVEC */
    return 0;
}

size_t ff_get_cpu_max_align_ppc(void)
{
    int flags = av_get_cpu_flags();

    if (flags & (AV_CPU_FLAG_ALTIVEC   |
                 AV_CPU_FLAG_VSX       |
                 AV_CPU_FLAG_POWER8))
        return 16;

    return 8;
}
