/*
 * thirdpel DSP functions
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

/**
 * @file
 * thirdpel DSP functions
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "tpeldsp.h"

#define BIT_DEPTH 8
#include "pel_template.c"

static inline void put_tpel_pixels_mc00_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    switch (width) {
    case 2:
        put_pixels2_8_c(dst, src, stride, height);
        break;
    case 4:
        put_pixels4_8_c(dst, src, stride, height);
        break;
    case 8:
        put_pixels8_8_c(dst, src, stride, height);
        break;
    case 16:
        put_pixels16_8_c(dst, src, stride, height);
        break;
    }
}

static inline void put_tpel_pixels_mc10_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((2 * src[j] + src[j + 1] + 1) *
                      683) >> 11;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc20_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((src[j] + 2 * src[j + 1] + 1) *
                      683) >> 11;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc01_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((2 * src[j] + src[j + stride] + 1) *
                      683) >> 11;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc11_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((4 * src[j]          + 3 * src[j + 1] +
                       3 * src[j + stride] + 2 * src[j + stride + 1] + 6) *
                      2731) >> 15;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc12_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((3 * src[j]          + 2 * src[j + 1] +
                       4 * src[j + stride] + 3 * src[j + stride + 1] + 6) *
                      2731) >> 15;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc02_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((src[j] + 2 * src[j + stride] + 1) *
                      683) >> 11;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc21_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((3 * src[j]          + 4 * src[j + 1] +
                       2 * src[j + stride] + 3 * src[j + stride + 1] + 6) *
                      2731) >> 15;
        src += stride;
        dst += stride;
    }
}

static inline void put_tpel_pixels_mc22_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = ((2 * src[j]          + 3 * src[j + 1] +
                       3 * src[j + stride] + 4 * src[j + stride + 1] + 6) *
                      2731) >> 15;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc00_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    switch (width) {
    case 2:
        avg_pixels2_8_c(dst, src, stride, height);
        break;
    case 4:
        avg_pixels4_8_c(dst, src, stride, height);
        break;
    case 8:
        avg_pixels8_8_c(dst, src, stride, height);
        break;
    case 16:
        avg_pixels16_8_c(dst, src, stride, height);
        break;
    }
}

static inline void avg_tpel_pixels_mc10_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((2 * src[j] + src[j + 1] + 1) *
                        683) >> 11) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc20_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((src[j] + 2 * src[j + 1] + 1) *
                        683) >> 11) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc01_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((2 * src[j] + src[j + stride] + 1) *
                        683) >> 11) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc11_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((4 * src[j]          + 3 * src[j + 1] +
                         3 * src[j + stride] + 2 * src[j + stride + 1] + 6) *
                        2731) >> 15) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc12_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((3 * src[j]          + 2 * src[j + 1] +
                         4 * src[j + stride] + 3 * src[j + stride + 1] + 6) *
                        2731) >> 15) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc02_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((src[j] + 2 * src[j + stride] + 1) *
                        683) >> 11) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc21_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((3 * src[j]          + 4 * src[j + 1] +
                         2 * src[j + stride] + 3 * src[j + stride + 1] + 6) *
                        2731) >> 15) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

static inline void avg_tpel_pixels_mc22_c(uint8_t *dst, const uint8_t *src,
                                          int stride, int width, int height)
{
    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++)
            dst[j] = (dst[j] +
                      (((2 * src[j]          + 3 * src[j + 1] +
                         3 * src[j + stride] + 4 * src[j + stride + 1] + 6) *
                        2731) >> 15) + 1) >> 1;
        src += stride;
        dst += stride;
    }
}

av_cold void ff_tpeldsp_init(TpelDSPContext *c)
{
    c->put_tpel_pixels_tab[ 0] = put_tpel_pixels_mc00_c;
    c->put_tpel_pixels_tab[ 1] = put_tpel_pixels_mc10_c;
    c->put_tpel_pixels_tab[ 2] = put_tpel_pixels_mc20_c;
    c->put_tpel_pixels_tab[ 4] = put_tpel_pixels_mc01_c;
    c->put_tpel_pixels_tab[ 5] = put_tpel_pixels_mc11_c;
    c->put_tpel_pixels_tab[ 6] = put_tpel_pixels_mc21_c;
    c->put_tpel_pixels_tab[ 8] = put_tpel_pixels_mc02_c;
    c->put_tpel_pixels_tab[ 9] = put_tpel_pixels_mc12_c;
    c->put_tpel_pixels_tab[10] = put_tpel_pixels_mc22_c;

    c->avg_tpel_pixels_tab[ 0] = avg_tpel_pixels_mc00_c;
    c->avg_tpel_pixels_tab[ 1] = avg_tpel_pixels_mc10_c;
    c->avg_tpel_pixels_tab[ 2] = avg_tpel_pixels_mc20_c;
    c->avg_tpel_pixels_tab[ 4] = avg_tpel_pixels_mc01_c;
    c->avg_tpel_pixels_tab[ 5] = avg_tpel_pixels_mc11_c;
    c->avg_tpel_pixels_tab[ 6] = avg_tpel_pixels_mc21_c;
    c->avg_tpel_pixels_tab[ 8] = avg_tpel_pixels_mc02_c;
    c->avg_tpel_pixels_tab[ 9] = avg_tpel_pixels_mc12_c;
    c->avg_tpel_pixels_tab[10] = avg_tpel_pixels_mc22_c;
}
