/*
 * Windows Television (WTV)
 * Copyright (c) 2010-2011 Peter Ross <pross@xvid.org>
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

#ifndef AVFORMAT_WTV_H
#define AVFORMAT_WTV_H

#include "riff.h"
#include "asf.h"

#define WTV_SECTOR_BITS    12
#define WTV_SECTOR_SIZE    (1 << WTV_SECTOR_BITS)
#define WTV_BIGSECTOR_BITS 18

extern const ff_asf_guid ff_dir_entry_guid;
extern const ff_asf_guid ff_wtv_guid;
extern const ff_asf_guid ff_timestamp_guid;
extern const ff_asf_guid ff_data_guid;
extern const ff_asf_guid ff_stream_guid;
extern const ff_asf_guid ff_mediatype_audio;
extern const ff_asf_guid ff_mediatype_video;
extern const ff_asf_guid ff_format_none;
extern const AVCodecGuid ff_video_guids[];
#endif /* AVFORMAT_WTV_H */
