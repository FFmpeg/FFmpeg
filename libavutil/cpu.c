/*
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

#include <stdint.h>

#include "cpu.h"
#include "cpu_internal.h"
#include "config.h"
#include "opt.h"
#include "common.h"

#if HAVE_SCHED_GETAFFINITY
#define _GNU_SOURCE
#include <sched.h>
#endif
#if HAVE_GETPROCESSAFFINITYMASK
#include <windows.h>
#endif
#if HAVE_SYSCTL
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#if HAVE_SYSCONF
#include <unistd.h>
#endif

static int cpuflags_mask = -1, checked;

int av_get_cpu_flags(void)
{
    static int flags;

    if (checked)
        return flags;

    if (ARCH_AARCH64)
        flags = ff_get_cpu_flags_aarch64();
    if (ARCH_ARM)
        flags = ff_get_cpu_flags_arm();
    if (ARCH_PPC)
        flags = ff_get_cpu_flags_ppc();
    if (ARCH_X86)
        flags = ff_get_cpu_flags_x86();

    flags  &= cpuflags_mask;
    checked = 1;

    return flags;
}

void av_set_cpu_flags_mask(int mask)
{
    cpuflags_mask = mask;
    checked       = 0;
}

int av_parse_cpu_flags(const char *s)
{
#define CPUFLAG_MMXEXT   (AV_CPU_FLAG_MMX      | AV_CPU_FLAG_MMXEXT | AV_CPU_FLAG_CMOV)
#define CPUFLAG_3DNOW    (AV_CPU_FLAG_3DNOW    | AV_CPU_FLAG_MMX)
#define CPUFLAG_3DNOWEXT (AV_CPU_FLAG_3DNOWEXT | CPUFLAG_3DNOW)
#define CPUFLAG_SSE      (AV_CPU_FLAG_SSE      | CPUFLAG_MMXEXT)
#define CPUFLAG_SSE2     (AV_CPU_FLAG_SSE2     | CPUFLAG_SSE)
#define CPUFLAG_SSE2SLOW (AV_CPU_FLAG_SSE2SLOW | CPUFLAG_SSE2)
#define CPUFLAG_SSE3     (AV_CPU_FLAG_SSE3     | CPUFLAG_SSE2)
#define CPUFLAG_SSE3SLOW (AV_CPU_FLAG_SSE3SLOW | CPUFLAG_SSE3)
#define CPUFLAG_SSSE3    (AV_CPU_FLAG_SSSE3    | CPUFLAG_SSE3)
#define CPUFLAG_SSE4     (AV_CPU_FLAG_SSE4     | CPUFLAG_SSSE3)
#define CPUFLAG_SSE42    (AV_CPU_FLAG_SSE42    | CPUFLAG_SSE4)
#define CPUFLAG_AVX      (AV_CPU_FLAG_AVX      | CPUFLAG_SSE42)
#define CPUFLAG_AVXSLOW  (AV_CPU_FLAG_AVXSLOW  | CPUFLAG_AVX)
#define CPUFLAG_XOP      (AV_CPU_FLAG_XOP      | CPUFLAG_AVX)
#define CPUFLAG_FMA3     (AV_CPU_FLAG_FMA3     | CPUFLAG_AVX)
#define CPUFLAG_FMA4     (AV_CPU_FLAG_FMA4     | CPUFLAG_AVX)
#define CPUFLAG_AVX2     (AV_CPU_FLAG_AVX2     | CPUFLAG_AVX)
#define CPUFLAG_BMI2     (AV_CPU_FLAG_BMI2     | AV_CPU_FLAG_BMI1)
    static const AVOption cpuflags_opts[] = {
        { "flags"   , NULL, 0, AV_OPT_TYPE_FLAGS, { .i64 = 0 }, INT64_MIN, INT64_MAX, .unit = "flags" },
#if   ARCH_PPC
        { "altivec" , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ALTIVEC  },    .unit = "flags" },
#elif ARCH_X86
        { "mmx"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_MMX      },    .unit = "flags" },
        { "mmxext"  , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_MMXEXT       },    .unit = "flags" },
        { "sse"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE          },    .unit = "flags" },
        { "sse2"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE2         },    .unit = "flags" },
        { "sse2slow", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE2SLOW     },    .unit = "flags" },
        { "sse3"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE3         },    .unit = "flags" },
        { "sse3slow", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE3SLOW     },    .unit = "flags" },
        { "ssse3"   , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSSE3        },    .unit = "flags" },
        { "atom"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ATOM     },    .unit = "flags" },
        { "sse4.1"  , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE4         },    .unit = "flags" },
        { "sse4.2"  , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_SSE42        },    .unit = "flags" },
        { "avx"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_AVX          },    .unit = "flags" },
        { "avxslow" , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_AVXSLOW      },    .unit = "flags" },
        { "xop"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_XOP          },    .unit = "flags" },
        { "fma3"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_FMA3         },    .unit = "flags" },
        { "fma4"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_FMA4         },    .unit = "flags" },
        { "avx2"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_AVX2         },    .unit = "flags" },
        { "bmi1"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_BMI1     },    .unit = "flags" },
        { "bmi2"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_BMI2         },    .unit = "flags" },
        { "3dnow"   , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_3DNOW        },    .unit = "flags" },
        { "3dnowext", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CPUFLAG_3DNOWEXT     },    .unit = "flags" },
        { "cmov",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_CMOV     },    .unit = "flags" },
#elif ARCH_ARM
        { "armv5te",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ARMV5TE  },    .unit = "flags" },
        { "armv6",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ARMV6    },    .unit = "flags" },
        { "armv6t2",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ARMV6T2  },    .unit = "flags" },
        { "vfp",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_VFP      },    .unit = "flags" },
        { "vfp_vm",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_VFP_VM   },    .unit = "flags" },
        { "vfpv3",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_VFPV3    },    .unit = "flags" },
        { "neon",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_NEON     },    .unit = "flags" },
#elif ARCH_AARCH64
        { "armv8",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_ARMV8    },    .unit = "flags" },
        { "neon",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_NEON     },    .unit = "flags" },
        { "vfp",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_CPU_FLAG_VFP      },    .unit = "flags" },
#endif
        { NULL },
    };
    static const AVClass class = {
        .class_name = "cpuflags",
        .item_name  = av_default_item_name,
        .option     = cpuflags_opts,
        .version    = LIBAVUTIL_VERSION_INT,
    };

    int flags = 0, ret;
    const AVClass *pclass = &class;

    if ((ret = av_opt_eval_flags(&pclass, &cpuflags_opts[0], s, &flags)) < 0)
        return ret;

    return flags & INT_MAX;
}

int av_cpu_count(void)
{
    int nb_cpus = 1;
#if HAVE_SCHED_GETAFFINITY && defined(CPU_COUNT)
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    if (!sched_getaffinity(0, sizeof(cpuset), &cpuset))
        nb_cpus = CPU_COUNT(&cpuset);
#elif HAVE_GETPROCESSAFFINITYMASK
    DWORD_PTR proc_aff, sys_aff;
    if (GetProcessAffinityMask(GetCurrentProcess(), &proc_aff, &sys_aff))
        nb_cpus = av_popcount64(proc_aff);
#elif HAVE_SYSCTL && defined(HW_NCPU)
    int mib[2] = { CTL_HW, HW_NCPU };
    size_t len = sizeof(nb_cpus);

    if (sysctl(mib, 2, &nb_cpus, &len, NULL, 0) == -1)
        nb_cpus = 0;
#elif HAVE_SYSCONF && defined(_SC_NPROC_ONLN)
    nb_cpus = sysconf(_SC_NPROC_ONLN);
#elif HAVE_SYSCONF && defined(_SC_NPROCESSORS_ONLN)
    nb_cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    return nb_cpus;
}
