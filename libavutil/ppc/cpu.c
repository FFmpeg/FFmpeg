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
#elif defined(__linux__)
#include <asm/cputable.h>
#include <linux/auxvec.h>
#include <fcntl.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#elif defined(__OpenBSD__)
#include <sys/param.h>
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
#elif defined(__APPLE__) || defined(__OpenBSD__)
#ifdef __OpenBSD__
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
                if (buf[i + 1] & PPC_FEATURE_HAS_ALTIVEC)
                    ret = AV_CPU_FLAG_ALTIVEC;
#ifdef PPC_FEATURE_HAS_VSX
                if (buf[i + 1] & PPC_FEATURE_HAS_VSX)
                    ret |= AV_CPU_FLAG_VSX;
#endif
#ifdef PPC_FEATURE_ARCH_2_07
                if (buf[i + 1] & PPC_FEATURE_HAS_POWER8)
                    ret |= AV_CPU_FLAG_POWER8;
#endif
                if (ret & AV_CPU_FLAG_VSX)
                    av_assert0(ret & AV_CPU_FLAG_ALTIVEC);
                goto out;
            }
        }
    }

out:
    close(fd);
    return ret;
#elif CONFIG_RUNTIME_CPUDETECT && defined(__linux__)
#define PVR_G4_7400  0x000C
#define PVR_G5_970   0x0039
#define PVR_G5_970FX 0x003C
#define PVR_G5_970MP 0x0044
#define PVR_G5_970GX 0x0045
#define PVR_POWER6   0x003E
#define PVR_POWER7   0x003F
#define PVR_POWER8   0x004B
#define PVR_CELL_PPU 0x0070
    int ret = 0;
    int proc_ver;
    // Support of mfspr PVR emulation added in Linux 2.6.17.
    __asm__ volatile("mfspr %0, 287" : "=r" (proc_ver));
    proc_ver >>= 16;
    if (proc_ver  & 0x8000 ||
        proc_ver == PVR_G4_7400  ||
        proc_ver == PVR_G5_970   ||
        proc_ver == PVR_G5_970FX ||
        proc_ver == PVR_G5_970MP ||
        proc_ver == PVR_G5_970GX ||
        proc_ver == PVR_POWER6   ||
        proc_ver == PVR_POWER7   ||
        proc_ver == PVR_POWER8   ||
        proc_ver == PVR_CELL_PPU)
        ret = AV_CPU_FLAG_ALTIVEC;
    if (proc_ver == PVR_POWER7 ||
        proc_ver == PVR_POWER8)
        ret |= AV_CPU_FLAG_VSX;
    if (proc_ver == PVR_POWER8)
        ret |= AV_CPU_FLAG_POWER8;

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
