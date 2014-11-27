/*
 * MOV, 3GP, MP4 muxer
 * Copyright (c) 2003 Thomas Raivio
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

#ifndef AVFORMAT_MOVENC_H
#define AVFORMAT_MOVENC_H

#include "avformat.h"

#define MOV_FRAG_INFO_ALLOC_INCREMENT 64
#define MOV_INDEX_CLUSTER_SIZE 1024
#define MOV_TIMESCALE 1000

#define RTP_MAX_PACKET_SIZE 1450

#define MODE_MP4  0x01
#define MODE_MOV  0x02
#define MODE_3GP  0x04
#define MODE_PSP  0x08 // example working PSP command line:
// ffmpeg -i testinput.avi  -f psp -r 14.985 -s 320x240 -b 768 -ar 24000 -ab 32 M4V00001.MP4
#define MODE_3G2  0x10
#define MODE_IPOD 0x20
#define MODE_ISM  0x40
#define MODE_F4V  0x80

typedef struct MOVIentry {
    uint64_t     pos;
    int64_t      dts;
    unsigned int size;
    unsigned int samples_in_chunk;
    unsigned int chunkNum;              ///< Chunk number if the current entry is a chunk start otherwise 0
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

typedef struct HintSampleQueue {
    int size;
    int len;
    HintSample *samples;
} HintSampleQueue;

typedef struct MOVFragmentInfo {
    int64_t offset;
    int64_t time;
    int64_t duration;
    int64_t tfrf_offset;
    int size;
} MOVFragmentInfo;

typedef struct MOVTrack {
    int         mode;
    int         entry;
    unsigned    timescale;
    uint64_t    time;
    int64_t     track_duration;
    int         last_sample_is_subtitle_end;
    long        sample_count;
    long        sample_size;
    long        chunkCount;
    int         has_keyframes;
#define MOV_TRACK_CTTS         0x0001
#define MOV_TRACK_STPS         0x0002
#define MOV_TRACK_ENABLED      0x0004
    uint32_t    flags;
#define MOV_TIMECODE_FLAG_DROPFRAME     0x0001
#define MOV_TIMECODE_FLAG_24HOURSMAX    0x0002
#define MOV_TIMECODE_FLAG_ALLOWNEGATIVE 0x0004
    uint32_t    timecode_flags;
    int         language;
    int         track_id;
    int         tag; ///< stsd fourcc
    AVStream        *st;
    AVCodecContext *enc;
    int multichannel_as_mono;

    int         vos_len;
    uint8_t     *vos_data;
    MOVIentry   *cluster;
    unsigned    cluster_capacity;
    int         audio_vbr;
    int         height; ///< active picture (w/o VBI) height for D-10/IMX
    uint32_t    tref_tag;
    int         tref_id; ///< trackID of the referenced track
    int64_t     start_dts;

    int         hint_track;   ///< the track that hints this track, -1 if no hint track is set
    int         src_track;    ///< the track that this hint (or tmcd) track describes
    AVFormatContext *rtp_ctx; ///< the format context for the hinting rtp muxer
    uint32_t    prev_rtp_ts;
    int64_t     cur_rtp_ts_unwrapped;
    uint32_t    max_packet_size;

    int64_t     default_duration;
    uint32_t    default_sample_flags;
    uint32_t    default_size;

    HintSampleQueue sample_queue;

    AVIOContext *mdat_buf;
    int64_t     data_offset;
    int64_t     frag_start;
    int         frag_discont;

    int         nb_frag_info;
    MOVFragmentInfo *frag_info;
    unsigned    frag_info_capacity;

    struct {
        int64_t struct_offset;
        int     first_packet_seq;
        int     first_packet_entry;
        int     packet_seq;
        int     packet_entry;
        int     slices;
    } vc1_info;

    void       *eac3_priv;
} MOVTrack;

typedef struct MOVMuxContext {
    const AVClass *av_class;
    int     mode;
    int64_t time;
    int     nb_streams;
    int     nb_meta_tmcd;  ///< number of new created tmcd track based on metadata (aka not data copy)
    int     chapter_track; ///< qt chapter track number
    int64_t mdat_pos;
    uint64_t mdat_size;
    MOVTrack *tracks;

    int flags;
    int rtp_flags;
    int exact;

    int iods_skip;
    int iods_video_profile;
    int iods_audio_profile;

    int fragments;
    int max_fragment_duration;
    int min_fragment_duration;
    int max_fragment_size;
    int ism_lookahead;
    AVIOContext *mdat_buf;
    int first_trun;

    int video_track_timescale;

    int reserved_moov_size; ///< 0 for disabled, -1 for automatic, size otherwise
    int64_t reserved_moov_pos;

    char *major_brand;

    int per_stream_grouping;
    AVFormatContext *fc;

    int use_editlist;
} MOVMuxContext;

#define FF_MOV_FLAG_RTP_HINT              (1 <<  0)
#define FF_MOV_FLAG_FRAGMENT              (1 <<  1)
#define FF_MOV_FLAG_EMPTY_MOOV            (1 <<  2)
#define FF_MOV_FLAG_FRAG_KEYFRAME         (1 <<  3)
#define FF_MOV_FLAG_SEPARATE_MOOF         (1 <<  4)
#define FF_MOV_FLAG_FRAG_CUSTOM           (1 <<  5)
#define FF_MOV_FLAG_ISML                  (1 <<  6)
#define FF_MOV_FLAG_FASTSTART             (1 <<  7)
#define FF_MOV_FLAG_OMIT_TFHD_OFFSET      (1 <<  8)
#define FF_MOV_FLAG_DISABLE_CHPL          (1 <<  9)
#define FF_MOV_FLAG_DEFAULT_BASE_MOOF     (1 << 10)
#define FF_MOV_FLAG_DASH                  (1 << 11)
#define FF_MOV_FLAG_FRAG_DISCONT          (1 << 12)

int ff_mov_write_packet(AVFormatContext *s, AVPacket *pkt);

int ff_mov_init_hinting(AVFormatContext *s, int index, int src_index);
int ff_mov_add_hinted_packet(AVFormatContext *s, AVPacket *pkt,
                             int track_index, int sample,
                             uint8_t *sample_data, int sample_size);
void ff_mov_close_hinting(MOVTrack *track);

#endif /* AVFORMAT_MOVENC_H */
