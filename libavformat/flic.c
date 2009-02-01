/*
 * FLI/FLC Animation File Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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
 * @file libavformat/flic.c
 * FLI/FLC file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .fli/.flc file format and all of its many
 * variations, visit:
 *   http://www.compuphase.com/flic.htm
 *
 * This demuxer handles standard 0xAF11- and 0xAF12-type FLIs. It also
 * handles special FLIs from the PC game "Magic Carpet".
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define FLIC_FILE_MAGIC_1 0xAF11
#define FLIC_FILE_MAGIC_2 0xAF12
#define FLIC_FILE_MAGIC_3 0xAF44  /* Flic Type for Extended FLX Format which
                                     originated in Dave's Targa Animator (DTA) */
#define FLIC_CHUNK_MAGIC_1 0xF1FA
#define FLIC_CHUNK_MAGIC_2 0xF5FA
#define FLIC_MC_SPEED 5  /* speed for Magic Carpet game FLIs */
#define FLIC_DEFAULT_SPEED 5  /* for FLIs that have 0 speed */

#define FLIC_HEADER_SIZE 128
#define FLIC_PREAMBLE_SIZE 6

typedef struct FlicDemuxContext {
    int video_stream_index;
    int frame_number;
} FlicDemuxContext;

static int flic_probe(AVProbeData *p)
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


    return AVPROBE_SCORE_MAX;
}

static int flic_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    FlicDemuxContext *flic = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned char header[FLIC_HEADER_SIZE];
    AVStream *st;
    int speed;
    int magic_number;

    flic->frame_number = 0;

    /* load the whole header and pull out the width and height */
    if (get_buffer(pb, header, FLIC_HEADER_SIZE) != FLIC_HEADER_SIZE)
        return AVERROR(EIO);

    magic_number = AV_RL16(&header[4]);
    speed = AV_RL32(&header[0x10]);
    if (speed == 0)
        speed = FLIC_DEFAULT_SPEED;

    /* initialize the decoder streams */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    flic->video_stream_index = st->index;
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_FLIC;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = AV_RL16(&header[0x08]);
    st->codec->height = AV_RL16(&header[0x0A]);

    if (!st->codec->width || !st->codec->height) {
        /* Ugly hack needed for the following sample: */
        /* http://samples.mplayerhq.hu/fli-flc/fli-bugs/specular.flc */
        av_log(s, AV_LOG_WARNING,
               "File with no specified width/height. Trying 640x480.\n");
        st->codec->width  = 640;
        st->codec->height = 480;
    }

    /* send over the whole 128-byte FLIC header */
    st->codec->extradata_size = FLIC_HEADER_SIZE;
    st->codec->extradata = av_malloc(FLIC_HEADER_SIZE);
    memcpy(st->codec->extradata, header, FLIC_HEADER_SIZE);

    /* Time to figure out the framerate: If there is a FLIC chunk magic
     * number at offset 0x10, assume this is from the Bullfrog game,
     * Magic Carpet. */
    if (AV_RL16(&header[0x10]) == FLIC_CHUNK_MAGIC_1) {

        av_set_pts_info(st, 64, FLIC_MC_SPEED, 70);

        /* rewind the stream since the first chunk is at offset 12 */
        url_fseek(pb, 12, SEEK_SET);

        /* send over abbreviated FLIC header chunk */
        av_free(st->codec->extradata);
        st->codec->extradata_size = 12;
        st->codec->extradata = av_malloc(12);
        memcpy(st->codec->extradata, header, 12);

    } else if (magic_number == FLIC_FILE_MAGIC_1) {
        av_set_pts_info(st, 64, speed, 70);
    } else if ((magic_number == FLIC_FILE_MAGIC_2) ||
               (magic_number == FLIC_FILE_MAGIC_3)) {
        av_set_pts_info(st, 64, speed, 1000);
    } else {
        av_log(s, AV_LOG_INFO, "Invalid or unsupported magic chunk in file\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int flic_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FlicDemuxContext *flic = s->priv_data;
    ByteIOContext *pb = s->pb;
    int packet_read = 0;
    unsigned int size;
    int magic;
    int ret = 0;
    unsigned char preamble[FLIC_PREAMBLE_SIZE];

    while (!packet_read) {

        if ((ret = get_buffer(pb, preamble, FLIC_PREAMBLE_SIZE)) !=
            FLIC_PREAMBLE_SIZE) {
            ret = AVERROR(EIO);
            break;
        }

        size = AV_RL32(&preamble[0]);
        magic = AV_RL16(&preamble[4]);

        if (((magic == FLIC_CHUNK_MAGIC_1) || (magic == FLIC_CHUNK_MAGIC_2)) && size > FLIC_PREAMBLE_SIZE) {
            if (av_new_packet(pkt, size)) {
                ret = AVERROR(EIO);
                break;
            }
            pkt->stream_index = flic->video_stream_index;
            pkt->pts = flic->frame_number++;
            pkt->pos = url_ftell(pb);
            memcpy(pkt->data, preamble, FLIC_PREAMBLE_SIZE);
            ret = get_buffer(pb, pkt->data + FLIC_PREAMBLE_SIZE,
                size - FLIC_PREAMBLE_SIZE);
            if (ret != size - FLIC_PREAMBLE_SIZE) {
                av_free_packet(pkt);
                ret = AVERROR(EIO);
            }
            packet_read = 1;
        } else {
            /* not interested in this chunk */
            url_fseek(pb, size - 6, SEEK_CUR);
        }
    }

    return ret;
}

AVInputFormat flic_demuxer = {
    "flic",
    NULL_IF_CONFIG_SMALL("FLI/FLC/FLX animation format"),
    sizeof(FlicDemuxContext),
    flic_probe,
    flic_read_header,
    flic_read_packet,
};
