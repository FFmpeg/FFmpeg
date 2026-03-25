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

#include <stdio.h>

#include "libavutil/mem.h"
#include "libavutil/spherical.h"

int main(void)
{
    AVSphericalMapping *map;
    size_t size;

    /* av_spherical_alloc with size output */
    printf("Testing av_spherical_alloc()\n");
    map = av_spherical_alloc(&size);
    if (map) {
        printf("alloc: OK, size>0=%s, default projection=%d\n",
               size > 0 ? "yes" : "no", map->projection);
        av_free(map);
    } else {
        printf("alloc: FAIL\n");
    }

    /* av_spherical_alloc without size */
    map = av_spherical_alloc(NULL);
    printf("alloc (no size): %s\n", map ? "OK" : "FAIL");
    av_free(map);

    /* av_spherical_projection_name - all valid projections */
    printf("\nTesting av_spherical_projection_name()\n");
    for (int i = 0; i <= AV_SPHERICAL_PARAMETRIC_IMMERSIVE; i++)
        printf("projection %d: %s\n", i, av_spherical_projection_name(i));
    printf("out of range: %s\n", av_spherical_projection_name(100));

    /* av_spherical_from_name - all valid names */
    printf("\nTesting av_spherical_from_name()\n");
    for (int i = 0; i <= AV_SPHERICAL_PARAMETRIC_IMMERSIVE; i++) {
        const char *name = av_spherical_projection_name(i);
        printf("%s: %d\n", name, av_spherical_from_name(name));
    }
    printf("nonexistent: %d\n", av_spherical_from_name("nonexistent"));

    /* projection name round-trip */
    printf("\nTesting projection name round-trip\n");
    for (int i = 0; i <= AV_SPHERICAL_PARAMETRIC_IMMERSIVE; i++) {
        const char *name = av_spherical_projection_name(i);
        int rt = av_spherical_from_name(name);
        printf("roundtrip %d (%s): %s\n", i, name, rt == i ? "OK" : "FAIL");
    }

    /* av_spherical_tile_bounds - no bounds (full frame) */
    printf("\nTesting av_spherical_tile_bounds()\n");
    map = av_spherical_alloc(NULL);
    if (map) {
        size_t left, top, right, bottom;

        map->projection = AV_SPHERICAL_EQUIRECTANGULAR_TILE;
        printf("projection: %s\n",
               av_spherical_projection_name(map->projection));

        map->bound_left   = 0;
        map->bound_top    = 0;
        map->bound_right  = 0;
        map->bound_bottom = 0;
        av_spherical_tile_bounds(map, 1920, 1080, &left, &top, &right, &bottom);
        printf("full frame: left=%zu top=%zu right=%zu bottom=%zu\n",
               left, top, right, bottom);

        /* quarter tile at top-left (each bound is 0.32 fixed point) */
        map->bound_left   = 0;
        map->bound_top    = 0;
        map->bound_right  = UINT32_MAX / 2;
        map->bound_bottom = UINT32_MAX / 2;
        av_spherical_tile_bounds(map, 960, 540, &left, &top, &right, &bottom);
        printf("quarter top-left: left=%zu top=%zu right=%zu bottom=%zu\n",
               left, top, right, bottom);

        /* centered tile with equal margins */
        map->bound_left   = UINT32_MAX / 4;
        map->bound_top    = UINT32_MAX / 4;
        map->bound_right  = UINT32_MAX / 4;
        map->bound_bottom = UINT32_MAX / 4;
        av_spherical_tile_bounds(map, 960, 540, &left, &top, &right, &bottom);
        printf("centered: left=%zu top=%zu right=%zu bottom=%zu\n",
               left, top, right, bottom);

        av_free(map);
    }

    return 0;
}
