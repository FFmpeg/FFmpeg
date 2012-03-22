/*
 * AVC helper functions for muxers
 * Copyright (c) 2008 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVFORMAT_AVC_H
#define AVFORMAT_AVC_H

#include <stdint.h>
#include "avio.h"

int ff_avc_parse_nal_units(AVIOContext *s, const uint8_t *buf, int size);
int ff_avc_parse_nal_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size);
int ff_isom_write_avcc(AVIOContext *pb, const uint8_t *data, int len);
const uint8_t *ff_avc_find_startcode(const uint8_t *p, const uint8_t *end);
int ff_avc_write_annexb_extradata(const uint8_t *in, uint8_t **buf, int *size);

#endif /* AVFORMAT_AVC_H */
