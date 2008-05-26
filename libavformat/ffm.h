/*
 * FFM (ffserver live feed) common header
 * Copyright (c) 2001 Fabrice Bellard.
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

#ifndef FFMPEG_FFM_H
#define FFMPEG_FFM_H

#include "avformat.h"

/* The FFM file is made of blocks of fixed size */
#define FFM_HEADER_SIZE 14
#define PACKET_ID       0x666d

/* each packet contains frames (which can span several packets */
#define FRAME_HEADER_SIZE    8
#define FLAG_KEY_FRAME       0x01

typedef struct FFMStream {
    int64_t pts;
} FFMStream;

enum {
    READ_HEADER,
    READ_DATA,
};

typedef struct FFMContext {
    /* only reading mode */
    offset_t write_index, file_size;
    int read_state;
    uint8_t header[FRAME_HEADER_SIZE];

    /* read and write */
    int first_packet; /* true if first packet, needed to set the discontinuity tag */
    int first_frame_in_packet; /* true if first frame in packet, needed to know if PTS information is valid */
    int packet_size;
    int frame_offset;
    int64_t pts;
    uint8_t *packet_ptr, *packet_end;
    uint8_t packet[FFM_PACKET_SIZE];
} FFMContext;

#endif /* FFMPEG_FFM_H */
