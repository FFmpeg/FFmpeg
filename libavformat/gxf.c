/*
 * GXF demuxer.
 * Copyright (c) 2006 Reimar Doeffinger
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

#include <inttypes.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "avformat.h"
#include "internal.h"
#include "gxf.h"
#include "libavcodec/mpeg12data.h"

struct gxf_stream_info {
    int64_t first_field;
    int64_t last_field;
    AVRational frames_per_second;
    int32_t fields_per_frame;
    int64_t track_aux_data;
};

/**
 * @brief parse gxf timecode and add it to metadata
 */
static int add_timecode_metadata(AVDictionary **pm, const char *key, uint32_t timecode, int fields_per_frame)
{
   char tmp[128];
   int field  = timecode & 0xff;
   int frame  = fields_per_frame ? field / fields_per_frame : field;
   int second = (timecode >>  8) & 0xff;
   int minute = (timecode >> 16) & 0xff;
   int hour   = (timecode >> 24) & 0x1f;
   int drop   = (timecode >> 29) & 1;
   // bit 30: color_frame, unused
   // ignore invalid time code
   if (timecode >> 31)
       return 0;
   snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d%c%02d",
       hour, minute, second, drop ? ';' : ':', frame);
   return av_dict_set(pm, key, tmp, 0);
}

/**
 * @brief parses a packet header, extracting type and length
 * @param pb AVIOContext to read header from
 * @param type detected packet type is stored here
 * @param length detected packet length, excluding header is stored here
 * @return 0 if header not found or contains invalid data, 1 otherwise
 */
static int parse_packet_header(AVIOContext *pb, GXFPktType *type, int *length) {
    if (avio_rb32(pb))
        return 0;
    if (avio_r8(pb) != 1)
        return 0;
    *type = avio_r8(pb);
    *length = avio_rb32(pb);
    if ((*length >> 24) || *length < 16)
        return 0;
    *length -= 16;
    if (avio_rb32(pb))
        return 0;
    if (avio_r8(pb) != 0xe1)
        return 0;
    if (avio_r8(pb) != 0xe2)
        return 0;
    return 1;
}

/**
 * @brief check if file starts with a PKT_MAP header
 */
