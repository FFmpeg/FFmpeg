/*
 * Copyright (c) 2025 Arpad Panyik <Arpad.Panyik@arm.com>
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define NUM_LINES 4
#define MAX_LINE_SIZE 1920

#define randomize_buffers(buf, size)      \
    do {                                  \
        for (int j = 0; j < size; j += 2) \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static void check_xyz12Torgb48le(void)
{
    static const int input_sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 17, 21, 31,
                                      32, 64, 128, 256, 512, 1024,
                                      MAX_LINE_SIZE};

    const int src_stride = 3 * sizeof(uint16_t) * MAX_LINE_SIZE;
    const int dst_stride = src_stride;

    const int src_pix_fmt = AV_PIX_FMT_XYZ12LE;
    const int dst_pix_fmt = AV_PIX_FMT_RGB48LE;

    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);

    LOCAL_ALIGNED_8(uint16_t, src,     [3 * MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint16_t, dst_ref, [3 * MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint16_t, dst_new, [3 * MAX_LINE_SIZE * NUM_LINES]);

    declare_func(void, const SwsInternal *, uint8_t *, int, const uint8_t *,
                 int, int, int);

    SwsInternal c;
    memset(&c, 0, sizeof(c));
    c.opts.src_format = src_pix_fmt;
    ff_sws_init_xyzdsp(&c);
    ff_sws_fill_xyztables(&c);

    randomize_buffers(src, 3 * MAX_LINE_SIZE * NUM_LINES);

    for (int height = 1; height <= NUM_LINES; height++) {
        for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
            int width = input_sizes[isi];

            if (check_func(c.xyz12Torgb48, "%s_%s_%dx%d", src_desc->name,
                           dst_desc->name, width, height)) {
                memset(dst_ref, 0xFE,
                       3 * sizeof(uint16_t) * MAX_LINE_SIZE * NUM_LINES);
                memset(dst_new, 0xFE,
                       3 * sizeof(uint16_t) * MAX_LINE_SIZE * NUM_LINES);

                call_ref(&c, (uint8_t *)dst_ref, dst_stride,
                         (const uint8_t *)src, src_stride, width, height);
                call_new(&c, (uint8_t *)dst_new, dst_stride,
                         (const uint8_t *)src, src_stride, width, height);

                checkasm_check(uint16_t, dst_ref, dst_stride, dst_new,
                               dst_stride, width, height, "dst_rgb");

                if (!(width & 3) && height == NUM_LINES) {
                    bench_new(&c, (uint8_t *)dst_new,
                              dst_stride, (const uint8_t *)src, src_stride,
                              width, height);
                }
            }
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE

void checkasm_check_sw_xyz2rgb(void)
{
    check_xyz12Torgb48le();
    report("xyz12Torgb48le");
}
