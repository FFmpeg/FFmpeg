/*
 * Copyright (c) 2017 Jokyo Images
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "checkasm.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/utvideodsp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define WIDTH 240
#define HEIGHT 120
#define WIDTH_PADDED (WIDTH + 16) /* padded to 32 */
#define BUFFER_SIZE (WIDTH_PADDED * HEIGHT)


#define randomize_plane(buf, type)          \
    do {                                    \
        int w, h;                           \
        type * tmp = buf;                   \
        for (h = 0; h < HEIGHT; h++) {      \
            for (w = 0; w < WIDTH; w++)     \
                tmp[w] = rnd() & 0xFF;      \
            tmp += WIDTH_PADDED;            \
        }                                   \
    } while (0)

#define cmp_plane(buf0, buf1, s)                    \
    do {                                            \
        int h;                                      \
        for (h = 0; h < HEIGHT; h++) {              \
            if (memcmp(buf0 + h*WIDTH_PADDED,       \
                buf1 + h*WIDTH_PADDED, WIDTH *s))   \
                fail();\
        }                                           \
    } while (0)


#define CHECK_RESTORE(type)\
LOCAL_ALIGNED_32(type, src_r0, [BUFFER_SIZE]);  \
LOCAL_ALIGNED_32(type, src_g0, [BUFFER_SIZE]);  \
LOCAL_ALIGNED_32(type, src_b0, [BUFFER_SIZE]);  \
LOCAL_ALIGNED_32(type, src_r1, [BUFFER_SIZE]);  \
LOCAL_ALIGNED_32(type, src_g1, [BUFFER_SIZE]);  \
LOCAL_ALIGNED_32(type, src_b1, [BUFFER_SIZE]);  \
declare_func(void, type *src_r, type *src_g, type *src_b,   \
             ptrdiff_t linesize_r, ptrdiff_t linesize_g,    \
             ptrdiff_t linesize_b, int width, int height);  \
memset(src_r0, 0, BUFFER_SIZE * sizeof(type));  \
memset(src_g0, 0, BUFFER_SIZE * sizeof(type));  \
memset(src_b0, 0, BUFFER_SIZE * sizeof(type));  \
randomize_plane(src_r0, type);                  \
randomize_plane(src_g0, type);                  \
randomize_plane(src_b0, type);                  \
memcpy(src_r1, src_r0, BUFFER_SIZE * sizeof(type));         \
memcpy(src_g1, src_g0, BUFFER_SIZE * sizeof(type));         \
memcpy(src_b1, src_b0, BUFFER_SIZE * sizeof(type));         \
call_ref(src_r0, src_g0, src_b0, WIDTH_PADDED, WIDTH_PADDED, WIDTH_PADDED, WIDTH, HEIGHT);\
call_new(src_r1, src_g1, src_b1, WIDTH_PADDED, WIDTH_PADDED, WIDTH_PADDED, WIDTH, HEIGHT);\
cmp_plane(src_r0, src_r1, sizeof(type));    \
cmp_plane(src_g0, src_g1, sizeof(type));    \
cmp_plane(src_b0, src_b1, sizeof(type));    \
bench_new(src_r1, src_g1, src_b1, WIDTH_PADDED, WIDTH_PADDED, WIDTH_PADDED, WIDTH, HEIGHT)

static void check_restore_rgb_planes(void) {
    CHECK_RESTORE(uint8_t);
}

static void check_restore_rgb_planes10(void) {
    CHECK_RESTORE(uint16_t);
}

void checkasm_check_utvideodsp(void)
{
    UTVideoDSPContext h;

    ff_utvideodsp_init(&h);

    if (check_func(h.restore_rgb_planes, "restore_rgb_planes"))
        check_restore_rgb_planes();

    report("restore_rgb_planes");

    if (check_func(h.restore_rgb_planes10, "restore_rgb_planes10"))
        check_restore_rgb_planes10();

    report("restore_rgb_planes10");
}
