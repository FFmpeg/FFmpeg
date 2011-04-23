/*
 * AAC Spectral Band Replication function declarations
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2010      Alex Converse <alex.converse@gmail.com>
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
 * AAC Spectral Band Replication function declarations
 * @author Robert Swain ( rob opendot cl )
 */

#ifndef AVCODEC_AACSBR_H
#define AVCODEC_AACSBR_H

#include "get_bits.h"
#include "aac.h"
#include "sbr.h"

/** Initialize SBR. */
av_cold void ff_aac_sbr_init(void);
/** Initialize one SBR context. */
av_cold void ff_aac_sbr_ctx_init(AACContext *ac, SpectralBandReplication *sbr);
/** Close one SBR context. */
av_cold void ff_aac_sbr_ctx_close(SpectralBandReplication *sbr);
/** Decode one SBR element. */
int ff_decode_sbr_extension(AACContext *ac, SpectralBandReplication *sbr,
                            GetBitContext *gb, int crc, int cnt, int id_aac);
/** Apply one SBR element to one AAC element. */
void ff_sbr_apply(AACContext *ac, SpectralBandReplication *sbr, int id_aac,
                  float* L, float *R);

#endif /* AVCODEC_AACSBR_H */
