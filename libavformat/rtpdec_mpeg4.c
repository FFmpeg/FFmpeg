/*
 * Common code for the RTP depacketization of MPEG-4 formats.
 * Copyright (c) 2010 Fabrice Bellard
 *                    Romain Degez
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
 * @brief MPEG4 / RTP Code
 * @author Fabrice Bellard
 * @author Romain Degez
 */

#include "rtpdec_formats.h"
#include "internal.h"
#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"

/** Structure listing useful vars to parse RTP packet payload */
struct PayloadContext {
    int sizelength;
    int indexlength;
    int indexdeltalength;
    int profile_level_id;
    int streamtype;
    int objecttype;
    char *mode;

    /** mpeg 4 AU headers */
    struct AUHeaders {
        int size;
        int index;
        int cts_flag;
        int cts;
        int dts_flag;
        int dts;
        int rap_flag;
        int streamstate;
    } *au_headers;
    int au_headers_allocated;
    int nb_au_headers;
    int au_headers_length_bytes;
    int cur_au_index;

    uint8_t buf[RTP_MAX_PACKET_LENGTH];
    int buf_pos, buf_size;
};

typedef struct {
    const char *str;
    uint16_t    type;
    uint32_t    offset;
} AttrNameMap;

/* All known fmtp parameters and the corresponding RTPAttrTypeEnum */
#define ATTR_NAME_TYPE_INT 0
#define ATTR_NAME_TYPE_STR 1
static const AttrNameMap attr_names[] = {
    { "SizeLength",       ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, sizelength) },
    { "IndexLength",      ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, indexlength) },
    { "IndexDeltaLength", ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, indexdeltalength) },
    { "profile-level-id", ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, profile_level_id) },
    { "StreamType",       ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, streamtype) },
    { "mode",             ATTR_NAME_TYPE_STR,
      offsetof(PayloadContext, mode) },
    { NULL, -1, -1 },
};

static PayloadContext *new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void free_context(PayloadContext *data)
{
    av_free(data->au_headers);
    av_free(data->mode);
    av_free(data);
}

static int parse_fmtp_config(AVCodecContext *codec, char *value)
{
    /* decode the hexa encoded parameter */
    int len = ff_hex_to_data(NULL, value);
    av_free(codec->extradata);
    codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!codec->extradata)
        return AVERROR(ENOMEM);
    codec->extradata_size = len;
    ff_hex_to_data(codec->extradata, value);
    return 0;
}

static int rtp_parse_mp4_au(PayloadContext *data, const uint8_t *buf, int len)
{
    int au_headers_length, au_header_size, i;
    GetBitContext getbitcontext;

    if (len < 2)
        return AVERROR_INVALIDDATA;

    /* decode the first 2 bytes where the AUHeader sections are stored
       length in bits */
    au_headers_length = AV_RB16(buf);

    if (au_headers_length > RTP_MAX_PACKET_LENGTH)
      return -1;

    data->au_headers_length_bytes = (au_headers_length + 7) / 8;

    /* skip AU headers length section (2 bytes) */
    buf += 2;
    len -= 2;

    if (len < data->au_headers_length_bytes)
        return AVERROR_INVALIDDATA;

    init_get_bits(&getbitcontext, buf, data->au_headers_length_bytes * 8);

    /* XXX: Wrong if optional additional sections are present (cts, dts etc...) */
    au_header_size = data->sizelength + data->indexlength;
    if (au_header_size <= 0 || (au_headers_length % au_header_size != 0))
        return -1;

    data->nb_au_headers = au_headers_length / au_header_size;
    if (!data->au_headers || data->au_headers_allocated < data->nb_au_headers) {
        av_free(data->au_headers);
        data->au_headers = av_malloc(sizeof(struct AUHeaders) * data->nb_au_headers);
        if (!data->au_headers)
            return AVERROR(ENOMEM);
        data->au_headers_allocated = data->nb_au_headers;
    }

    for (i = 0; i < data->nb_au_headers; ++i) {
        data->au_headers[i].size  = get_bits_long(&getbitcontext, data->sizelength);
        data->au_headers[i].index = get_bits_long(&getbitcontext, data->indexlength);
    }

    return 0;
}


