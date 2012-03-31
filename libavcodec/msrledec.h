/*
 * Microsoft RLE decoder
 * Copyright (C) 2008 Konstantin Shishkov
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

#ifndef AVCODEC_MSRLEDEC_H
#define AVCODEC_MSRLEDEC_H

#include "avcodec.h"
#include "bytestream.h"

/**
 * Decode stream in MS RLE format into frame.
 *
 * @param avctx     codec context
 * @param pic       destination frame
 * @param depth     bit depth
 * @param gb        input bytestream context
 */
int ff_msrle_decode(AVCodecContext *avctx, AVPicture *pic,
                    int depth, GetByteContext *gb);

#endif /* AVCODEC_MSRLEDEC_H */
