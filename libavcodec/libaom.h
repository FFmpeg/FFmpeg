/*
 * Copyright (c) 2013 Guillaume Martres <smarter@ubuntu.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_LIBAOM_H
#define AVCODEC_LIBAOM_H

#include <aom/aom_codec.h>

#include "libavutil/pixfmt.h"

enum AVPixelFormat ff_aom_imgfmt_to_pixfmt(aom_img_fmt_t img, int depth);
aom_img_fmt_t ff_aom_pixfmt_to_imgfmt(enum AVPixelFormat pix);

#endif /* AVCODEC_LIBAOM_H */
