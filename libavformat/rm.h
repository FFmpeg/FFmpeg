/*
 * "Real" compatible muxer and demuxer.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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

#ifndef FFMPEG_RM_H
#define FFMPEG_RM_H

#include "avformat.h"


typedef struct {
    int nb_packets;
    int packet_total_size;
    int packet_max_size;
    /* codec related output */
    int bit_rate;
    float frame_rate;
    int nb_frames;    /* current frame number */
    int total_frames; /* total number of frames */
    int num;
    AVCodecContext *enc;
} StreamInfo;

typedef struct {
    StreamInfo streams[2];
    StreamInfo *audio_stream, *video_stream;
    int data_pos; /* position of the data after the header */
    int nb_packets;
    int old_format;
    int current_stream;
    int remaining_len;
    uint8_t *videobuf; ///< place to store merged video frame
    int videobufsize;  ///< current assembled frame size
    int videobufpos;   ///< position for the next slice in the video buffer
    int curpic_num;    ///< picture number of current frame
    int cur_slice, slices;
    int64_t pktpos;    ///< first slice position in file
    /// Audio descrambling matrix parameters
    uint8_t *audiobuf; ///< place to store reordered audio data
    int64_t audiotimestamp; ///< Audio packet timestamp
    int sub_packet_cnt; // Subpacket counter, used while reading
    int sub_packet_size, sub_packet_h, coded_framesize; ///< Descrambling parameters from container
    int audio_stream_num; ///< Stream number for audio packets
    int audio_pkt_cnt; ///< Output packet counter
    int audio_framesize; /// Audio frame size from container
    int sub_packet_lengths[16]; /// Length of each aac subpacket
} RMContext;

#endif /* FFMPEG_RM_H */
