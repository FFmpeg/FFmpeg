/*
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by jinbo <jinbo@loongson.cn>
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

#ifndef AVCODEC_LOONGARCH_HEVCDSP_LASX_H
#define AVCODEC_LOONGARCH_HEVCDSP_LASX_H

#include "libavcodec/hevcdsp.h"

#define PEL_UNI_W(PEL, DIR, WIDTH)                                       \
void ff_hevc_put_hevc_##PEL##_uni_w_##DIR##WIDTH##_8_lasx(uint8_t *dst,  \
                                                          ptrdiff_t      \
                                                          dst_stride,    \
                                                          const uint8_t *src,  \
                                                          ptrdiff_t      \
                                                          src_stride,    \
                                                          int height,    \
                                                          int denom,     \
                                                          int wx,        \
                                                          int ox,        \
                                                          intptr_t mx,   \
                                                          intptr_t my,   \
                                                          int width)

PEL_UNI_W(pel, pixels, 6);
PEL_UNI_W(pel, pixels, 8);
PEL_UNI_W(pel, pixels, 12);
PEL_UNI_W(pel, pixels, 16);
PEL_UNI_W(pel, pixels, 24);
PEL_UNI_W(pel, pixels, 32);
PEL_UNI_W(pel, pixels, 48);
PEL_UNI_W(pel, pixels, 64);

PEL_UNI_W(qpel, v, 8);
PEL_UNI_W(qpel, v, 12);
PEL_UNI_W(qpel, v, 16);
PEL_UNI_W(qpel, v, 24);
PEL_UNI_W(qpel, v, 32);
PEL_UNI_W(qpel, v, 48);
PEL_UNI_W(qpel, v, 64);

PEL_UNI_W(qpel, h, 4);
PEL_UNI_W(qpel, h, 6);
PEL_UNI_W(qpel, h, 8);
PEL_UNI_W(qpel, h, 12);
PEL_UNI_W(qpel, h, 16);
PEL_UNI_W(qpel, h, 24);
PEL_UNI_W(qpel, h, 32);
PEL_UNI_W(qpel, h, 48);
PEL_UNI_W(qpel, h, 64);

#undef PEL_UNI_W

#endif  // #ifndef AVCODEC_LOONGARCH_HEVCDSP_LASX_H
