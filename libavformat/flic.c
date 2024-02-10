/*
 * FLI/FLC Animation File Demuxer
 * Copyright (c) 2003 The FFmpeg project
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
 * FLI/FLC file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .fli/.flc file format and all of its many
 * variations, visit:
 *   http://www.compuphase.com/flic.htm
 *
 * This demuxer handles standard 0xAF11- and 0xAF12-type FLIs. It also handles
 * special FLIs from the PC games "Magic Carpet" and "X-COM: Terror from the Deep".
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define FLIC_FILE_MAGIC_1 0xAF11
#define FLIC_FILE_MAGIC_2 0xAF12
#define FLIC_FILE_MAGIC_3 0xAF44  /* Flic Type for Extended FLX Format which
                                     originated in Dave's Targa Animator (DTA) */
#define FLIC_CHUNK_MAGIC_1 0xF1FA
#define FLIC_CHUNK_MAGIC_2 0xF5FA
#define FLIC_MC_SPEED 5  /* speed for Magic Carpet game FLIs */
#define FLIC_DEFAULT_SPEED 5  /* for FLIs that have 0 speed */
#define FLIC_TFTD_CHUNK_AUDIO 0xAAAA /* Audio chunk. Used in Terror from the Deep.
                                        Has 10 B extra header not accounted for in the chunk header */
#define FLIC_TFTD_SAMPLE_RATE 22050

#define FLIC_HEADER_SIZE 128
#define FLIC_PREAMBLE_SIZE 6

typedef struct FlicDemuxContext {
    int video_stream_index;
    int audio_stream_index;
    int frame_number;
} FlicDemuxContext;

static int flic_probe(const AVProbeData *p)
{
    int magic_number;

    if(p->buf_size < FLIC_HEADER_SIZE)
        return 0;

    magic_number = AV_RL16(&p->buf[4]);
    if ((magic_number != FLIC_FILE_MAGIC_1) &&
        (magic_number != FLIC_FILE_MAGIC_2) &&
        (magic_number != FLIC_FILE_MAGIC_3))
        return 0;

    if(AV_RL16(&p->buf[0x10]) != FLIC_CHUNK_MAGIC_1){
        if(AV_RL32(&p->buf[0x10]) > 2000)
            return 0;
    }

    if(   AV_RL16(&p->buf[0x08]) > 4096
       || AV_RL16(&p->buf[0x0A]) > 4096)
        return 0;


    return AVPROBE_SCORE_MAX - 1;
}

