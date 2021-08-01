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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/cpu.h"
#include "libavutil/error.h"
#include "libavutil/imgutils_internal.h"
#include "libavutil/macros.h"

#include "cpu.h"

void ff_image_copy_plane_uc_from_sse4(uint8_t *dst, ptrdiff_t dst_linesize,
                                      const uint8_t *src, ptrdiff_t src_linesize,
                                      ptrdiff_t bytewidth, int height);

int ff_image_copy_plane_uc_from_x86(uint8_t       *dst, ptrdiff_t dst_linesize,
                                    const uint8_t *src, ptrdiff_t src_linesize,
                                    ptrdiff_t bytewidth, int height)
{
    int cpu_flags = av_get_cpu_flags();
    ptrdiff_t bw_aligned = FFALIGN(bytewidth, 64);

    if (EXTERNAL_SSE4(cpu_flags) &&
        bw_aligned <= dst_linesize && bw_aligned <= src_linesize)
        ff_image_copy_plane_uc_from_sse4(dst, dst_linesize, src, src_linesize,
                                         bw_aligned, height);
    else
        return AVERROR(ENOSYS);

    return 0;
}
