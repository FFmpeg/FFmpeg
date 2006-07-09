/*
 * GXF demuxer.
 * Copyright (c) 2006 Reimar Doeffinger.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "avformat.h"
#include "avi.h"

typedef enum {
    PKT_MAP = 0xbc,
    PKT_MEDIA = 0xbf,
    PKT_EOS = 0xfb,
    PKT_FLT = 0xfc,
    PKT_UMF = 0xfd
} pkt_type_t;

/**
 * \brief parses a packet header, extracting type and length
 * \param pb ByteIOContext to read header from
 * \param type detected packet type is stored here
 * \param length detected packet length, excluding header is stored here
 * \return 0 if header not found or contains invalid data, 1 otherwise
 */
static int parse_packet_header(ByteIOContext *pb, pkt_type_t *type, int *length) {
    if (get_be32(pb))
        return 0;
    if (get_byte(pb) != 1)
        return 0;
    *type = get_byte(pb);
    *length = get_be32(pb);
    if ((*length >> 24) || *length < 16)
        return 0;
    *length -= 16;
    if (get_be32(pb))
        return 0;
    if (get_byte(pb) != 0xe1)
        return 0;
    if (get_byte(pb) != 0xe2)
        return 0;
    return 1;
}

/**
 * \brief check if file starts with a PKT_MAP header
 */
