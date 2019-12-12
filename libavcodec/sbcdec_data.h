/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

/**
 * @file
 * SBC decoder tables
 */

#ifndef AVCODEC_SBCDEC_DATA_H
#define AVCODEC_SBCDEC_DATA_H

#include <stdint.h>

extern const int32_t ff_sbc_proto_4_40m0[];
extern const int32_t ff_sbc_proto_4_40m1[];
extern const int32_t ff_sbc_proto_8_80m0[];
extern const int32_t ff_sbc_proto_8_80m1[];
extern const int32_t ff_synmatrix4[8][4];
extern const int32_t ff_synmatrix8[16][8];

#endif /* AVCODEC_SBCDEC_DATA_H */
