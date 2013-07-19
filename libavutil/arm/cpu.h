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

#ifndef AVUTIL_ARM_CPU_H
#define AVUTIL_ARM_CPU_H

#include "config.h"
#include "libavutil/cpu.h"

#define have_armv5te(flags) (HAVE_ARMV5TE && ((flags) & AV_CPU_FLAG_ARMV5TE))
#define have_armv6(flags)   (HAVE_ARMV6   && ((flags) & AV_CPU_FLAG_ARMV6))
#define have_armv6t2(flags) (HAVE_ARMV6T2 && ((flags) & AV_CPU_FLAG_ARMV6T2))
#define have_vfp(flags)     (HAVE_VFP     && ((flags) & AV_CPU_FLAG_VFP))
#define have_vfpv3(flags)   (HAVE_VFPV3   && ((flags) & AV_CPU_FLAG_VFPV3))
#define have_neon(flags)    (HAVE_NEON    && ((flags) & AV_CPU_FLAG_NEON))

#endif /* AVUTIL_ARM_CPU_H */
