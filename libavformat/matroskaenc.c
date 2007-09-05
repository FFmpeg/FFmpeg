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
#include "xiph.h"
#include "matroska.h"

typedef struct MatroskaMuxContext {
    offset_t    segment;
    offset_t    cluster;
    uint64_t    cluster_pts;
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

static void put_ebml_float(ByteIOContext *pb, unsigned int elementid, double val)
{
    // XXX: single-precision floats?
    put_ebml_id(pb, elementid);
    put_ebml_size(pb, 8, 0);
    put_be64(pb, av_dbl2int(val));
}

static void put_ebml_binary(ByteIOContext *pb, unsigned int elementid,
                            const uint8_t *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_size(pb, size, 0);
    put_buffer(pb, buf, size);
}

static void put_ebml_string(ByteIOContext *pb, unsigned int elementid, const char *str)
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
    int i, j, k;

    ebml_header = start_ebml_master(pb, EBML_ID_HEADER);
    put_ebml_uint   (pb, EBML_ID_EBMLVERSION        ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLREADVERSION    ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXIDLENGTH    ,           4);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXSIZELENGTH  ,           8);
    put_ebml_string (pb, EBML_ID_DOCTYPE            ,  "matroska");
    put_ebml_uint   (pb, EBML_ID_DOCTYPEVERSION     ,           2);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEREADVERSION ,           2);
    end_ebml_master(pb, ebml_header);

    mkv->segment = start_ebml_master(pb, MATROSKA_ID_SEGMENT);

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if (strlen(s->title))
        put_ebml_string(pb, MATROSKA_ID_TITLE, s->title);
    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP, LIBAVFORMAT_IDENT);
        // XXX: both are required; something better for writing app?
        put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);
    }
    // XXX: segment UID and duration
    end_ebml_master(pb, segment_info);

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecContext *codec = st->codec;
        offset_t subinfo, track;
        int native_id = 0;

        track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY);
        put_ebml_uint (pb, MATROSKA_ID_TRACKNUMBER     , i);
        // XXX: random number for UID? and can we use the same UID when copying
        // from another MKV as the specs recommend?
        put_ebml_uint (pb, MATROSKA_ID_TRACKUID        , i);
        put_ebml_uint (pb, MATROSKA_ID_TRACKFLAGLACING , 0);    // no lacing (yet)

        if (st->language[0])
            put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, st->language);

        // look for a codec id string specific to mkv to use, if none are found, use AVI codes
        for (j = 0; ff_mkv_codec_tags[j].id != CODEC_ID_NONE; j++) {
            if (ff_mkv_codec_tags[j].id == codec->codec_id) {
                put_ebml_string(pb, MATROSKA_ID_CODECID, ff_mkv_codec_tags[j].str);
                native_id = 1;
                break;
            }
        }

        // XXX: CodecPrivate for vorbis, theora, aac, native mpeg4, ...
        if (native_id) {
            offset_t codecprivate;

            if (codec->codec_id == CODEC_ID_VORBIS || codec->codec_id == CODEC_ID_THEORA) {
                uint8_t *header_start[3];
                int header_len[3];
                int first_header_size;

                if (codec->codec_id == CODEC_ID_VORBIS)
                    first_header_size = 30;
                else
                    first_header_size = 42;

                if (ff_split_xiph_headers(codec->extradata, codec->extradata_size,
                                          first_header_size, header_start, header_len) < 0) {
                    av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
                    return -1;
                }

                codecprivate = start_ebml_master(pb, MATROSKA_ID_CODECPRIVATE);
                put_byte(pb, 2);                    // number packets - 1
                for (j = 0; j < 2; j++) {
                    for (k = 0; k < header_len[j] / 255; k++)
                        put_byte(pb, 255);
                    put_byte(pb, header_len[j] % 255);
                }
                for (j = 0; j < 3; j++)
                    put_buffer(pb, header_start[j], header_len[j]);
                end_ebml_master(pb, codecprivate);
            } else {
                put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codec->extradata, codec->extradata_size);
            }
        }

        switch (codec->codec_type) {
            case CODEC_TYPE_VIDEO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);

                if (!native_id) {
                    offset_t bmp_header;
                    // if there is no mkv-specific codec id, use VFW mode
                    if (!codec->codec_tag)
                        codec->codec_tag = codec_get_tag(codec_bmp_tags, codec->codec_id);

                    put_ebml_string(pb, MATROSKA_ID_CODECID, MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC);
                    // XXX: codec private isn't a master; is there a better way to re-use put_bmp_header?
                    bmp_header = start_ebml_master(pb, MATROSKA_ID_CODECPRIVATE);
                    put_bmp_header(pb, codec, codec_bmp_tags, 0);
                    end_ebml_master(pb, bmp_header);
                }
                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKVIDEO);
                // XXX: interlace flag?
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELWIDTH , codec->width);
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELHEIGHT, codec->height);
                // XXX: display width/height
                end_ebml_master(pb, subinfo);
                break;

            case CODEC_TYPE_AUDIO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_AUDIO);

                // XXX: A_MS/ACM
                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKAUDIO);
                put_ebml_uint  (pb, MATROSKA_ID_AUDIOCHANNELS    , codec->channels);
                put_ebml_float (pb, MATROSKA_ID_AUDIOSAMPLINGFREQ, codec->sample_rate);
                // XXX: output sample freq (for sbr) and bitdepth (for pcm)
                end_ebml_master(pb, subinfo);
                break;

            default:
                av_log(s, AV_LOG_ERROR, "Only audio and video are supported for Matroska.");
                break;
        }
        end_ebml_master(pb, track);

        // ms precision is the de-facto standard timescale for mkv files
        av_set_pts_info(st, 64, 1, 1000);
    }
    end_ebml_master(pb, tracks);

    mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER);
    put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, 0);
    mkv->cluster_pts = 0;

    return 0;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t block;

    // start a new cluster every 5 MB or 5 sec
    if (url_ftell(pb) > mkv->cluster + 5*1024*1024 || pkt->pts > mkv->cluster_pts + 5000) {
        end_ebml_master(pb, mkv->cluster);
        mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER);
        put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, pkt->pts);
        mkv->cluster_pts = pkt->pts;
    }

    block = start_ebml_master(pb, MATROSKA_ID_SIMPLEBLOCK);
    put_byte(pb, 0x80 | pkt->stream_index);     // this assumes stream_index is less than 127
    put_be16(pb, pkt->pts - mkv->cluster_pts);
    put_byte(pb, !!(pkt->flags & PKT_FLAG_KEY));
    put_buffer(pb, pkt->data, pkt->size);
    end_ebml_master(pb, block);
    return 0;
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    end_ebml_master(pb, mkv->cluster);
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