static int gxf_probe(AVProbeData *p) {
    static const uint8_t startcode[] = {0, 0, 0, 0, 1, 0xbc}; // start with map packet
    static const uint8_t endcode[] = {0, 0, 0, 0, 0xe1, 0xe2};
    if (!memcmp(p->buf, startcode, sizeof(startcode)) &&
        !memcmp(&p->buf[16 - sizeof(endcode)], endcode, sizeof(endcode)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

/**
 * @brief gets the stream index for the track with the specified id, creates new
 *        stream if not found
 * @param id     id of stream to find / add
 * @param format stream format identifier
 */
static int get_sindex(AVFormatContext *s, int id, int format) {
    int i;
    AVStream *st = NULL;
    i = ff_find_stream_index(s, id);
    if (i >= 0)
        return i;
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->id = id;
    switch (format) {
        case 3:
        case 4:
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_MJPEG;
            break;
        case 13:
        case 14:
        case 15:
        case 16:
        case 25:
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_DVVIDEO;
            break;
        case 11:
        case 12:
        case 20:
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_MPEG2VIDEO;
            st->need_parsing = AVSTREAM_PARSE_HEADERS; //get keyframe flag etc.
            break;
        case 22:
        case 23:
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_MPEG1VIDEO;
            st->need_parsing = AVSTREAM_PARSE_HEADERS; //get keyframe flag etc.
            break;
        case 9:
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_id = AV_CODEC_ID_PCM_S24LE;
            st->codec->channels = 1;
            st->codec->channel_layout = AV_CH_LAYOUT_MONO;
            st->codec->sample_rate = 48000;
            st->codec->bit_rate = 3 * 1 * 48000 * 8;
            st->codec->block_align = 3 * 1;
            st->codec->bits_per_coded_sample = 24;
            break;
        case 10:
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_id = AV_CODEC_ID_PCM_S16LE;
            st->codec->channels = 1;
            st->codec->channel_layout = AV_CH_LAYOUT_MONO;
            st->codec->sample_rate = 48000;
            st->codec->bit_rate = 2 * 1 * 48000 * 8;
            st->codec->block_align = 2 * 1;
            st->codec->bits_per_coded_sample = 16;
            break;
        case 17:
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_id = AV_CODEC_ID_AC3;
            st->codec->channels = 2;
            st->codec->channel_layout = AV_CH_LAYOUT_STEREO;
            st->codec->sample_rate = 48000;
            break;
        case 26: /* AVCi50 / AVCi100 (AVC Intra) */
        case 29: /* AVCHD */
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_H264;
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
            break;
        // timecode tracks:
        case 7:
        case 8:
        case 24:
            st->codec->codec_type = AVMEDIA_TYPE_DATA;
            st->codec->codec_id = AV_CODEC_ID_NONE;
            break;
        case 30:
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_DNXHD;
            break;
        default:
            st->codec->codec_type = AVMEDIA_TYPE_UNKNOWN;
            st->codec->codec_id = AV_CODEC_ID_NONE;
            break;
    }
    return s->nb_streams - 1;
}

/**
 * @brief filters out interesting tags from material information.
 * @param len length of tag section, will be adjusted to contain remaining bytes
 * @param si struct to store collected information into
 */
static void gxf_material_tags(AVIOContext *pb, int *len, struct gxf_stream_info *si) {
    si->first_field = AV_NOPTS_VALUE;
    si->last_field = AV_NOPTS_VALUE;
    while (*len >= 2) {
        GXFMatTag tag = avio_r8(pb);
        int tlen = avio_r8(pb);
        *len -= 2;
        if (tlen > *len)
            return;
        *len -= tlen;
        if (tlen == 4) {
            uint32_t value = avio_rb32(pb);
            if (tag == MAT_FIRST_FIELD)
                si->first_field = value;
            else if (tag == MAT_LAST_FIELD)
                si->last_field = value;
        } else
            avio_skip(pb, tlen);
    }
}

static const AVRational frame_rate_tab[] = {
    {   60,    1},
    {60000, 1001},
    {   50,    1},
    {   30,    1},
    {30000, 1001},
    {   25,    1},
    {   24,    1},
    {24000, 1001},
    {    0,    0},
};

/**
 * @brief convert fps tag value to AVRational fps
 * @param fps fps value from tag
 * @return fps as AVRational, or 0 / 0 if unknown
 */
static AVRational fps_tag2avr(int32_t fps) {
    if (fps < 1 || fps > 9) fps = 9;
    return frame_rate_tab[fps - 1];
}

/**
 * @brief convert UMF attributes flags to AVRational fps
 * @param flags UMF flags to convert
 * @return fps as AVRational, or 0 / 0 if unknown
 */
static AVRational fps_umf2avr(uint32_t flags) {
    static const AVRational map[] = {{50, 1}, {60000, 1001}, {24, 1},
        {25, 1}, {30000, 1001}};
    int idx =  av_log2((flags & 0x7c0) >> 6);
    return map[idx];
}

/**
 * @brief filters out interesting tags from track information.
 * @param len length of tag section, will be adjusted to contain remaining bytes
 * @param si struct to store collected information into
 */
static void gxf_track_tags(AVIOContext *pb, int *len, struct gxf_stream_info *si) {
    si->frames_per_second = (AVRational){0, 0};
    si->fields_per_frame = 0;
    si->track_aux_data = 0x80000000;
    while (*len >= 2) {
        GXFTrackTag tag = avio_r8(pb);
        int tlen = avio_r8(pb);
        *len -= 2;
        if (tlen > *len)
            return;
        *len -= tlen;
        if (tlen == 4) {
            uint32_t value = avio_rb32(pb);
            if (tag == TRACK_FPS)
                si->frames_per_second = fps_tag2avr(value);
            else if (tag == TRACK_FPF && (value == 1 || value == 2))
                si->fields_per_frame = value;
        } else if (tlen == 8 && tag == TRACK_AUX)
            si->track_aux_data = avio_rl64(pb);
        else
            avio_skip(pb, tlen);
    }
}

/**
 * @brief read index from FLT packet into stream 0 av_index
 */
static void gxf_read_index(AVFormatContext *s, int pkt_len) {
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint32_t fields_per_map = avio_rl32(pb);
    uint32_t map_cnt = avio_rl32(pb);
    int i;
    pkt_len -= 8;
    if ((s->flags & AVFMT_FLAG_IGNIDX) || !s->streams) {
        avio_skip(pb, pkt_len);
        return;
    }
    st = s->streams[0];
    if (map_cnt > 1000) {
        av_log(s, AV_LOG_ERROR,
               "too many index entries %"PRIu32" (%"PRIx32")\n",
               map_cnt, map_cnt);
        map_cnt = 1000;
    }
    if (pkt_len < 4 * map_cnt) {
        av_log(s, AV_LOG_ERROR, "invalid index length\n");
        avio_skip(pb, pkt_len);
        return;
    }
    pkt_len -= 4 * map_cnt;
    av_add_index_entry(st, 0, 0, 0, 0, 0);
    for (i = 0; i < map_cnt; i++)
        av_add_index_entry(st, (uint64_t)avio_rl32(pb) * 1024,
                           i * (uint64_t)fields_per_map + 1, 0, 0, 0);
    avio_skip(pb, pkt_len);
}

static int gxf_header(AVFormatContext *s) {
    AVIOContext *pb = s->pb;
    GXFPktType pkt_type;
    int map_len;
    int len;
    AVRational main_timebase = {0, 0};
    struct gxf_stream_info *si = s->priv_data;
    int i;
    if (!parse_packet_header(pb, &pkt_type, &map_len) || pkt_type != PKT_MAP) {
        av_log(s, AV_LOG_ERROR, "map packet not found\n");
        return 0;
    }
    map_len -= 2;
    if (avio_r8(pb) != 0x0e0 || avio_r8(pb) != 0xff) {
        av_log(s, AV_LOG_ERROR, "unknown version or invalid map preamble\n");
        return 0;
    }
    map_len -= 2;
    len = avio_rb16(pb); // length of material data section
    if (len > map_len) {
        av_log(s, AV_LOG_ERROR, "material data longer than map data\n");
        return 0;
    }
    map_len -= len;
    gxf_material_tags(pb, &len, si);
    avio_skip(pb, len);
    map_len -= 2;
    len = avio_rb16(pb); // length of track description
    if (len > map_len) {
        av_log(s, AV_LOG_ERROR, "track description longer than map data\n");
        return 0;
    }
    map_len -= len;
    while (len > 0) {
        int track_type, track_id, track_len;
        AVStream *st;
        int idx;
        len -= 4;
        track_type = avio_r8(pb);
        track_id = avio_r8(pb);
        track_len = avio_rb16(pb);
        len -= track_len;
        if (!(track_type & 0x80)) {
           av_log(s, AV_LOG_ERROR, "invalid track type %x\n", track_type);
           continue;
        }
        track_type &= 0x7f;
        if ((track_id & 0xc0) != 0xc0) {
           av_log(s, AV_LOG_ERROR, "invalid track id %x\n", track_id);
           continue;
        }
        track_id &= 0x3f;
        gxf_track_tags(pb, &track_len, si);
        // check for timecode tracks
        if (track_type == 7 || track_type == 8 || track_type == 24) {
            add_timecode_metadata(&s->metadata, "timecode",
                                  si->track_aux_data & 0xffffffff,
                                  si->fields_per_frame);

        }
        avio_skip(pb, track_len);

        idx = get_sindex(s, track_id, track_type);
        if (idx < 0) continue;
        st = s->streams[idx];
        if (!main_timebase.num || !main_timebase.den) {
            main_timebase.num = si->frames_per_second.den;
            main_timebase.den = si->frames_per_second.num * 2;
        }
        st->start_time = si->first_field;
        if (si->first_field != AV_NOPTS_VALUE && si->last_field != AV_NOPTS_VALUE)
            st->duration = si->last_field - si->first_field;
    }
    if (len < 0)
        av_log(s, AV_LOG_ERROR, "invalid track description length specified\n");
    if (map_len)
        avio_skip(pb, map_len);
    if (!parse_packet_header(pb, &pkt_type, &len)) {
        av_log(s, AV_LOG_ERROR, "sync lost in header\n");
        return -1;
    }
    if (pkt_type == PKT_FLT) {
        gxf_read_index(s, len);
        if (!parse_packet_header(pb, &pkt_type, &len)) {
            av_log(s, AV_LOG_ERROR, "sync lost in header\n");
            return -1;
        }
    }
    if (pkt_type == PKT_UMF) {
        if (len >= 0x39) {
            AVRational fps;
            len -= 0x39;
            avio_skip(pb, 5); // preamble
            avio_skip(pb, 0x30); // payload description
            fps = fps_umf2avr(avio_rl32(pb));
            if (!main_timebase.num || !main_timebase.den) {
                av_log(s, AV_LOG_WARNING, "No FPS track tag, using UMF fps tag."
                                          " This might give wrong results.\n");
                // this may not always be correct, but simply the best we can get
                main_timebase.num = fps.den;
                main_timebase.den = fps.num * 2;
            }

            if (len >= 0x18) {
                len -= 0x18;
                avio_skip(pb, 0x10);
                add_timecode_metadata(&s->metadata, "timecode_at_mark_in",
                                      avio_rl32(pb), si->fields_per_frame);
                add_timecode_metadata(&s->metadata, "timecode_at_mark_out",
                                      avio_rl32(pb), si->fields_per_frame);
            }
        } else
            av_log(s, AV_LOG_INFO, "UMF packet too short\n");
    } else
        av_log(s, AV_LOG_INFO, "UMF packet missing\n");
    avio_skip(pb, len);
    // set a fallback value, 60000/1001 is specified for audio-only files
    // so use that regardless of why we do not know the video frame rate.
    if (!main_timebase.num || !main_timebase.den)
        main_timebase = (AVRational){1001, 60000};
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        avpriv_set_pts_info(st, 32, main_timebase.num, main_timebase.den);
    }
    return 0;
}

#define READ_ONE() \
    { \
        if (!max_interval-- || avio_feof(pb)) \
            goto out; \
        tmp = tmp << 8 | avio_r8(pb); \
    }

/**
 * @brief resync the stream on the next media packet with specified properties
 * @param max_interval how many bytes to search for matching packet at most
 * @param track track id the media packet must belong to, -1 for any
 * @param timestamp minimum timestamp (== field number) the packet must have, -1 for any
 * @return timestamp of packet found
 */
static int64_t gxf_resync_media(AVFormatContext *s, uint64_t max_interval, int track, int timestamp) {
    uint32_t tmp;
    uint64_t last_pos;
    uint64_t last_found_pos = 0;
    int cur_track;
    int64_t cur_timestamp = AV_NOPTS_VALUE;
    int len;
    AVIOContext *pb = s->pb;
    GXFPktType type;
    tmp = avio_rb32(pb);
start:
    while (tmp)
        READ_ONE();
    READ_ONE();
    if (tmp != 1)
        goto start;
    last_pos = avio_tell(pb);
    if (avio_seek(pb, -5, SEEK_CUR) < 0)
        goto out;
    if (!parse_packet_header(pb, &type, &len) || type != PKT_MEDIA) {
        if (avio_seek(pb, last_pos, SEEK_SET) < 0)
            goto out;
        goto start;
    }
    avio_r8(pb);
    cur_track = avio_r8(pb);
    cur_timestamp = avio_rb32(pb);
    last_found_pos = avio_tell(pb) - 16 - 6;
    if ((track >= 0 && track != cur_track) || (timestamp >= 0 && timestamp > cur_timestamp)) {
        if (avio_seek(pb, last_pos, SEEK_SET) >= 0)
            goto start;
    }
out:
    if (last_found_pos)
        avio_seek(pb, last_found_pos, SEEK_SET);
    return cur_timestamp;
}

static int gxf_packet(AVFormatContext *s, AVPacket *pkt) {
    AVIOContext *pb = s->pb;
    GXFPktType pkt_type;
    int pkt_len;
    struct gxf_stream_info *si = s->priv_data;

    while (!pb->eof_reached) {
        AVStream *st;
        int track_type, track_id, ret;
        int field_nr, field_info, skip = 0;
        int stream_index;
        if (!parse_packet_header(pb, &pkt_type, &pkt_len)) {
            if (!avio_feof(pb))
                av_log(s, AV_LOG_ERROR, "sync lost\n");
            return -1;
        }
        if (pkt_type == PKT_FLT) {
            gxf_read_index(s, pkt_len);
            continue;
        }
        if (pkt_type != PKT_MEDIA) {
            avio_skip(pb, pkt_len);
            continue;
        }
        if (pkt_len < 16) {
            av_log(s, AV_LOG_ERROR, "invalid media packet length\n");
            continue;
        }
        pkt_len -= 16;
        track_type = avio_r8(pb);
        track_id = avio_r8(pb);
        stream_index = get_sindex(s, track_id, track_type);
        if (stream_index < 0)
            return stream_index;
        st = s->streams[stream_index];
        field_nr = avio_rb32(pb);
        field_info = avio_rb32(pb);
        avio_rb32(pb); // "timeline" field number
        avio_r8(pb); // flags
        avio_r8(pb); // reserved
        if (st->codec->codec_id == AV_CODEC_ID_PCM_S24LE ||
            st->codec->codec_id == AV_CODEC_ID_PCM_S16LE) {
            int first = field_info >> 16;
            int last  = field_info & 0xffff; // last is exclusive
            int bps = av_get_bits_per_sample(st->codec->codec_id)>>3;
            if (first <= last && last*bps <= pkt_len) {
                avio_skip(pb, first*bps);
                skip = pkt_len - last*bps;
                pkt_len = (last-first)*bps;
            } else
                av_log(s, AV_LOG_ERROR, "invalid first and last sample values\n");
        }
        ret = av_get_packet(pb, pkt, pkt_len);
        if (skip)
            avio_skip(pb, skip);
        pkt->stream_index = stream_index;
        pkt->dts = field_nr;

        //set duration manually for DV or else lavf misdetects the frame rate
        if (st->codec->codec_id == AV_CODEC_ID_DVVIDEO)
            pkt->duration = si->fields_per_frame;

        return ret;
    }
    return AVERROR_EOF;
}

static int gxf_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags) {
    int64_t res = 0;
    uint64_t pos;
    uint64_t maxlen = 100 * 1024 * 1024;
    AVStream *st = s->streams[0];
    int64_t start_time = s->streams[stream_index]->start_time;
    int64_t found;
    int idx;
    if (timestamp < start_time) timestamp = start_time;
    idx = av_index_search_timestamp(st, timestamp - start_time,
                                    AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
    if (idx < 0)
        return -1;
    pos = st->index_entries[idx].pos;
    if (idx < st->nb_index_entries - 2)
        maxlen = st->index_entries[idx + 2].pos - pos;
    maxlen = FFMAX(maxlen, 200 * 1024);
    res = avio_seek(s->pb, pos, SEEK_SET);
    if (res < 0)
        return res;
    found = gxf_resync_media(s, maxlen, -1, timestamp);
    if (FFABS(found - timestamp) > 4)
        return -1;
    return 0;
}

static int64_t gxf_read_timestamp(AVFormatContext *s, int stream_index,
                                  int64_t *pos, int64_t pos_limit) {
    AVIOContext *pb = s->pb;
    int64_t res;
    if (avio_seek(pb, *pos, SEEK_SET) < 0)
        return AV_NOPTS_VALUE;
    res = gxf_resync_media(s, pos_limit - *pos, -1, -1);
    *pos = avio_tell(pb);
    return res;
}

AVInputFormat ff_gxf_demuxer = {
    .name           = "gxf",
    .long_name      = NULL_IF_CONFIG_SMALL("GXF (General eXchange Format)"),
    .priv_data_size = sizeof(struct gxf_stream_info),
    .read_probe     = gxf_probe,
    .read_header    = gxf_header,
    .read_packet    = gxf_packet,
    .read_seek      = gxf_seek,
    .read_timestamp = gxf_read_timestamp,
};