/* Follows RFC 3640 */
static int aac_parse_packet(AVFormatContext *ctx, PayloadContext *data,
                            AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                            const uint8_t *buf, int len, uint16_t seq,
                            int flags)
{
    int ret;

    if (!buf) {
        if (data->cur_au_index > data->nb_au_headers)
            return AVERROR_INVALIDDATA;
        if (data->buf_size - data->buf_pos < data->au_headers[data->cur_au_index].size)
            return AVERROR_INVALIDDATA;
        if ((ret = av_new_packet(pkt, data->au_headers[data->cur_au_index].size)) < 0)
            return ret;
        memcpy(pkt->data, &data->buf[data->buf_pos], data->au_headers[data->cur_au_index].size);
        data->buf_pos += data->au_headers[data->cur_au_index].size;
        pkt->stream_index = st->index;
        data->cur_au_index++;
        return data->cur_au_index < data->nb_au_headers;
    }

    if (rtp_parse_mp4_au(data, buf, len))
        return -1;

    buf += data->au_headers_length_bytes + 2;
    len -= data->au_headers_length_bytes + 2;

    if (len < data->au_headers[0].size)
        return AVERROR_INVALIDDATA;
    if ((ret = av_new_packet(pkt, data->au_headers[0].size)) < 0)
        return ret;
    memcpy(pkt->data, buf, data->au_headers[0].size);
    len -= data->au_headers[0].size;
    buf += data->au_headers[0].size;
    pkt->stream_index = st->index;

    if (len > 0 && data->nb_au_headers > 1) {
        data->buf_size = FFMIN(len, sizeof(data->buf));
        memcpy(data->buf, buf, data->buf_size);
        data->cur_au_index = 1;
        data->buf_pos = 0;
        return 1;
    }

    return 0;
}

static int parse_fmtp(AVStream *stream, PayloadContext *data,
                      char *attr, char *value)
{
    AVCodecContext *codec = stream->codec;
    int res, i;

    if (!strcmp(attr, "config")) {
        res = parse_fmtp_config(codec, value);

        if (res < 0)
            return res;
    }

    if (codec->codec_id == AV_CODEC_ID_AAC) {
        /* Looking for a known attribute */
        for (i = 0; attr_names[i].str; ++i) {
            if (!av_strcasecmp(attr, attr_names[i].str)) {
                if (attr_names[i].type == ATTR_NAME_TYPE_INT) {
                    *(int *)((char *)data+
                        attr_names[i].offset) = atoi(value);
                } else if (attr_names[i].type == ATTR_NAME_TYPE_STR)
                    *(char **)((char *)data+
                        attr_names[i].offset) = av_strdup(value);
            }
        }
    }
    return 0;
}

static int parse_sdp_line(AVFormatContext *s, int st_index,
                          PayloadContext *data, const char *line)
{
    const char *p;

    if (st_index < 0)
        return 0;

    if (av_strstart(line, "fmtp:", &p))
        return ff_parse_fmtp(s->streams[st_index], data, p, parse_fmtp);

    return 0;
}

static av_cold int init_video(AVFormatContext *s, int st_index,
                              PayloadContext *data)
{
    if (st_index < 0)
        return 0;
    s->streams[st_index]->need_parsing = AVSTREAM_PARSE_FULL;
    return 0;
}

RTPDynamicProtocolHandler ff_mp4v_es_dynamic_handler = {
    .enc_name           = "MP4V-ES",
    .codec_type         = AVMEDIA_TYPE_VIDEO,
    .codec_id           = AV_CODEC_ID_MPEG4,
    .init               = init_video,
    .parse_sdp_a_line   = parse_sdp_line,
};

RTPDynamicProtocolHandler ff_mpeg4_generic_dynamic_handler = {
    .enc_name           = "mpeg4-generic",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = AV_CODEC_ID_AAC,
    .parse_sdp_a_line   = parse_sdp_line,
    .alloc              = new_context,
    .free               = free_context,
    .parse_packet       = aac_parse_packet
};
