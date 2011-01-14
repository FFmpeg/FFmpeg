/*
 * Windows Television (WTV) demuxer
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * Windows Television (WTV) demuxer
 * @author Peter Ross <pross@xvid.org>
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "riff.h"
#include "asf.h"
#include "mpegts.h"

/* Macros for formating GUIDs */
#define PRI_GUID \
    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
#define ARG_GUID(g) \
    g[0],g[1],g[2],g[3],g[4],g[5],g[6],g[7],g[8],g[9],g[10],g[11],g[12],g[13],g[14],g[15]

typedef struct {
    int seen_data;
} WtvStream;

typedef struct WtvContext {
    uint64_t pts;
} WtvContext;

static int is_zero(uint8_t *v, int n)
{
   int i;
   for (i = 0; i < n; i++)
      if (v[i]) return 0;
   return 1;
}

typedef struct {
    enum CodecID id;
    ff_asf_guid guid;
} AVCodecGuid;

static enum CodecID ff_codec_guid_get_id(const AVCodecGuid *guids, ff_asf_guid guid)
{
    int i;
    for (i = 0; guids[i].id != CODEC_ID_NONE; i++) {
        if (!ff_guidcmp(guids[i].guid, guid))
            return guids[i].id;
    }
    return CODEC_ID_NONE;
}

