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
 * @file
 * Scene SAD funtions
 */

#include "scene_sad.h"

void ff_scene_sad16_c(SCENE_SAD_PARAMS)
{
    uint64_t sad = 0;
    const uint16_t *src1w = (const uint16_t *)src1;
    const uint16_t *src2w = (const uint16_t *)src2;
    int x, y;

    stride1 /= 2;
    stride2 /= 2;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            sad += FFABS(src1w[x] - src2w[x]);
        src1w += stride1;
        src2w += stride2;
    }
    *sum = sad;
}

void ff_scene_sad_c(SCENE_SAD_PARAMS)
{
    uint64_t sad = 0;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            sad += FFABS(src1[x] - src2[x]);
        src1 += stride1;
        src2 += stride2;
    }
    *sum = sad;
}

ff_scene_sad_fn ff_scene_sad_get_fn(int depth)
{
    ff_scene_sad_fn sad = NULL;
    if (ARCH_X86)
        sad = ff_scene_sad_get_fn_x86(depth);
    if (!sad) {
        if (depth == 8)
            sad = ff_scene_sad_c;
        if (depth == 16)
            sad = ff_scene_sad16_c;
    }
    return sad;
}

