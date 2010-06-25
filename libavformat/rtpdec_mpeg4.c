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
    .parse_packet       = NULL
};