static int flic_read_header(AVFormatContext *s)
{
    FlicDemuxContext *flic = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned char header[FLIC_HEADER_SIZE];
    AVStream *st, *ast;
    int speed, ret;
    int magic_number;
    unsigned char preamble[FLIC_PREAMBLE_SIZE];

    flic->frame_number = 0;

    /* load the whole header and pull out the width and height */
    if (avio_read(pb, header, FLIC_HEADER_SIZE) != FLIC_HEADER_SIZE)
        return AVERROR(EIO);

    magic_number = AV_RL16(&header[4]);
    speed = AV_RL32(&header[0x10]);
    if (speed == 0)
        speed = FLIC_DEFAULT_SPEED;

    /* initialize the decoder streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    flic->video_stream_index = st->index;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_FLIC;
    st->codecpar->codec_tag = 0;  /* no fourcc */
    st->codecpar->width = AV_RL16(&header[0x08]);
    st->codecpar->height = AV_RL16(&header[0x0A]);

    if (!st->codecpar->width || !st->codecpar->height) {
        /* Ugly hack needed for the following sample: */
        /* http://samples.mplayerhq.hu/fli-flc/fli-bugs/specular.flc */
        av_log(s, AV_LOG_WARNING,
               "File with no specified width/height. Trying 640x480.\n");
        st->codecpar->width  = 640;
        st->codecpar->height = 480;
    }

    /* send over the whole 128-byte FLIC header */
    if ((ret = ff_alloc_extradata(st->codecpar, FLIC_HEADER_SIZE)) < 0)
        return ret;
    memcpy(st->codecpar->extradata, header, FLIC_HEADER_SIZE);

    /* peek at the preamble to detect TFTD videos - they seem to always start with an audio chunk */
    if (avio_read(pb, preamble, FLIC_PREAMBLE_SIZE) != FLIC_PREAMBLE_SIZE) {
        av_log(s, AV_LOG_ERROR, "Failed to peek at preamble\n");
        return AVERROR(EIO);
    }

    avio_seek(pb, -FLIC_PREAMBLE_SIZE, SEEK_CUR);

    /* Time to figure out the framerate:
     * If the first preamble's magic number is 0xAAAA then this file is from
     * X-COM: Terror from the Deep. If on the other hand there is a FLIC chunk
     * magic number at offset 0x10 assume this file is from Magic Carpet instead.
     * If neither of the above is true then this is a normal FLIC file.
     */
    if (AV_RL16(&preamble[4]) == FLIC_TFTD_CHUNK_AUDIO) {
        /* TFTD videos have an extra 22050 Hz 8-bit mono audio stream */
        ast = avformat_new_stream(s, NULL);
        if (!ast)
            return AVERROR(ENOMEM);

        flic->audio_stream_index = ast->index;

        /* all audio frames are the same size, so use the size of the first chunk for block_align */
        ast->codecpar->block_align = AV_RL32(&preamble[0]);
        ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
        ast->codecpar->codec_tag = 0;
        ast->codecpar->sample_rate = FLIC_TFTD_SAMPLE_RATE;
        ast->codecpar->bit_rate = st->codecpar->sample_rate * 8;
        ast->codecpar->bits_per_coded_sample = 8;
        ast->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        ast->codecpar->extradata_size = 0;

        /* Since the header information is incorrect we have to figure out the
         * framerate using block_align and the fact that the audio is 22050 Hz.
         * We usually have two cases: 2205 -> 10 fps and 1470 -> 15 fps */
        avpriv_set_pts_info(st, 64, ast->codecpar->block_align, FLIC_TFTD_SAMPLE_RATE);
        avpriv_set_pts_info(ast, 64, 1, FLIC_TFTD_SAMPLE_RATE);
    } else if (AV_RL16(&header[0x10]) == FLIC_CHUNK_MAGIC_1) {
        avpriv_set_pts_info(st, 64, FLIC_MC_SPEED, 70);

        /* rewind the stream since the first chunk is at offset 12 */
        avio_seek(pb, 12, SEEK_SET);

        /* send over abbreviated FLIC header chunk */
        if ((ret = ff_alloc_extradata(st->codecpar, 12)) < 0)
            return ret;
        memcpy(st->codecpar->extradata, header, 12);

    } else if (magic_number == FLIC_FILE_MAGIC_1) {
        avpriv_set_pts_info(st, 64, speed, 70);
    } else if ((magic_number == FLIC_FILE_MAGIC_2) ||
               (magic_number == FLIC_FILE_MAGIC_3)) {
        avpriv_set_pts_info(st, 64, speed, 1000);
    } else {
        av_log(s, AV_LOG_ERROR, "Invalid or unsupported magic chunk in file\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int flic_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FlicDemuxContext *flic = s->priv_data;
    AVIOContext *pb = s->pb;
    int packet_read = 0;
    unsigned int size;
    int magic;
    int ret = 0;
    unsigned char preamble[FLIC_PREAMBLE_SIZE];
    int64_t pos = avio_tell(pb);

    while (!packet_read && !avio_feof(pb)) {

        if ((ret = avio_read(pb, preamble, FLIC_PREAMBLE_SIZE)) !=
            FLIC_PREAMBLE_SIZE) {
            ret = AVERROR(EIO);
            break;
        }

        size = AV_RL32(&preamble[0]);
        magic = AV_RL16(&preamble[4]);

        if (((magic == FLIC_CHUNK_MAGIC_1) || (magic == FLIC_CHUNK_MAGIC_2)) && size > FLIC_PREAMBLE_SIZE) {
            if ((ret = av_new_packet(pkt, size)) < 0)
                return ret;

            pkt->stream_index = flic->video_stream_index;
            pkt->pos = pos;
            memcpy(pkt->data, preamble, FLIC_PREAMBLE_SIZE);
            ret = avio_read(pb, pkt->data + FLIC_PREAMBLE_SIZE,
                size - FLIC_PREAMBLE_SIZE);
            if (ret != size - FLIC_PREAMBLE_SIZE) {
                ret = AVERROR(EIO);
            }
            pkt->flags = flic->frame_number == 0 ? AV_PKT_FLAG_KEY : 0;
            pkt->pts = flic->frame_number;
            if (flic->frame_number == 0)
                av_add_index_entry(s->streams[flic->video_stream_index], pkt->pos, pkt->pts, pkt->size, 0, AVINDEX_KEYFRAME);
            packet_read = 1;
            flic->frame_number++;
        } else if (magic == FLIC_TFTD_CHUNK_AUDIO) {
            if ((ret = av_new_packet(pkt, size)) < 0)
                return ret;

            /* skip useless 10B sub-header (yes, it's not accounted for in the chunk header) */
            avio_skip(pb, 10);

            pkt->stream_index = flic->audio_stream_index;
            pkt->pos = pos;
            pkt->flags = AV_PKT_FLAG_KEY;
            ret = avio_read(pb, pkt->data, size);

            if (ret != size) {
                ret = AVERROR(EIO);
                break;
            }

            packet_read = 1;
        } else {
            /* not interested in this chunk */
            avio_skip(pb, size - 6);
        }
    }

    return avio_feof(pb) ? AVERROR_EOF : ret;
}

static int flic_read_seek(AVFormatContext *s, int stream_index,
                          int64_t pts, int flags)
{
    FlicDemuxContext *flic = s->priv_data;
    AVStream *st = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    int64_t pos, ts;
    int index;

    if (!sti->index_entries || stream_index != flic->video_stream_index)
        return -1;

    index = av_index_search_timestamp(st, pts, flags);

    if (index < 0)
        index = av_index_search_timestamp(st, pts, flags ^ AVSEEK_FLAG_BACKWARD);
    if (index < 0)
        return -1;

    pos = sti->index_entries[index].pos;
    ts  = sti->index_entries[index].timestamp;
    flic->frame_number = ts;
    avio_seek(s->pb, pos, SEEK_SET);
    return 0;
}

const FFInputFormat ff_flic_demuxer = {
    .p.name         = "flic",
    .p.long_name    = NULL_IF_CONFIG_SMALL("FLI/FLC/FLX animation"),
    .priv_data_size = sizeof(FlicDemuxContext),
    .read_probe     = flic_probe,
    .read_header    = flic_read_header,
    .read_packet    = flic_read_packet,
    .read_seek      = flic_read_seek,
};
