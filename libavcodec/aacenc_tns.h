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

#include "aacenc.h"

/* Could be set to 3 to save an additional bit at the cost of little quality */
#define TNS_Q_BITS 4

/* TNS will only be used if the LPC gain is within these margins */
#define TNS_GAIN_THRESHOLD_LOW  1.395f
#define TNS_GAIN_THRESHOLD_HIGH 11.19f

/* If the energy ratio between the low SFBs vs the high SFBs is not between
 * those two values, use 2 filters instead */
#define TNS_E_RATIO_LOW  0.77
#define TNS_E_RATIO_HIGH 1.23

/* Do not use TNS if the psy band spread is below this value */
#define TNS_SPREAD_THRESHOLD 37.081512f

void ff_aac_encode_tns_info(AACEncContext *s, SingleChannelElement *sce);
void ff_aac_apply_tns(AACEncContext *s, SingleChannelElement *sce);
void ff_aac_search_for_tns(AACEncContext *s, SingleChannelElement *sce);

#endif /* AVCODEC_AACENC_TNS_H */
