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
 * @brief MPEG-4 / RTP Code
 * @author Fabrice Bellard
 * @author Romain Degez
 */

#include "rtpdec_formats.h"
#include "internal.h"
#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavcodec/get_bits.h"

#define MAX_AAC_HBR_FRAME_SIZE 8191

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

    uint8_t buf[FFMAX(RTP_MAX_PACKET_LENGTH, MAX_AAC_HBR_FRAME_SIZE)];
    int buf_pos, buf_size;
    uint32_t timestamp;
};

typedef struct AttrNameMap {
    const char *str;
    uint16_t    type;
    uint32_t    offset;

    /** Range for integer values */
    struct Range {
        int min;
        int max;
    } range;
} AttrNameMap;

/* All known fmtp parameters and the corresponding RTPAttrTypeEnum */
#define ATTR_NAME_TYPE_INT 0
#define ATTR_NAME_TYPE_STR 1
static const AttrNameMap attr_names[] = {
    { "SizeLength",       ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, sizelength),
      {0, 32} }, // SizeLength number of bits used to encode AU-size integer value
    { "IndexLength",      ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, indexlength),
      {0, 32} }, // IndexLength number of bits used to encode AU-Index integer value
    { "IndexDeltaLength", ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, indexdeltalength),
      {0, 32} }, // IndexDeltaLength number of bits to encode AU-Index-delta integer value
    { "profile-level-id", ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, profile_level_id),
      {INT32_MIN, INT32_MAX} }, // It differs depending on StreamType
    { "StreamType",       ATTR_NAME_TYPE_INT,
      offsetof(PayloadContext, streamtype),
      {0x00, 0x3F} }, // Values from ISO/IEC 14496-1, 'StreamType Values' table
    { "mode",             ATTR_NAME_TYPE_STR,
      offsetof(PayloadContext, mode),
       {0} },
    { NULL, -1, -1, {0} },
};

static void close_context(PayloadContext *data)
{
    av_freep(&data->au_headers);
    av_freep(&data->mode);
}

static int parse_fmtp_config(AVCodecParameters *par, const char *value)
{
    /* decode the hexa encoded parameter */
    int len = ff_hex_to_data(NULL, value), ret;

    if ((ret = ff_alloc_extradata(par, len)) < 0)
        return ret;
    ff_hex_to_data(par->extradata, value);
    return 0;
}

static int rtp_parse_mp4_au(PayloadContext *data, const uint8_t *buf, int len)
{
    int au_headers_length, au_header_size, i;
    GetBitContext getbitcontext;
    int ret;

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

    ret = init_get_bits(&getbitcontext, buf, data->au_headers_length_bytes * 8);
    if (ret < 0)
        return ret;

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
        if (data->cur_au_index > data->nb_au_headers) {
            av_log(ctx, AV_LOG_ERROR, "Invalid parser state\n");
            return AVERROR_INVALIDDATA;
        }
        if (data->buf_size - data->buf_pos < data->au_headers[data->cur_au_index].size) {
            av_log(ctx, AV_LOG_ERROR, "Invalid AU size\n");
            return AVERROR_INVALIDDATA;
        }
        if ((ret = av_new_packet(pkt, data->au_headers[data->cur_au_index].size)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory\n");
            return ret;
        }
        memcpy(pkt->data, &data->buf[data->buf_pos], data->au_headers[data->cur_au_index].size);
        data->buf_pos += data->au_headers[data->cur_au_index].size;
        pkt->stream_index = st->index;
        data->cur_au_index++;

        if (data->cur_au_index == data->nb_au_headers) {
            data->buf_pos = 0;
            return 0;
        }

        return 1;
    }

    if (rtp_parse_mp4_au(data, buf, len)) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing AU headers\n");
        return -1;
    }

    buf += data->au_headers_length_bytes + 2;
    len -= data->au_headers_length_bytes + 2;
    if (data->nb_au_headers == 1 && len < data->au_headers[0].size) {
        /* Packet is fragmented */

        if (!data->buf_pos) {
            if (data->au_headers[0].size > MAX_AAC_HBR_FRAME_SIZE) {
                av_log(ctx, AV_LOG_ERROR, "Invalid AU size\n");
                return AVERROR_INVALIDDATA;
            }

            data->buf_size = data->au_headers[0].size;
            data->timestamp = *timestamp;
        }

        if (data->timestamp != *timestamp ||
            data->au_headers[0].size != data->buf_size ||
            data->buf_pos + len > MAX_AAC_HBR_FRAME_SIZE) {
            data->buf_pos = 0;
            data->buf_size = 0;
            av_log(ctx, AV_LOG_ERROR, "Invalid packet received\n");
            return AVERROR_INVALIDDATA;
        }

        memcpy(&data->buf[data->buf_pos], buf, len);
        data->buf_pos += len;

        if (!(flags & RTP_FLAG_MARKER))
            return AVERROR(EAGAIN);

        if (data->buf_pos != data->buf_size) {
            data->buf_pos = 0;
            av_log(ctx, AV_LOG_ERROR, "Missed some packets, discarding frame\n");
            return AVERROR_INVALIDDATA;
        }

        data->buf_pos = 0;
        ret = av_new_packet(pkt, data->buf_size);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory\n");
            return ret;
        }
        pkt->stream_index = st->index;

        memcpy(pkt->data, data->buf, data->buf_size);

        return 0;
    }

    if (len < data->au_headers[0].size) {
        av_log(ctx, AV_LOG_ERROR, "First AU larger than packet size\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = av_new_packet(pkt, data->au_headers[0].size)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Out of memory\n");
        return ret;
    }
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