static int gxf_probe(AVProbeData *p) {
    static const uint8_t startcode[] = {0, 0, 0, 0, 1, 0xbc}; // start with map packet
    static const uint8_t endcode[] = {0, 0, 0, 0, 0xe1, 0xe2};
    if (p->buf_size < 16)
        return 0;
    if (!memcmp(p->buf, startcode, sizeof(startcode)) &&
        !memcmp(&p->buf[16 - sizeof(endcode)], endcode, sizeof(endcode)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

/**
 * \brief gets the stream index for the track with the specified id, creates new
 *        stream if not found
 * \param stream id of stream to find / add
 * \param format stream format identifier
 */
static int get_sindex(AVFormatContext *s, int id, int format) {
    int i;
    AVStream *st = NULL;
    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->id == id)
            return i;
    }
    st = av_new_stream(s, id);
    switch (format) {
        case 3:
        case 4:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = CODEC_ID_MJPEG;
            st->codec->codec_tag = MKTAG('M', 'J', 'P', 'G');
            break;
        case 13:
        case 15:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = CODEC_ID_DVVIDEO;
            st->codec->codec_tag = MKTAG('d', 'v', 'c', ' ');
            break;
        case 14:
        case 16:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = CODEC_ID_DVVIDEO;
            st->codec->codec_tag = MKTAG('d', 'v', 'c', 'p');
            break;
        case 11:
        case 12:
        case 20:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = CODEC_ID_MPEG2VIDEO;
            st->codec->codec_tag = MKTAG('M', 'P', 'G', '2');
            break;
        case 22:
        case 23:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = CODEC_ID_MPEG1VIDEO;
            st->codec->codec_tag = MKTAG('M', 'P', 'G', '1');
            break;
        case 9:
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = CODEC_ID_PCM_S24LE;
            st->codec->codec_tag = 0x1;
            st->codec->channels = 1;
            st->codec->sample_rate = 48000;
            st->codec->bit_rate = 3 * 1 * 48000 * 8;
            st->codec->block_align = 3 * 1;
            st->codec->bits_per_sample = 24;
            break;
        case 10:
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = CODEC_ID_PCM_S16LE;
            st->codec->codec_tag = 0x1;
            st->codec->channels = 1;
            st->codec->sample_rate = 48000;
            st->codec->bit_rate = 2 * 1 * 48000 * 8;
            st->codec->block_align = 2 * 1;
            st->codec->bits_per_sample = 16;
            break;
        case 17:
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = CODEC_ID_AC3;
            st->codec->codec_tag = 0x2000;
            st->codec->channels = 2;
            st->codec->sample_rate = 48000;
            break;
        default:
            st->codec->codec_type = CODEC_TYPE_UNKNOWN;
            st->codec->codec_id = CODEC_ID_NONE;
            break;
    }
    return s->nb_streams - 1;
}

static int gxf_header(AVFormatContext *s, AVFormatParameters *ap) {
    ByteIOContext *pb = &s->pb;
    pkt_type_t pkt_type;
    int map_len;
    int len;
    if (!parse_packet_header(pb, &pkt_type, &map_len) || pkt_type != PKT_MAP) {
        av_log(s, AV_LOG_ERROR, "GXF: map packet not found\n");
        return 0;
    }
    map_len -= 2;
    if (get_byte(pb) != 0x0e0 || get_byte(pb) != 0xff) {
        av_log(s, AV_LOG_ERROR, "GXF: unknown version or invalid map preamble\n");
        return 0;
    }
    map_len -= 2;
    len = get_be16(pb); // length of material data section
    if (len > map_len) {
        av_log(s, AV_LOG_ERROR, "GXF: material data longer than map data\n");
        return 0;
    }
    map_len -= len;
    url_fskip(pb, len);
    map_len -= 2;
    len = get_be16(pb); // length of track description
    if (len > map_len) {
        av_log(s, AV_LOG_ERROR, "GXF: track description longer than map data\n");
        return 0;
    }
    map_len -= len;
    while (len > 0) {
        int track_type, track_id, track_len;
        len -= 4;
        track_type = get_byte(pb);
        track_id = get_byte(pb);
        track_len = get_be16(pb);
        len -= track_len;
        url_fskip(pb, track_len);
        if (!(track_type & 0x80)) {
           av_log(s, AV_LOG_ERROR, "GXF: invalid track type %x\n", track_type);
           continue;
        }
        track_type &= 0x7f;
        if ((track_id & 0xc0) != 0xc0) {
           av_log(s, AV_LOG_ERROR, "GXF: invalid track id %x\n", track_id);
           continue;
        }
        track_id &= 0x3f;
        get_sindex(s, track_id, track_type);
    }
    if (len < 0)
        av_log(s, AV_LOG_ERROR, "GXF: invalid track description length specified\n");
    if (map_len)
        url_fskip(pb, map_len);
    return 0;
}

static int gxf_packet(AVFormatContext *s, AVPacket *pkt) {
    ByteIOContext *pb = &s->pb;
    pkt_type_t pkt_type;
    int pkt_len;
    while (!url_feof(pb)) {
        int track_type, track_id, ret;
        if (!parse_packet_header(pb, &pkt_type, &pkt_len)) {
            if (!url_feof(pb))
                av_log(s, AV_LOG_ERROR, "GXF: sync lost\n");
            return -1;
        }
        if (pkt_type != PKT_MEDIA) {
            url_fskip(pb, pkt_len);
            continue;
        }
        if (pkt_len < 16) {
            av_log(s, AV_LOG_ERROR, "GXF: invalid media packet length\n");
            continue;
        }
        pkt_len -= 16;
        track_type = get_byte(pb);
        track_id = get_byte(pb);
        get_be32(pb); // field number
        get_be32(pb); // field information
        get_be32(pb); // "timeline" field number
        get_byte(pb); // flags
        get_byte(pb); // reserved
        // NOTE: there is also data length information in the
        // field information, it might be better to take this int account
        // as well.
        ret = av_get_packet(pb, pkt, pkt_len);
        pkt->stream_index = get_sindex(s, track_id, track_type);
        return ret;
    }
    return AVERROR_IO;
}

static AVInputFormat gxf_demuxer = {
    "gxf",
    "GXF format",
    0,
    gxf_probe,
    gxf_header,
    gxf_packet,
    NULL,
    NULL,
};

int gxf_init(void) {
    av_register_input_format(&gxf_demuxer);
    return 0;
}

