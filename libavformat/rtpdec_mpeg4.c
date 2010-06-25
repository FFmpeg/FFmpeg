/**
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

#include "rtpdec_mpeg4.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"
#include <strings.h>

#include "rtsp.h" //XXX remove this dependency

/* return the length and optionally the data */
static int hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v = 1;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;
        c = toupper((unsigned char) *p++);
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            break;
        v = (v << 4) | c;
        if (v & 0x100) {
            if (data)
                data[len] = v;
            len++;
            v = 1;
        }
    }
    return len;
}

typedef struct {
    const char *str;
    uint16_t    type;
    uint32_t    offset;
} AttrNameMap;

/* All known fmtp parameters and the corresponding RTPAttrTypeEnum */
#define ATTR_NAME_TYPE_INT 0
#define ATTR_NAME_TYPE_STR 1
static const AttrNameMap attr_names[]=
{
    { "SizeLength",       ATTR_NAME_TYPE_INT,
      offsetof(RTPPayloadData, sizelength) },
    { "IndexLength",      ATTR_NAME_TYPE_INT,
      offsetof(RTPPayloadData, indexlength) },
    { "IndexDeltaLength", ATTR_NAME_TYPE_INT,
      offsetof(RTPPayloadData, indexdeltalength) },
    { "profile-level-id", ATTR_NAME_TYPE_INT,
      offsetof(RTPPayloadData, profile_level_id) },
    { "StreamType",       ATTR_NAME_TYPE_INT,
      offsetof(RTPPayloadData, streamtype) },
    { "mode",             ATTR_NAME_TYPE_STR,
      offsetof(RTPPayloadData, mode) },
    { NULL, -1, -1 },
};

static int parse_fmtp_config(AVCodecContext * codec, char *value)
{
    /* decode the hexa encoded parameter */
    int len = hex_to_data(NULL, value);
    if (codec->extradata)
        av_free(codec->extradata);
    codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!codec->extradata)
        return AVERROR(ENOMEM);
    codec->extradata_size = len;
    hex_to_data(codec->extradata, value);
    return 0;
}

static int rtp_parse_mp4_au(RTSPStream *rtsp_st, const uint8_t *buf)
{
    int au_headers_length, au_header_size, i;
    GetBitContext getbitcontext;
    RTPPayloadData *infos;

    infos =  &rtsp_st->rtp_payload_data;
    if (infos == NULL)
        return -1;

    /* decode the first 2 bytes where the AUHeader sections are stored
       length in bits */
    au_headers_length = AV_RB16(buf);

    if (au_headers_length > RTP_MAX_PACKET_LENGTH)
      return -1;

    infos->au_headers_length_bytes = (au_headers_length + 7) / 8;

    /* skip AU headers length section (2 bytes) */
    buf += 2;

    init_get_bits(&getbitcontext, buf, infos->au_headers_length_bytes * 8);

    /* XXX: Wrong if optionnal additional sections are present (cts, dts etc...) */
    au_header_size = infos->sizelength + infos->indexlength;
    if (au_header_size <= 0 || (au_headers_length % au_header_size != 0))
        return -1;

    infos->nb_au_headers = au_headers_length / au_header_size;
    if (!infos->au_headers || infos->au_headers_allocated < infos->nb_au_headers) {
        av_free(infos->au_headers);
        infos->au_headers = av_malloc(sizeof(struct AUHeaders) * infos->nb_au_headers);
        infos->au_headers_allocated = infos->nb_au_headers;
    }

    /* XXX: We handle multiple AU Section as only one (need to fix this for interleaving)
       In my test, the FAAD decoder does not behave correctly when sending each AU one by one
       but does when sending the whole as one big packet...  */
    infos->au_headers[0].size = 0;
    infos->au_headers[0].index = 0;
    for (i = 0; i < infos->nb_au_headers; ++i) {
        infos->au_headers[0].size += get_bits_long(&getbitcontext, infos->sizelength);
        infos->au_headers[0].index = get_bits_long(&getbitcontext, infos->indexlength);
    }

    infos->nb_au_headers = 1;

    return 0;
}


/* Follows RFC 3640 */
static int aac_parse_packet(AVFormatContext *ctx,
                            PayloadContext *data,
                            AVStream *st,
                            AVPacket *pkt,
                            uint32_t *timestamp,
                            const uint8_t *buf, int len, int flags)
{
    RTSPStream *rtsp_st = st->priv_data;
    RTPPayloadData *infos;

    if (rtp_parse_mp4_au(rtsp_st, buf))
        return -1;

    infos = &rtsp_st->rtp_payload_data;
    if (infos == NULL)
        return -1;
    buf += infos->au_headers_length_bytes + 2;
    len -= infos->au_headers_length_bytes + 2;

    /* XXX: Fixme we only handle the case where rtp_parse_mp4_au define
                    one au_header */
    av_new_packet(pkt, infos->au_headers[0].size);
    memcpy(pkt->data, buf, infos->au_headers[0].size);

    pkt->stream_index = st->index;
    return 0;
}

static int parse_sdp_line(AVFormatContext *s, int st_index,
                          PayloadContext *data, const char *line)
{
    const char *p;
    char value[4096], attr[25];
    int res = 0, i;
    AVStream *st = s->streams[st_index];
    RTSPStream *rtsp_st = st->priv_data;
    AVCodecContext* codec = st->codec;
    RTPPayloadData *rtp_payload_data = &rtsp_st->rtp_payload_data;

    if (av_strstart(line, "fmtp:", &p)) {
        // remove protocol identifier
        while (*p && *p == ' ') p++; // strip spaces
        while (*p && *p != ' ') p++; // eat protocol identifier
        while (*p && *p == ' ') p++; // strip trailing spaces

        while (ff_rtsp_next_attr_and_value(&p,
                                           attr, sizeof(attr),
                                           value, sizeof(value))) {
            if (!strcmp(attr, "config")) {
                res = parse_fmtp_config(codec, value);

                if (res < 0)
                    return res;
            }

            if (codec->codec_id == CODEC_ID_AAC) {
                /* Looking for a known attribute */
                for (i = 0; attr_names[i].str; ++i) {
                    if (!strcasecmp(attr, attr_names[i].str)) {
                        if (attr_names[i].type == ATTR_NAME_TYPE_INT) {
                            *(int *)((char *)rtp_payload_data +
                                attr_names[i].offset) = atoi(value);
                        } else if (attr_names[i].type == ATTR_NAME_TYPE_STR)
                            *(char **)((char *)rtp_payload_data +
                                attr_names[i].offset) = av_strdup(value);
                    }
                }
            }
        }
    }

    return 0;

}

RTPDynamicProtocolHandler ff_mp4v_es_dynamic_handler = {
    .enc_name           = "MP4V-ES",
    .codec_type         = AVMEDIA_TYPE_VIDEO,
    .codec_id           = CODEC_ID_MPEG4,
    .parse_sdp_a_line   = parse_sdp_line,
    .open               = NULL,
    .close              = NULL,
    .parse_packet       = NULL
};

RTPDynamicProtocolHandler ff_mpeg4_generic_dynamic_handler = {
    .enc_name           = "mpeg4-generic",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = CODEC_ID_AAC,
    .parse_sdp_a_line   = parse_sdp_line,
    .open               = NULL,
    .close              = NULL,
    .parse_packet       = aac_parse_packet
};
