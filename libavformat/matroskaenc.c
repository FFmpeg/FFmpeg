/*
 * Matroska file muxer
 * Copyright (c) 2007 David Conrad
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

#include "avformat.h"
#include "riff.h"
#include "matroska.h"

typedef struct MatroskaMuxContext {
    offset_t    segment;
} MatroskaMuxContext;

static void put_ebml_id(ByteIOContext *pb, unsigned int id)
{
    if (id >= 0x3fffff)
        put_byte(pb, id >> 24);
    if (id >= 0x7fff)
        put_byte(pb, id >> 16);
    if (id >= 0xff)
        put_byte(pb, id >> 8);
    put_byte(pb, id);
}

// XXX: test this thoroughly and get rid of minbytes hack (currently needed to
// use up all of the space reserved in start_ebml_master)
static void put_ebml_size(ByteIOContext *pb, uint64_t size, int minbytes)
{
    int bytes = minbytes;
    while (size >> (bytes*7 + 7)) bytes++;

    // sizes larger than this are currently undefined in EBML
    // XXX: error condition?
    if (size > (1ULL<<56)-1) return;

    put_byte(pb, (0x80 >> bytes) | (size >> bytes*8));
    for (bytes -= 1; bytes >= 0; bytes--)
        put_byte(pb, size >> bytes*8);
}

static void put_ebml_uint(ByteIOContext *pb, unsigned int elementid, uint64_t val)
{
    int bytes = 1;
    while (val >> bytes*8) bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_size(pb, bytes, 0);
    for (bytes -= 1; bytes >= 0; bytes--)
        put_byte(pb, val >> bytes*8);
}

//static void put_ebml_sint(ByteIOContext *pb, unsigned int elementid, int64_t val)

static void put_ebml_binary(ByteIOContext *pb, unsigned int elementid,
                            uint8_t *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_size(pb, size, 0);
    put_buffer(pb, buf, size);
}

// XXX: should we do any special checking for valid strings for these 2 functions?
static void put_ebml_string(ByteIOContext *pb, unsigned int elementid, char *str)
{
    put_ebml_binary(pb, elementid, str, strlen(str));
}

static void put_ebml_utf8(ByteIOContext *pb, unsigned int elementid, char *str)
{
    put_ebml_binary(pb, elementid, str, strlen(str));
}

static offset_t start_ebml_master(ByteIOContext *pb, unsigned int elementid)
{
    put_ebml_id(pb, elementid);
    // XXX: this always reserves the maximum needed space to store any size value
    // we should be smarter (additional parameter for expected size?)
    put_ebml_size(pb, (1ULL<<56)-1, 0);     // largest unknown size
    return url_ftell(pb);
}

static void end_ebml_master(ByteIOContext *pb, offset_t start)
{
    offset_t pos = url_ftell(pb);

    url_fseek(pb, start - 8, SEEK_SET);
    put_ebml_size(pb, pos - start, 7);
    url_fseek(pb, pos, SEEK_SET);
}


static int mkv_write_header(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t ebml_header, segment_info, tracks;
    int i;

    ebml_header = start_ebml_master(pb, EBML_ID_HEADER);
    put_ebml_uint   (pb, EBML_ID_EBMLVERSION        ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLREADVERSION    ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXIDLENGTH    ,           4);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXSIZELENGTH  ,           8);
    put_ebml_string (pb, EBML_ID_DOCTYPE            ,  "matroska");
    put_ebml_uint   (pb, EBML_ID_DOCTYPEVERSION     ,           1);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEREADVERSION ,           1);
    end_ebml_master(pb, ebml_header);

    mkv->segment = start_ebml_master(pb, MATROSKA_ID_SEGMENT);

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if (strlen(s->title))
        put_ebml_utf8(pb, MATROSKA_ID_TITLE, s->title);
    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        put_ebml_utf8(pb, MATROSKA_ID_MUXINGAPP, LIBAVFORMAT_IDENT);
        // XXX: both are required; something better for writing app?
        put_ebml_utf8(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);
    }
    // XXX: segment UID and duration
    end_ebml_master(pb, segment_info);

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        offset_t track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY);

        end_ebml_master(pb, track);
    }
    end_ebml_master(pb, tracks);

    put_be64(pb, 0xdeadbeef);
    return 0;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    end_ebml_master(pb, mkv->segment);
    return 0;
}

AVOutputFormat matroska_muxer = {
    "matroska",
    "Matroska File Format",
    "video/x-matroska",
    "mkv",
    sizeof(MatroskaMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG4,
    mkv_write_header,
    mkv_write_packet,
    mkv_write_trailer,
    .codec_tag = (const AVCodecTag*[]){codec_bmp_tags, codec_wav_tags, 0},
};