static int parse_fmtp(AVFormatContext *s,
                      AVStream *stream, PayloadContext *data,
                      const char *attr, const char *value)
{
    AVCodecParameters *par = stream->codecpar;
    int res, i;

    if (!strcmp(attr, "config")) {
        res = parse_fmtp_config(par, value);

        if (res < 0)
            return res;
    }

    if (par->codec_id == AV_CODEC_ID_AAC) {
        /* Looking for a known attribute */
        for (i = 0; attr_names[i].str; ++i) {
            if (!av_strcasecmp(attr, attr_names[i].str)) {
                if (attr_names[i].type == ATTR_NAME_TYPE_INT) {
                    char *end_ptr = NULL;
                    long long int val = strtoll(value, &end_ptr, 10);
                    if (end_ptr == value || end_ptr[0] != '\0') {
                        av_log(s, AV_LOG_ERROR,
                               "The %s field value is not a valid number: %s\n",
                               attr, value);
                        return AVERROR_INVALIDDATA;
                    }
                    if (val < attr_names[i].range.min ||
                        val > attr_names[i].range.max) {
                        av_log(s, AV_LOG_ERROR,
                            "fmtp field %s should be in range [%d,%d] (provided value: %lld)",
                            attr, attr_names[i].range.min, attr_names[i].range.max, val);
                        return  AVERROR_INVALIDDATA;
                    }

                    *(int *)((char *)data+
                        attr_names[i].offset) = (int) val;
                } else if (attr_names[i].type == ATTR_NAME_TYPE_STR) {
                    char *val = av_strdup(value);
                    if (!val)
                        return AVERROR(ENOMEM);
                    *(char **)((char *)data+
                        attr_names[i].offset) = val;
                }
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
        return ff_parse_fmtp(s, s->streams[st_index], data, p, parse_fmtp);

    return 0;
}

const RTPDynamicProtocolHandler ff_mp4v_es_dynamic_handler = {
    .enc_name           = "MP4V-ES",
    .codec_type         = AVMEDIA_TYPE_VIDEO,
    .codec_id           = AV_CODEC_ID_MPEG4,
    .need_parsing       = AVSTREAM_PARSE_FULL,
    .priv_data_size     = sizeof(PayloadContext),
    .parse_sdp_a_line   = parse_sdp_line,
};

const RTPDynamicProtocolHandler ff_mpeg4_generic_dynamic_handler = {
    .enc_name           = "mpeg4-generic",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = AV_CODEC_ID_AAC,
    .priv_data_size     = sizeof(PayloadContext),
    .parse_sdp_a_line   = parse_sdp_line,
    .close              = close_context,
    .parse_packet       = aac_parse_packet,
};
