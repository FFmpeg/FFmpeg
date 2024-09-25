/*
 * RISC-V optimised DSP functions
 * Copyright (c) 2024 Niklas Haas
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/h264qpel.h"

#define DECL_QPEL_OPS(OP, SIZE, EXT)                                                                       \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc00_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc10_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc20_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc30_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc01_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc11_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc21_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc31_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc02_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc12_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc22_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc32_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc03_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc13_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc23_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride); \
void ff_ ## OP ## _h264_qpel ## SIZE ## _mc33_ ## EXT(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

DECL_QPEL_OPS(put, 16, rvv256)
DECL_QPEL_OPS(put, 8,  rvv256)
// DECL_QPEL_OPS(put, 4,  rvv256)

DECL_QPEL_OPS(avg, 16, rvv256)
DECL_QPEL_OPS(avg, 8,  rvv256)
// DECL_QPEL_OPS(avg, 4,  rvv256)

DECL_QPEL_OPS(put, 16, rvv)
DECL_QPEL_OPS(put, 8,  rvv)
DECL_QPEL_OPS(put, 4,  rvv)

DECL_QPEL_OPS(avg, 16, rvv)
DECL_QPEL_OPS(avg, 8,  rvv)
DECL_QPEL_OPS(avg, 4,  rvv)

#define SET_QPEL_FNS(OP, IDX, SIZE, EXT)                                                        \
do {                                                                                            \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 0] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc00_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 1] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc10_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 2] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc20_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 3] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc30_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 4] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc01_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 5] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc11_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 6] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc21_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 7] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc31_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 8] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc02_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][ 9] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc12_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][10] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc22_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][11] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc32_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][12] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc03_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][13] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc13_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][14] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc23_ ## EXT; \
    c->OP ## _h264_qpel_pixels_tab[IDX][15] = ff_ ## OP ## _h264_qpel ## SIZE ## _mc33_ ## EXT; \
} while (0)

av_cold void ff_h264qpel_init_riscv(H264QpelContext *c, int bit_depth)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();
    if (flags & AV_CPU_FLAG_RVV_I32) {
        const int vlen = 8 * ff_get_rv_vlenb();

        switch (bit_depth) {
        case 8:
            if (vlen >= 256) {
                SET_QPEL_FNS(put, 0, 16, rvv256);
                SET_QPEL_FNS(put, 1, 8,  rvv256);
                SET_QPEL_FNS(put, 2, 4,  rvv);

                SET_QPEL_FNS(avg, 0, 16, rvv256);
                SET_QPEL_FNS(avg, 1, 8,  rvv256);
                SET_QPEL_FNS(avg, 2, 4,  rvv);
            } else if (vlen >= 128) {
                SET_QPEL_FNS(put, 0, 16, rvv);
                SET_QPEL_FNS(put, 1, 8,  rvv);
                SET_QPEL_FNS(put, 2, 4,  rvv);

                SET_QPEL_FNS(avg, 0, 16, rvv);
                SET_QPEL_FNS(avg, 1, 8,  rvv);
                SET_QPEL_FNS(avg, 2, 4,  rvv);
            }
            break;
        }
    }
#endif
}
