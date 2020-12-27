/*
 * Copyright (c) 2016 Vittorio Giovara <vittorio.giovara@gmail.com>
 *
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

#include "mem.h"
#include "spherical.h"

AVSphericalMapping *av_spherical_alloc(size_t *size)
{
    AVSphericalMapping *spherical = av_mallocz(sizeof(AVSphericalMapping));
    if (!spherical)
        return NULL;

    if (size)
        *size = sizeof(*spherical);

    return spherical;
}

void av_spherical_tile_bounds(const AVSphericalMapping *map,
                              size_t width, size_t height,
                              size_t *left, size_t *top,
                              size_t *right, size_t *bottom)
{
    /* conversion from 0.32 coordinates to pixels */
    uint64_t orig_width  = (uint64_t) width  * UINT32_MAX /
                           (UINT32_MAX - map->bound_right  - map->bound_left);
    uint64_t orig_height = (uint64_t) height * UINT32_MAX /
                           (UINT32_MAX - map->bound_bottom - map->bound_top);

    /* add a (UINT32_MAX - 1) to round up integer division */
    *left   = (orig_width  * map->bound_left + UINT32_MAX - 1) / UINT32_MAX;
    *top    = (orig_height * map->bound_top  + UINT32_MAX - 1) / UINT32_MAX;
    *right  = orig_width  - width  - *left;
    *bottom = orig_height - height - *top;
}

static const char *const spherical_projection_names[] = {
    [AV_SPHERICAL_EQUIRECTANGULAR]      = "equirectangular",
    [AV_SPHERICAL_CUBEMAP]              = "cubemap",
    [AV_SPHERICAL_EQUIRECTANGULAR_TILE] = "tiled equirectangular",
};

const char *av_spherical_projection_name(enum AVSphericalProjection projection)
{
    if ((unsigned)projection >= FF_ARRAY_ELEMS(spherical_projection_names))
        return "unknown";

    return spherical_projection_names[projection];
}

int av_spherical_from_name(const char *name)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(spherical_projection_names); i++) {
        size_t len = strlen(spherical_projection_names[i]);
        if (!strncmp(spherical_projection_names[i], name, len))
            return i;
    }

    return -1;
}
