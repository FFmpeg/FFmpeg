/*
 * MOV, 3GP, MP4 muxer
 * Copyright (c) 2003 Thomas Raivio
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

#ifndef AVFORMAT_MOVENC_H
#define AVFORMAT_MOVENC_H

#include "avformat.h"

#define MOV_INDEX_CLUSTER_SIZE 16384
#define MOV_TIMESCALE 1000

#define RTP_MAX_PACKET_SIZE 1450

#define MODE_MP4  0x01
#define MODE_MOV  0x02
#define MODE_3GP  0x04
#define MODE_PSP  0x08 // example working PSP command line:
// avconv -i testinput.avi  -f psp -r 14.985 -s 320x240 -b 768 -ar 24000 -ab 32 M4V00001.MP4
#define MODE_3G2  0x10
#define MODE_IPOD 0x20
#define MODE_ISM  0x40

typedef struct MOVIentry {
    uint64_t     pos;
    int64_t      dts;
    unsigned int size;
    unsigned int samplesInChunk;
    unsigned int entries;
    int          cts;
#define MOV_SYNC_SAMPLE         0x0001
#define MOV_PARTIAL_SYNC_SAMPLE 0x0002
    uint32_t     flags;
} MOVIentry;

typedef struct HintSample {
    uint8_t *data;
    int size;
    int sample_number;
    int offset;
    int own_data;
} HintSample;

typedef struct {
    int size;
    int len;
    HintSample *samples;
} HintSampleQueue;

typedef struct {
    int64_t offset;
    int64_t time;
    int64_t duration;
    int64_t tfrf_offset;
} MOVFragmentInfo;

typedef struct MOVIndex {
    int         mode;
    int         entry;
    unsigned    timescale;
    uint64_t    time;
    int64_t     trackDuration;
    long        sampleCount;
    long        sampleSize;
    int         hasKeyframes;
#define MOV_TRACK_CTTS         0x0001
#define MOV_TRACK_STPS         0x0002
    uint32_t    flags;
    int         language;
    int         trackID;
    int         tag; ///< stsd fourcc
    AVCodecContext *enc;

    int         vosLen;
    uint8_t     *vosData;
    MOVIentry   *cluster;
    int         audio_vbr;
    int         height; ///< active picture (w/o VBI) height for D-10/IMX
    uint32_t    tref_tag;
    int         tref_id; ///< trackID of the referenced track
    int64_t     start_dts;

    int         hint_track;   ///< the track that hints this track, -1 if no hint track is set
    int         src_track;    ///< the track that this hint track describes
    AVFormatContext *rtp_ctx; ///< the format context for the hinting rtp muxer
    uint32_t    prev_rtp_ts;
    int64_t     cur_rtp_ts_unwrapped;
    uint32_t    max_packet_size;

    int64_t     default_duration;
    uint32_t    default_sample_flags;
    uint32_t    default_size;

    HintSampleQueue sample_queue;

    AVIOContext *mdat_buf;
    int64_t     moof_size_offset;
    int64_t     data_offset;
    int64_t     frag_start;
    int64_t     tfrf_offset;

    int         nb_frag_info;
    MOVFragmentInfo *frag_info;
} MOVTrack;

typedef struct MOVMuxContext {
    const AVClass *av_class;
    int     mode;
    int64_t time;
    int     nb_streams;
    int     chapter_track; ///< qt chapter track number
    int64_t mdat_pos;
    uint64_t mdat_size;
    MOVTrack *tracks;

    int flags;
    int rtp_flags;
    int iods_skip;
    int iods_video_profile;
    int iods_audio_profile;

    int fragments;
    int max_fragment_duration;
    int max_fragment_size;
    int ism_lookahead;
} MOVMuxContext;

#define FF_MOV_FLAG_RTP_HINT 1
#define FF_MOV_FLAG_FRAGMENT 2
#define FF_MOV_FLAG_EMPTY_MOOV 4
#define FF_MOV_FLAG_FRAG_KEYFRAME 8
#define FF_MOV_FLAG_SEPARATE_MOOF 16
#define FF_MOV_FLAG_FRAG_CUSTOM 32
#define FF_MOV_FLAG_ISML 64

int ff_mov_write_packet(AVFormatContext *s, AVPacket *pkt);

int ff_mov_init_hinting(AVFormatContext *s, int index, int src_index);
int ff_mov_add_hinted_packet(AVFormatContext *s, AVPacket *pkt,
                             int track_index, int sample,
                             uint8_t *sample_data, int sample_size);
void ff_mov_close_hinting(MOVTrack *track);

#endif /* AVFORMAT_MOVENC_H */
