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

#ifndef AVUTIL_AARCH64_CRC_H
#define AVUTIL_AARCH64_CRC_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#include "cpu.h"
#include "libavutil/attributes_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/crc.h"

FF_VISIBILITY_PUSH_HIDDEN
uint32_t ff_crc32_aarch64(const AVCRC *ctx, uint32_t crc, const uint8_t *buffer,
                          size_t length);
FF_VISIBILITY_POP_HIDDEN

static inline uint32_t ff_crc_aarch64(const AVCRC *ctx, uint32_t crc,
                                      const uint8_t *buffer, size_t length)
{
#if HAVE_ARM_CRC
    av_assert2(ctx[0] == AV_CRC_32_IEEE_LE + 1);
    return ff_crc32_aarch64(ctx, crc, buffer, length);
#else
    av_unreachable("AARCH64 has only AV_CRC_32_IEEE_LE arch-specific CRC code");
    return 0;
#endif
}

static inline const AVCRC *ff_crc_get_table_aarch64(AVCRCId crc_id)
{
#if HAVE_ARM_CRC
    static const AVCRC crc32_ieee_le_ctx[] = {
        AV_CRC_32_IEEE_LE + 1
    };

    if (crc_id != AV_CRC_32_IEEE_LE)
        return NULL;

    int cpu_flags = av_get_cpu_flags();
    if (have_arm_crc(cpu_flags)) {
        return crc32_ieee_le_ctx;
    }
#endif
    return NULL;
}

#endif /* AVUTIL_AARCH64_CRC_H */
