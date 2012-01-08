/*
 * VDA HW acceleration
 *
 * copyright (c) 2011 Sebastien Zwickert
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

#ifndef AVCODEC_VDA_INTERNAL_H
#define AVCODEC_VDA_INTERNAL_H

#include "vda.h"

/**
 * \addtogroup VDA_Decoding
 *
 * @{
 */

/** Send frame data to the hardware decoder. */
int ff_vda_decoder_decode(struct vda_context *vda_ctx,
                          uint8_t *bitstream,
                          int bitstream_size,
                          int64_t frame_pts);

/* @} */

#endif /* AVCODEC_VDA_INTERNAL_H */
