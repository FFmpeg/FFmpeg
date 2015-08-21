/*
 * AAC encoder TNS
 * Copyright (C) 2015 Rostislav Pehlivanov
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
 * AAC encoder temporal noise shaping
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#ifndef AVCODEC_AACENC_TNS_H
#define AVCODEC_AACENC_TNS_H

#include "aac.h"

/** Frequency in Hz for lower limit of TNS **/
#define TNS_LOW_LIMIT 2150

/** LPC settings */
#define TNS_MIN_PRED_ORDER 0
#define MAX_LPC_PRECISION  4   /* 4 bits ltp coeff precision */
#define TNS_LPC_PASSES     2
#define MAX_LPC_SHIFT      4

void encode_tns_info(AACEncContext *s, SingleChannelElement *sce);
void search_for_tns(AACEncContext *s, SingleChannelElement *sce);

#endif /* AVCODEC_AACENC_TNS_H */
