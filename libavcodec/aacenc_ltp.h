/*
 * AAC encoder long term prediction extension
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
 * AAC encoder long term prediction extension
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#ifndef AVCODEC_AACENC_LTP_H
#define AVCODEC_AACENC_LTP_H

#include "aacenc.h"

void ff_aac_encode_ltp_info(AACEncContext *s, SingleChannelElement *sce,
                            int common_window);
void ff_aac_update_ltp(AACEncContext *s, SingleChannelElement *sce);
void ff_aac_adjust_common_ltp(AACEncContext *s, ChannelElement *cpe);
void ff_aac_ltp_insert_new_frame(AACEncContext *s);
void ff_aac_search_for_ltp(AACEncContext *s, SingleChannelElement *sce,
                           int common_window);

#endif /* AVCODEC_AACENC_LTP_H */
