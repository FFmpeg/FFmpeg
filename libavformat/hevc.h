/*
 * Copyright (c) 2014 Tim Walker <tdskywalker@gmail.com>
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

/**
 * @file
 * internal header for HEVC (de)muxer utilities
 */

#ifndef AVFORMAT_HEVC_H
#define AVFORMAT_HEVC_H

#include <stdint.h>
#include "avio.h"

/**
 * Writes HEVC extradata (parameter sets, declarative SEI NAL units) to the
 * provided AVIOContext.
 *
 * If the extradata is Annex B format, it gets converted to hvcC format before
 * writing.
 *
 * @param pb address of the AVIOContext where the hvcC shall be written
 * @param data address of the buffer holding the data needed to write the hvcC
 * @param size size (in bytes) of the data buffer
 * @param ps_array_completeness whether all parameter sets are in the hvcC (1)
 *        or there may be additional parameter sets in the bitstream (0)
 * @return 0 in case of success, a negative value corresponding to an AVERROR
 *         code in case of failure
 */
int ff_isom_write_hvcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness);

#endif /* AVFORMAT_HEVC_H */
