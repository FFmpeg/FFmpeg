/*
 * EVC helper functions for muxers
 * Copyright (c) 2022 Dawid Kozinski <d.kozinski@samsung.com>
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

#ifndef AVFORMAT_EVC_H
#define AVFORMAT_EVC_H

#include <stdint.h>
#include "libavutil/rational.h"
#include "avio.h"



/**
 * Writes EVC sample metadata to the provided AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the evc sample metadata shall be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 * @param ps_array_completeness @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
 *
 * @return 0 in case of success, a negative error code in case of failure
 */
int ff_isom_write_evcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness);

#endif // AVFORMAT_EVC_H