/* WTV GUIDs */
static const ff_asf_guid wtv_guid =
    {0xB7,0xD8,0x00,0x20,0x37,0x49,0xDA,0x11,0xA6,0x4E,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid meta_guid =
    {0x5A,0xFE,0xD7,0x6D,0xC8,0x1D,0x8F,0x4A,0x99,0x22,0xFA,0xB1,0x1C,0x38,0x14,0x53};
static const ff_asf_guid timestamp_guid =
    {0x5B,0x05,0xE6,0x1B,0x97,0xA9,0x49,0x43,0x88,0x17,0x1A,0x65,0x5A,0x29,0x8A,0x97};
static const ff_asf_guid data_guid =
    {0x95,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid stream_guid =
    {0xED,0xA4,0x13,0x23,0x2D,0xBF,0x4F,0x45,0xAD,0x8A,0xD9,0x5B,0xA7,0xF9,0x1F,0xEE};
static const ff_asf_guid stream2_guid =
    {0xA2,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid EVENTID_SubtitleSpanningEvent =
    {0x48,0xC0,0xCE,0x5D,0xB9,0xD0,0x63,0x41,0x87,0x2C,0x4F,0x32,0x22,0x3B,0xE8,0x8A};
static const ff_asf_guid EVENTID_LanguageSpanningEvent =
    {0x6D,0x66,0x92,0xE2,0x02,0x9C,0x8D,0x44,0xAA,0x8D,0x78,0x1A,0x93,0xFD,0xC3,0x95};
static const ff_asf_guid EVENTID_AudioDescriptorSpanningEvent =
    {0x1C,0xD4,0x7B,0x10,0xDA,0xA6,0x91,0x46,0x83,0x69,0x11,0xB2,0xCD,0xAA,0x28,0x8E};
static const ff_asf_guid EVENTID_CtxADescriptorSpanningEvent =
    {0xE6,0xA2,0xB4,0x3A,0x47,0x42,0x34,0x4B,0x89,0x6C,0x30,0xAF,0xA5,0xD2,0x1C,0x24};
static const ff_asf_guid EVENTID_CSDescriptorSpanningEvent =
    {0xD9,0x79,0xE7,0xEf,0xF0,0x97,0x86,0x47,0x80,0x0D,0x95,0xCF,0x50,0x5D,0xDC,0x66};
static const ff_asf_guid EVENTID_DVBScramblingControlSpanningEvent =
    {0xC4,0xE1,0xD4,0x4B,0xA1,0x90,0x09,0x41,0x82,0x36,0x27,0xF0,0x0E,0x7D,0xCC,0x5B};
static const ff_asf_guid EVENTID_StreamIDSpanningEvent =
    {0x68,0xAB,0xF1,0xCA,0x53,0xE1,0x41,0x4D,0xA6,0xB3,0xA7,0xC9,0x98,0xDB,0x75,0xEE};
static const ff_asf_guid EVENTID_TeletextSpanningEvent =
    {0x50,0xD9,0x99,0x95,0x33,0x5F,0x17,0x46,0xAF,0x7C,0x1E,0x54,0xB5,0x10,0xDA,0xA3};

/* Windows media GUIDs */

#define MEDIASUBTYPE_BASE_GUID \
    0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71

/* Media types */
static const ff_asf_guid mediatype_audio =
    {'a','u','d','s',MEDIASUBTYPE_BASE_GUID};
static const ff_asf_guid mediatype_video =
    {'v','i','d','s',MEDIASUBTYPE_BASE_GUID};
static const ff_asf_guid mediasubtype_mpeg1payload =
    {0x81,0xEB,0x36,0xE4,0x4F,0x52,0xCE,0x11,0x9F,0x53,0x00,0x20,0xAF,0x0B,0xA7,0x70};
static const ff_asf_guid mediatype_mpeg2_sections =
    {0x6C,0x17,0x5F,0x45,0x06,0x4B,0xCE,0x47,0x9A,0xEF,0x8C,0xAE,0xF7,0x3D,0xF7,0xB5};
static const ff_asf_guid mediatype_mpeg2_pes =
    {0x20,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA};
static const ff_asf_guid mediatype_mstvcaption =
    {0x89,0x8A,0x8B,0xB8,0x49,0xB0,0x80,0x4C,0xAD,0xCF,0x58,0x98,0x98,0x5E,0x22,0xC1};

/* Media subtypes */
static const ff_asf_guid mediasubtype_cpfilters_processed =
    {0x28,0xBD,0xAD,0x46,0xD0,0x6F,0x96,0x47,0x93,0xB2,0x15,0x5C,0x51,0xDC,0x04,0x8D};
static const ff_asf_guid mediasubtype_dvb_subtitle =
    {0xC3,0xCB,0xFF,0x34,0xB3,0xD5,0x71,0x41,0x90,0x02,0xD4,0xC6,0x03,0x01,0x69,0x7F};
static const ff_asf_guid mediasubtype_teletext =
    {0xE3,0x76,0x2A,0xF7,0x0A,0xEB,0xD0,0x11,0xAC,0xE4,0x00,0x00,0xC0,0xCC,0x16,0xBA};
static const ff_asf_guid mediasubtype_dtvccdata =
    {0xAA,0xDD,0x2A,0xF5,0xF0,0x36,0xF5,0x43,0x95,0xEA,0x6D,0x86,0x64,0x84,0x26,0x2A};
static const ff_asf_guid mediasubtype_mpeg2_sections =
    {0x79,0x85,0x9F,0x4A,0xF8,0x6B,0x92,0x43,0x8A,0x6D,0xD2,0xDD,0x09,0xFA,0x78,0x61};

/* Formats */
static const ff_asf_guid format_cpfilters_processed =
    {0x6F,0xB3,0x39,0x67,0x5F,0x1D,0xC2,0x4A,0x81,0x92,0x28,0xBB,0x0E,0x73,0xD1,0x6A};
static const ff_asf_guid format_waveformatex =
    {0x81,0x9F,0x58,0x05,0x56,0xC3,0xCE,0x11,0xBF,0x01,0x00,0xAA,0x00,0x55,0x59,0x5A};
static const ff_asf_guid format_videoinfo2 =
    {0xA0,0x76,0x2A,0xF7,0x0A,0xEB,0xD0,0x11,0xAC,0xE4,0x00,0x00,0xC0,0xCC,0x16,0xBA};
static const ff_asf_guid format_mpeg2_video =
    {0xE3,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA};
static const ff_asf_guid format_none =
    {0xD6,0x17,0x64,0x0F,0x18,0xC3,0xD0,0x11,0xA4,0x3F,0x00,0xA0,0xC9,0x22,0x31,0x96};

static const AVCodecGuid video_guids[] = {
    {CODEC_ID_MPEG2VIDEO, {0x26,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA}},
    {CODEC_ID_NONE}
};

static const AVCodecGuid audio_guids[] = {
    {CODEC_ID_AC3,        {0x2C,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA}},
    {CODEC_ID_EAC3,       {0xAF,0x87,0xFB,0xA7,0x02,0x2D,0xFB,0x42,0xA4,0xD4,0x05,0xCD,0x93,0x84,0x3B,0xDD}},
    {CODEC_ID_MP2,        {0x2B,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA}},
    {CODEC_ID_NONE}
};

static int read_probe(AVProbeData *p)
{
    return ff_guidcmp(p->buf, wtv_guid) ? 0 : AVPROBE_SCORE_MAX;
}

/**
 * parse VIDEOINFOHEADER2 structure
 * @return bytes consumed
 */
static int parse_videoinfoheader2(AVFormatContext *s, AVStream *st)
{
    ByteIOContext *pb = s->pb;

    url_fskip(pb, 72);  // picture aspect ratio is unreliable
    ff_get_bmp_header(pb, st);

    return 72 + 40;
}

/**
 * Parse MPEG1WAVEFORMATEX extradata structure
 */
static void parse_mpeg1waveformatex(AVStream *st)
{
    /* fwHeadLayer */
    switch (AV_RL16(st->codec->extradata)) {
    case 0x0001 : st->codec->codec_id = CODEC_ID_MP1; break;
    case 0x0002 : st->codec->codec_id = CODEC_ID_MP2; break;
    case 0x0004 : st->codec->codec_id = CODEC_ID_MP3; break;
    }

    st->codec->bit_rate = AV_RL32(st->codec->extradata + 2); /* dwHeadBitrate */

    /* dwHeadMode */
    switch (AV_RL16(st->codec->extradata + 6)) {
    case 1 : case 2 : case 4 : st->codec->channels = 2; break;
    case 8 :                   st->codec->channels = 1; break;
    }
}

/**
 * Initialise stream
 * @param st Stream to initialise, or NULL to create and initialise new stream
 * @return NULL on error
 */
static AVStream * new_stream(AVFormatContext *s, AVStream *st, int sid, int codec_type)
{
    if (st) {
        if (st->codec->extradata) {
            av_freep(&st->codec->extradata);
            st->codec->extradata_size = 0;
        }
    } else {
        WtvStream *wst = av_mallocz(sizeof(WtvStream));
        if (!wst)
            return NULL;
        st = av_new_stream(s, sid);
        if (!st)
            return NULL;
        st->priv_data = wst;
    }
    st->codec->codec_type = codec_type;
    st->need_parsing      = AVSTREAM_PARSE_FULL;
    av_set_pts_info(st, 64, 1, 10000000);
    return st;
}

/**
 * parse Media Type structure and populate stream
 * @param st         Stream, or NULL to create new stream
 * @param mediatype  Mediatype GUID
 * @param subtype    Subtype GUID
 * @param formattype Format GUID
 * @param size       Size of format buffer
 * @return NULL on error
 */
static AVStream * parse_media_type(AVFormatContext *s, AVStream *st, int sid,
                                   ff_asf_guid mediatype, ff_asf_guid subtype,
                                   ff_asf_guid formattype, int size)
{
    ByteIOContext *pb = s->pb;
    if (!ff_guidcmp(subtype, mediasubtype_cpfilters_processed) &&
        !ff_guidcmp(formattype, format_cpfilters_processed)) {
        ff_asf_guid actual_subtype;
        ff_asf_guid actual_formattype;

        if (size < 32) {
            av_log(s, AV_LOG_WARNING, "format buffer size underflow\n");
            url_fskip(pb, size);
            return NULL;
        }

        url_fskip(pb, size - 32);
        ff_get_guid(pb, &actual_subtype);
        ff_get_guid(pb, &actual_formattype);
        url_fseek(pb, -size, SEEK_CUR);

        st = parse_media_type(s, st, sid, mediatype, actual_subtype, actual_formattype, size - 32);
        url_fskip(pb, 32);
        return st;
    } else if (!ff_guidcmp(mediatype, mediatype_audio)) {
        st = new_stream(s, st, sid, AVMEDIA_TYPE_AUDIO);
        if (!st)
            return NULL;
        if (!ff_guidcmp(formattype, format_waveformatex)) {
            ff_get_wav_header(pb, st->codec, size);
        } else {
            if (ff_guidcmp(formattype, format_none))
                av_log(s, AV_LOG_WARNING, "unknown formattype:"PRI_GUID"\n", ARG_GUID(formattype));
            url_fskip(pb, size);
        }

        if (!memcmp(subtype + 4, (const uint8_t[]){MEDIASUBTYPE_BASE_GUID}, 12)) {
            st->codec->codec_id = ff_wav_codec_get_id(AV_RL32(subtype), st->codec->bits_per_coded_sample);
        } else if (!ff_guidcmp(subtype, mediasubtype_mpeg1payload)) {
            if (st->codec->extradata && st->codec->extradata_size >= 22)
                parse_mpeg1waveformatex(st);
            else
                av_log(s, AV_LOG_WARNING, "MPEG1WAVEFORMATEX underflow\n");
        } else {
            st->codec->codec_id = ff_codec_guid_get_id(audio_guids, subtype);
            if (st->codec->codec_id == CODEC_ID_NONE)
                av_log(s, AV_LOG_WARNING, "unknown subtype:"PRI_GUID"\n", ARG_GUID(subtype));
        }
        return st;
    } else if (!ff_guidcmp(mediatype, mediatype_video)) {
        st = new_stream(s, st, sid, AVMEDIA_TYPE_VIDEO);
        if (!st)
            return NULL;
        if (!ff_guidcmp(formattype, format_videoinfo2)) {
            int consumed = parse_videoinfoheader2(s, st);
            url_fskip(pb, FFMAX(size - consumed, 0));
        } else if (!ff_guidcmp(formattype, format_mpeg2_video)) {
            int consumed = parse_videoinfoheader2(s, st);
            url_fskip(pb, FFMAX(size - consumed, 0));
        } else {
            if (ff_guidcmp(formattype, format_none))
                av_log(s, AV_LOG_WARNING, "unknown formattype:"PRI_GUID"\n", ARG_GUID(formattype));
            url_fskip(pb, size);
        }

        if (!memcmp(subtype + 4, (const uint8_t[]){MEDIASUBTYPE_BASE_GUID}, 12)) {
            st->codec->codec_id = ff_codec_get_id(ff_codec_bmp_tags, AV_RL32(subtype));
        } else {
            st->codec->codec_id = ff_codec_guid_get_id(video_guids, subtype);
        }
        if (st->codec->codec_id == CODEC_ID_NONE)
            av_log(s, AV_LOG_WARNING, "unknown subtype:"PRI_GUID"\n", ARG_GUID(subtype));
        return st;
    } else if (!ff_guidcmp(mediatype, mediatype_mpeg2_pes) &&
               !ff_guidcmp(subtype, mediasubtype_dvb_subtitle)) {
        st = new_stream(s, st, sid, AVMEDIA_TYPE_SUBTITLE);
        if (!st)
            return NULL;
        if (ff_guidcmp(formattype, format_none))
            av_log(s, AV_LOG_WARNING, "unknown formattype:"PRI_GUID"\n", ARG_GUID(formattype));
        url_fskip(pb, size);
        st->codec->codec_id = CODEC_ID_DVB_SUBTITLE;
        return st;
    } else if (!ff_guidcmp(mediatype, mediatype_mstvcaption) &&
               (!ff_guidcmp(subtype, mediasubtype_teletext) || !ff_guidcmp(subtype, mediasubtype_dtvccdata))) {
        st = new_stream(s, st, sid, AVMEDIA_TYPE_SUBTITLE);
        if (!st)
            return NULL;
        if (ff_guidcmp(formattype, format_none))
            av_log(s, AV_LOG_WARNING, "unknown formattype:"PRI_GUID"\n", ARG_GUID(formattype));
        url_fskip(pb, size);
        st->codec->codec_id   = CODEC_ID_DVB_TELETEXT;
        return st;
    } else if (!ff_guidcmp(mediatype, mediatype_mpeg2_sections) &&
               !ff_guidcmp(subtype, mediasubtype_mpeg2_sections)) {
        if (ff_guidcmp(formattype, format_none))
            av_log(s, AV_LOG_WARNING, "unknown formattype:"PRI_GUID"\n", ARG_GUID(formattype));
        url_fskip(pb, size);
        return NULL;
    }

    av_log(s, AV_LOG_WARNING, "unknown media type, mediatype:"PRI_GUID
                              ", subtype:"PRI_GUID", formattype:"PRI_GUID"\n",
                              ARG_GUID(mediatype), ARG_GUID(subtype), ARG_GUID(formattype));
    url_fskip(pb, size);
    return NULL;
}

enum {
    SEEK_TO_DATA = 0,
    SEEK_TO_BYTE,
    SEEK_TO_PTS,
};

/**
 * Parse WTV chunks
 * @param mode SEEK_TO_DATA, SEEK_TO_BYTE, SEEK_TO_PTS
 * @param seekts either byte position or timestamp
 * @param[out] len Length of data chunk
 * @return stream index of data chunk, or <0 on error
 */
static int parse_chunks(AVFormatContext *s, int mode, int64_t seekts, int *len_ptr)
{
    WtvContext *wtv = s->priv_data;
    ByteIOContext *pb = s->pb;
    while (!url_feof(pb)) {
        ff_asf_guid g;
        int len, sid, consumed;

        if (mode == SEEK_TO_BYTE && url_ftell(pb) >= seekts)
            return 0;

        ff_get_guid(pb, &g);
        if (is_zero(g, sizeof(ff_asf_guid)))
            return AVERROR_EOF;

        len = get_le32(pb);
        if (len < 32)
            break;
        sid = get_le32(pb) & 0x7FFF;
        url_fskip(pb, 8);
        consumed = 32;

        if (!ff_guidcmp(g, stream_guid)) {
            if (ff_find_stream_index(s, sid) < 0) {
                ff_asf_guid mediatype, subtype, formattype;
                int size;
                consumed += 20;
                url_fskip(pb, 16);
                if (get_le32(pb)) {
                    url_fskip(pb, 8);
                    ff_get_guid(pb, &mediatype);
                    ff_get_guid(pb, &subtype);
                    url_fskip(pb, 12);
                    ff_get_guid(pb, &formattype);
                    size = get_le32(pb);
                    parse_media_type(s, 0, sid, mediatype, subtype, formattype, size);
                    consumed += 72 + size;
                }
            }
        } else if (!ff_guidcmp(g, stream2_guid)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (stream_index >= 0 && !((WtvStream*)s->streams[stream_index]->priv_data)->seen_data) {
                ff_asf_guid mediatype, subtype, formattype;
                int size;
                url_fskip(pb, 12);
                ff_get_guid(pb, &mediatype);
                ff_get_guid(pb, &subtype);
                url_fskip(pb, 12);
                ff_get_guid(pb, &formattype);
                size = get_le32(pb);
                parse_media_type(s, s->streams[stream_index], sid, mediatype, subtype, formattype, size);
                consumed += 76 + size;
            }
        } else if (!ff_guidcmp(g, EVENTID_AudioDescriptorSpanningEvent) ||
                   !ff_guidcmp(g, EVENTID_CtxADescriptorSpanningEvent) ||
                   !ff_guidcmp(g, EVENTID_CSDescriptorSpanningEvent) ||
                   !ff_guidcmp(g, EVENTID_StreamIDSpanningEvent) ||
                   !ff_guidcmp(g, EVENTID_SubtitleSpanningEvent) ||
                   !ff_guidcmp(g, EVENTID_TeletextSpanningEvent)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (stream_index >= 0) {
                AVStream *st = s->streams[stream_index];
                uint8_t buf[258];
                const uint8_t *pbuf = buf;
                int buf_size;

                url_fskip(pb, 8);
                consumed += 8;
                if (!ff_guidcmp(g, EVENTID_CtxADescriptorSpanningEvent) ||
                    !ff_guidcmp(g, EVENTID_CSDescriptorSpanningEvent)) {
                    url_fskip(pb, 6);
                    consumed += 6;
                }

                buf_size = FFMIN(len - consumed, sizeof(buf));
                get_buffer(pb, buf, buf_size);
                consumed += buf_size;
                ff_parse_mpeg2_descriptor(s, st, 0, &pbuf, buf + buf_size, 0, 0, 0, 0);
            }
        } else if (!ff_guidcmp(g, EVENTID_DVBScramblingControlSpanningEvent)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (stream_index >= 0) {
                url_fskip(pb, 12);
                if (get_le32(pb))
                    av_log(s, AV_LOG_WARNING, "DVB scrambled stream detected (st:%d), decoding will likely fail\n", stream_index);
                consumed += 16;
            }
        } else if (!ff_guidcmp(g, EVENTID_LanguageSpanningEvent)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (stream_index >= 0) {
                AVStream *st = s->streams[stream_index];
                uint8_t language[4];
                url_fskip(pb, 12);
                get_buffer(pb, language, 3);
                if (language[0]) {
                    language[3] = 0;
                    av_metadata_set2(&st->metadata, "language", language, 0);
                }
                consumed += 15;
            }
        } else if (!ff_guidcmp(g, timestamp_guid)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (stream_index >= 0) {
                url_fskip(pb, 8);
                wtv->pts = get_le64(pb);
                consumed += 16;
                if (wtv->pts == -1)
                    wtv->pts = AV_NOPTS_VALUE;
                if (mode == SEEK_TO_PTS && wtv->pts >= seekts) {
#define WTV_PAD8(x) (((x) + 7) & ~7)
                    url_fskip(pb, WTV_PAD8(len) - consumed);
                    return 0;
                }
            }
        } else if (!ff_guidcmp(g, data_guid)) {
            int stream_index = ff_find_stream_index(s, sid);
            if (mode == SEEK_TO_DATA && stream_index >= 0) {
                WtvStream *wst = s->streams[stream_index]->priv_data;
                wst->seen_data = 1;
                if (len_ptr) {
                    *len_ptr = len;
                }
                if (wtv->pts != AV_NOPTS_VALUE)
                    av_add_index_entry(s->streams[stream_index], url_ftell(pb) - consumed, wtv->pts, 0, 0, AVINDEX_KEYFRAME);
                return stream_index;
            }
        } else if (
            !ff_guidcmp(g, /* DSATTRIB_CAPTURE_STREAMTIME */ (const ff_asf_guid){0x14,0x56,0x1A,0x0C,0xCD,0x30,0x40,0x4F,0xBC,0xBF,0xD0,0x3E,0x52,0x30,0x62,0x07}) ||
            !ff_guidcmp(g, /* DSATTRIB_PicSampleSeq */ (const ff_asf_guid){0x02,0xAE,0x5B,0x2F,0x8F,0x7B,0x60,0x4F,0x82,0xD6,0xE4,0xEA,0x2F,0x1F,0x4C,0x99}) ||
            !ff_guidcmp(g, /* DSATTRIB_TRANSPORT_PROPERTIES */ (const ff_asf_guid){0x12,0xF6,0x22,0xB6,0xAD,0x47,0x71,0x46,0xAD,0x6C,0x05,0xA9,0x8E,0x65,0xDE,0x3A}) ||
            !ff_guidcmp(g, /* dvr_ms_vid_frame_rep_data */ (const ff_asf_guid){0xCC,0x32,0x64,0xDD,0x29,0xE2,0xDB,0x40,0x80,0xF6,0xD2,0x63,0x28,0xD2,0x76,0x1F}) ||
            !ff_guidcmp(g, /* EVENTID_AudioTypeSpanningEvent */ (const ff_asf_guid){0xBE,0xBF,0x1C,0x50,0x49,0xB8,0xCE,0x42,0x9B,0xE9,0x3D,0xB8,0x69,0xFB,0x82,0xB3}) ||
            !ff_guidcmp(g, /* EVENTID_ChannelChangeSpanningEvent */ (const ff_asf_guid){0xE5,0xC5,0x67,0x90,0x5C,0x4C,0x05,0x42,0x86,0xC8,0x7A,0xFE,0x20,0xFE,0x1E,0xFA}) ||
            !ff_guidcmp(g, /* EVENTID_ChannelInfoSpanningEvent */ (const ff_asf_guid){0x80,0x6D,0xF3,0x41,0x32,0x41,0xC2,0x4C,0xB1,0x21,0x01,0xA4,0x32,0x19,0xD8,0x1B}) ||
            !ff_guidcmp(g, /* EVENTID_ChannelTypeSpanningEvent */ (const ff_asf_guid){0x51,0x1D,0xAB,0x72,0xD2,0x87,0x9B,0x48,0xBA,0x11,0x0E,0x08,0xDC,0x21,0x02,0x43}) ||
            !ff_guidcmp(g, /* EVENTID_PIDListSpanningEvent */ (const ff_asf_guid){0x65,0x8F,0xFC,0x47,0xBB,0xE2,0x34,0x46,0x9C,0xEF,0xFD,0xBF,0xE6,0x26,0x1D,0x5C}) ||
            !ff_guidcmp(g, /* EVENTID_SignalAndServiceStatusSpanningEvent */ (const ff_asf_guid){0xCB,0xC5,0x68,0x80,0x04,0x3C,0x2B,0x49,0xB4,0x7D,0x03,0x08,0x82,0x0D,0xCE,0x51}) ||
            !ff_guidcmp(g, /* EVENTID_StreamTypeSpanningEvent */ (const ff_asf_guid){0xBC,0x2E,0xAF,0x82,0xA6,0x30,0x64,0x42,0xA8,0x0B,0xAD,0x2E,0x13,0x72,0xAC,0x60}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x1E,0xBE,0xC3,0xC5,0x43,0x92,0xDC,0x11,0x85,0xE5,0x00,0x12,0x3F,0x6F,0x73,0xB9}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x3B,0x86,0xA2,0xB1,0xEB,0x1E,0xC3,0x44,0x8C,0x88,0x1C,0xA3,0xFF,0xE3,0xE7,0x6A}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x4E,0x7F,0x4C,0x5B,0xC4,0xD0,0x38,0x4B,0xA8,0x3E,0x21,0x7F,0x7B,0xBF,0x52,0xE7}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x63,0x36,0xEB,0xFE,0xA1,0x7E,0xD9,0x11,0x83,0x08,0x00,0x07,0xE9,0x5E,0xAD,0x8D}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x70,0xE9,0xF1,0xF8,0x89,0xA4,0x4C,0x4D,0x83,0x73,0xB8,0x12,0xE0,0xD5,0xF8,0x1E}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x96,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0x97,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D}) ||
            !ff_guidcmp(g, (const ff_asf_guid){0xA1,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D})) {
            //ignore known guids
        } else
            av_log(s, AV_LOG_WARNING, "unsupported chunk:"PRI_GUID"\n", ARG_GUID(g));

        url_fskip(pb, WTV_PAD8(len) - consumed);
    }
    return AVERROR_EOF;
}

