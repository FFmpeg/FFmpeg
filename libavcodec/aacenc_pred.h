/*
 * AAC encoder main-type prediction
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
 * AAC encoder main-type prediction
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#ifndef AVCODEC_AACENC_PRED_H
#define AVCODEC_AACENC_PRED_H

#include "aacenc.h"

/* Every predictor group needs to get reset at least once in this many frames */
#define PRED_RESET_FRAME_MIN 240

/* Any frame with less than this amount of frames since last reset is ok */
#define PRED_RESET_MIN 64

/* Raise to filter any low frequency artifacts due to prediction */
#define PRED_SFB_START 10

void ff_aac_apply_main_pred(AACEncContext *s, SingleChannelElement *sce);
void ff_aac_adjust_common_pred(AACEncContext *s, ChannelElement *cpe);
void ff_aac_search_for_pred(AACEncContext *s, SingleChannelElement *sce);
void ff_aac_encode_main_pred(AACEncContext *s, SingleChannelElement *sce);

#endif /* AVCODEC_AACENC_PRED_H */
