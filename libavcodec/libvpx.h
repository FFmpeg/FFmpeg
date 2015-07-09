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

#ifndef AVCODEC_LIBVPX_H
#define AVCODEC_LIBVPX_H

#include <vpx/vpx_codec.h>

#include "avcodec.h"

enum AVPixelFormat ff_vpx_imgfmt_to_pixfmt(vpx_img_fmt_t img);
vpx_img_fmt_t ff_vpx_pixfmt_to_imgfmt(enum AVPixelFormat pix);

#endif /* AVCODEC_LIBVPX_H */