#define WTV_CHUNK_START 0x40000

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    int ret;

    url_fseek(pb, WTV_CHUNK_START, SEEK_SET);
    ret = parse_chunks(s, SEEK_TO_DATA, 0, 0);
    if (ret < 0)
        return ret;

    url_fseek(pb, -32, SEEK_CUR);
    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WtvContext *wtv = s->priv_data;
    ByteIOContext *pb = s->pb;
    int stream_index, len, ret;

    stream_index = parse_chunks(s, SEEK_TO_DATA, 0, &len);
    if (stream_index < 0)
        return stream_index;

    ret = av_get_packet(pb, pkt, len - 32);
    if (ret < 0)
        return ret;
    pkt->stream_index = stream_index;
    pkt->pts          = wtv->pts;
    url_fskip(pb, WTV_PAD8(len) - len);
    return 0;
}

static int read_seek2(AVFormatContext *s, int stream_index,
                      int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    WtvContext *wtv = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    int i;

    if (stream_index < 0) {
        stream_index = av_find_default_stream_index(s);
        if (stream_index < 0)
            return -1;
    }
    st = s->streams[stream_index];

    if ((flags & AVSEEK_FLAG_FRAME)) {
        return AVERROR_NOTSUPP;
    } else if ((flags & AVSEEK_FLAG_BYTE)) {
        if (ts < url_ftell(pb)) {
            for (i = st->nb_index_entries - 1; i >= 0; i--) {
                if (st->index_entries[i].pos <= ts) {
                    wtv->pts    = st->index_entries[i].timestamp;
                    url_fseek(pb, st->index_entries[i].pos, SEEK_SET);
                    break;
                }
            }
            if (i < 0) {
                wtv->pts = 0;
                url_fseek(pb, WTV_CHUNK_START, SEEK_SET);
            }
        }
        if (parse_chunks(s, SEEK_TO_BYTE, ts, 0) < 0)
            return AVERROR(ERANGE);
        return 0;
    } else {
        ts *= 10;
        i = av_index_search_timestamp(st, ts, flags);
        if (i < 0) {
            if (st->nb_index_entries > 0) {
                wtv->pts    = st->index_entries[st->nb_index_entries - 1].timestamp;
                url_fseek(pb, st->index_entries[st->nb_index_entries - 1].pos, SEEK_SET);
            } else {
                wtv->pts = 0;
                url_fseek(pb, WTV_CHUNK_START, SEEK_SET);
            }
            if (parse_chunks(s, SEEK_TO_PTS, ts, 0) < 0)
                return AVERROR(ERANGE);
            return 0;
        }
        wtv->pts    = st->index_entries[i].timestamp;
        url_fseek(pb, st->index_entries[i].pos, SEEK_SET);
        return 0;
    }
}

AVInputFormat wtv_demuxer = {
    .name           = "wtv",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Television (WTV)"),
    .priv_data_size = sizeof(WtvContext),
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_seek2     = read_seek2,
    .flags          = AVFMT_SHOW_IDS|AVFMT_TS_DISCONT,
};
