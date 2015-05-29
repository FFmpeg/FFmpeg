/*
 * Copyright (c) 2015 Kevin Wheatley <kevin.j.wheatley@gmail.com>
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

#ifndef AVUTIL_COLOR_UTILS_H
#define AVUTIL_COLOR_UTILS_H


#include "libavutil/pixfmt.h"

/**
 * Determine a suitable 'gamma' value to match the supplied
 * AVColorTransferCharacteristic.
 *
 * See Apple Technical Note TN2257 (https://developer.apple.com/library/mac/technotes/tn2257/_index.html)
 *
 * @return Will return an approximation to the simple gamma function matching
 *         the supplied Transfer Characteristic, Will return 0.0 for any
 *         we cannot reasonably match against.
 */
double avpriv_get_gamma_from_trc(enum AVColorTransferCharacteristic trc);

#endif
