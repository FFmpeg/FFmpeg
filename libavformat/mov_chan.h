/*
 * Copyright (c) 2011 Justin Ruggles
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
 * mov 'chan' tag reading/writing.
 * @author Justin Ruggles
 */

#ifndef AVFORMAT_MOV_CHAN_H
#define AVFORMAT_MOV_CHAN_H

#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "avformat.h"

/**
 * Get the channel layout for the specified channel layout tag.
 *
 * @param[in]  tag     channel layout tag
 * @param[out] bitmap  channel bitmap (only used if needed)
 * @return             channel layout
 */
uint64_t ff_mov_get_channel_layout(uint32_t tag, uint32_t bitmap);

/**
 * Get the channel layout tag for the specified codec id and channel layout.
 * If the layout tag was not found, use a channel bitmap if possible.
 *
 * @param[in]  codec_id        codec id
 * @param[in]  channel_layout  channel layout
 * @param[out] bitmap          channel bitmap
 * @return                     channel layout tag
 */
uint32_t ff_mov_get_channel_layout_tag(enum AVCodecID codec_id,
                                       uint64_t channel_layout,
                                       uint32_t *bitmap);

/**
 * Read 'chan' tag from the input stream.
 *
 * @param s     AVFormatContext
 * @param pb    AVIOContext
 * @param st    The stream to set codec values for
 * @param size  Remaining size in the 'chan' tag
 * @return      0 if ok, or negative AVERROR code on failure
 */
int ff_mov_read_chan(AVFormatContext *s, AVIOContext *pb, AVStream *st,
                     int64_t size);

#endif /* AVFORMAT_MOV_CHAN_H */
