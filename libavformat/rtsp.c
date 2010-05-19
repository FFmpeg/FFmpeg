/*
 * RTSP/SDP client
 * Copyright (c) 2002 Fabrice Bellard
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

#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"

#include <sys/time.h>
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <strings.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "rtsp.h"

#include "rtpdec.h"
#include "rdt.h"
#include "rtpdec_asf.h"

//#define DEBUG
//#define DEBUG_RTP_TCP

#if LIBAVFORMAT_VERSION_INT < (53 << 16)
int rtsp_default_protocols = (1 << RTSP_LOWER_TRANSPORT_UDP);
#endif

/* Timeout values for socket select, in ms,
 * and read_packet(), in seconds  */
#define SELECT_TIMEOUT_MS 100
#define READ_PACKET_TIMEOUT_S 10
#define MAX_TIMEOUTS READ_PACKET_TIMEOUT_S * 1000 / SELECT_TIMEOUT_MS

#define SPACE_CHARS " \t\r\n"
/* we use memchr() instead of strchr() here because strchr() will return
 * the terminating '\0' of SPACE_CHARS instead of NULL if c is '\0'. */
#define redir_isspace(c) memchr(SPACE_CHARS, c, 4)
static void skip_spaces(const char **pp)
{
    const char *p;
    p = *pp;
    while (redir_isspace(*p))
        p++;
    *pp = p;
}

static void get_word_until_chars(char *buf, int buf_size,
                                 const char *sep, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    skip_spaces(&p);
    q = buf;
    while (!strchr(sep, *p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (buf_size > 0)
        *q = '\0';
    *pp = p;
}

static void get_word_sep(char *buf, int buf_size, const char *sep,
                         const char **pp)
{
    if (**pp == '/') (*pp)++;
    get_word_until_chars(buf, buf_size, sep, pp);
}

static void get_word(char *buf, int buf_size, const char **pp)
{
    get_word_until_chars(buf, buf_size, SPACE_CHARS, pp);
}

/* parse the rtpmap description: <codec_name>/<clock_rate>[/<other params>] */
static int sdp_parse_rtpmap(AVFormatContext *s,
                            AVCodecContext *codec, RTSPStream *rtsp_st,
                            int payload_type, const char *p)
{
    char buf[256];
    int i;
    AVCodec *c;
    const char *c_name;

    /* Loop into AVRtpDynamicPayloadTypes[] and AVRtpPayloadTypes[] and
     * see if we can handle this kind of payload.
     * The space should normally not be there but some Real streams or
     * particular servers ("RealServer Version 6.1.3.970", see issue 1658)
     * have a trailing space. */
    get_word_sep(buf, sizeof(buf), "/ ", &p);
    if (payload_type >= RTP_PT_PRIVATE) {
        RTPDynamicProtocolHandler *handler;
        for (handler = RTPFirstDynamicPayloadHandler;
             handler; handler = handler->next) {
            if (!strcasecmp(buf, handler->enc_name) &&
                codec->codec_type == handler->codec_type) {
                codec->codec_id          = handler->codec_id;
                rtsp_st->dynamic_handler = handler;
                if (handler->open)
                    rtsp_st->dynamic_protocol_context = handler->open();
                break;
            }
        }
    } else {
        /* We are in a standard case
         * (from http://www.iana.org/assignments/rtp-parameters). */
        /* search into AVRtpPayloadTypes[] */
        codec->codec_id = ff_rtp_codec_id(buf, codec->codec_type);
    }

    c = avcodec_find_decoder(codec->codec_id);
    if (c && c->name)
        c_name = c->name;
    else
        c_name = "(null)";

    get_word_sep(buf, sizeof(buf), "/", &p);
    i = atoi(buf);
    switch (codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_log(s, AV_LOG_DEBUG, "audio codec set to: %s\n", c_name);
        codec->sample_rate = RTSP_DEFAULT_AUDIO_SAMPLERATE;
        codec->channels = RTSP_DEFAULT_NB_AUDIO_CHANNELS;
        if (i > 0) {
            codec->sample_rate = i;
            get_word_sep(buf, sizeof(buf), "/", &p);
            i = atoi(buf);
            if (i > 0)
                codec->channels = i;
            // TODO: there is a bug here; if it is a mono stream, and
            // less than 22000Hz, faad upconverts to stereo and twice
            // the frequency.  No problem, but the sample rate is being
            // set here by the sdp line. Patch on its way. (rdm)
        }
        av_log(s, AV_LOG_DEBUG, "audio samplerate set to: %i\n",
               codec->sample_rate);
        av_log(s, AV_LOG_DEBUG, "audio channels set to: %i\n",
               codec->channels);
        break;
    case AVMEDIA_TYPE_VIDEO:
        av_log(s, AV_LOG_DEBUG, "video codec set to: %s\n", c_name);
        break;
    default:
        break;
    }
    return 0;
}

/* return the length and optionally the data */
static int hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v = 1;
    for (;;) {
        skip_spaces(&p);
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

static void sdp_parse_fmtp_config(AVCodecContext * codec, void *ctx,
                                  char *attr, char *value)
{
    switch (codec->codec_id) {
    case CODEC_ID_MPEG4:
    case CODEC_ID_AAC:
        if (!strcmp(attr, "config")) {
            /* decode the hexa encoded parameter */
            int len = hex_to_data(NULL, value);
            if (codec->extradata)
                av_free(codec->extradata);
            codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!codec->extradata)
                return;
            codec->extradata_size = len;
            hex_to_data(codec->extradata, value);
        }
        break;
    default:
        break;
    }
    return;
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

/* parse the attribute line from the fmtp a line of an sdp response. This
 * is broken out as a function because it is used in rtp_h264.c, which is
 * forthcoming. */
int ff_rtsp_next_attr_and_value(const char **p, char *attr, int attr_size,
                                char *value, int value_size)
{
    skip_spaces(p);
    if (**p) {
        get_word_sep(attr, attr_size, "=", p);
        if (**p == '=')
            (*p)++;
        get_word_sep(value, value_size, ";", p);
        if (**p == ';')
            (*p)++;
        return 1;
    }
    return 0;
}

/* parse a SDP line and save stream attributes */
static void sdp_parse_fmtp(AVStream *st, const char *p)
{
    char attr[256];
    /* Vorbis setup headers can be up to 12KB and are sent base64
     * encoded, giving a 12KB * (4/3) = 16KB FMTP line. */
    char value[16384];
    int i;
    RTSPStream *rtsp_st = st->priv_data;
    AVCodecContext *codec = st->codec;
    RTPPayloadData *rtp_payload_data = &rtsp_st->rtp_payload_data;

    /* loop on each attribute */
    while (ff_rtsp_next_attr_and_value(&p, attr, sizeof(attr),
                                       value, sizeof(value))) {
        /* grab the codec extra_data from the config parameter of the fmtp
         * line */
        sdp_parse_fmtp_config(codec, rtsp_st->dynamic_protocol_context,
                              attr, value);
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

/** Parse a string p in the form of Range:npt=xx-xx, and determine the start
 *  and end time.
 *  Used for seeking in the rtp stream.
 */
static void rtsp_parse_range_npt(const char *p, int64_t *start, int64_t *end)
{
    char buf[256];

    skip_spaces(&p);
    if (!av_stristart(p, "npt=", &p))
        return;

    *start = AV_NOPTS_VALUE;
    *end = AV_NOPTS_VALUE;

    get_word_sep(buf, sizeof(buf), "-", &p);
    *start = parse_date(buf, 1);
    if (*p == '-') {
        p++;
        get_word_sep(buf, sizeof(buf), "-", &p);
        *end = parse_date(buf, 1);
    }
//    av_log(NULL, AV_LOG_DEBUG, "Range Start: %lld\n", *start);
//    av_log(NULL, AV_LOG_DEBUG, "Range End: %lld\n", *end);
}

typedef struct SDPParseState {
    /* SDP only */
    struct in_addr default_ip;
    int            default_ttl;
    int            skip_media;  ///< set if an unknown m= line occurs
} SDPParseState;

static void sdp_parse_line(AVFormatContext *s, SDPParseState *s1,
                           int letter, const char *buf)
{
    RTSPState *rt = s->priv_data;
    char buf1[64], st_type[64];
    const char *p;
    enum AVMediaType codec_type;
    int payload_type, i;
    AVStream *st;
    RTSPStream *rtsp_st;
    struct in_addr sdp_ip;
    int ttl;

    dprintf(s, "sdp: %c='%s'\n", letter, buf);

    p = buf;
    if (s1->skip_media && letter != 'm')
        return;
    switch (letter) {
    case 'c':
        get_word(buf1, sizeof(buf1), &p);
        if (strcmp(buf1, "IN") != 0)
            return;
        get_word(buf1, sizeof(buf1), &p);
        if (strcmp(buf1, "IP4") != 0)
            return;
        get_word_sep(buf1, sizeof(buf1), "/", &p);
        if (ff_inet_aton(buf1, &sdp_ip) == 0)
            return;
        ttl = 16;
        if (*p == '/') {
            p++;
            get_word_sep(buf1, sizeof(buf1), "/", &p);
            ttl = atoi(buf1);
        }
        if (s->nb_streams == 0) {
            s1->default_ip = sdp_ip;
            s1->default_ttl = ttl;
        } else {
            st = s->streams[s->nb_streams - 1];
            rtsp_st = st->priv_data;
            rtsp_st->sdp_ip = sdp_ip;
            rtsp_st->sdp_ttl = ttl;
        }
        break;
    case 's':
        av_metadata_set2(&s->metadata, "title", p, 0);
        break;
    case 'i':
        if (s->nb_streams == 0) {
            av_metadata_set2(&s->metadata, "comment", p, 0);
            break;
        }
        break;
    case 'm':
        /* new stream */
        s1->skip_media = 0;
        get_word(st_type, sizeof(st_type), &p);
        if (!strcmp(st_type, "audio")) {
            codec_type = AVMEDIA_TYPE_AUDIO;
        } else if (!strcmp(st_type, "video")) {
            codec_type = AVMEDIA_TYPE_VIDEO;
        } else if (!strcmp(st_type, "application")) {
            codec_type = AVMEDIA_TYPE_DATA;
        } else {
            s1->skip_media = 1;
            return;
        }
        rtsp_st = av_mallocz(sizeof(RTSPStream));
        if (!rtsp_st)
            return;
        rtsp_st->stream_index = -1;
        dynarray_add(&rt->rtsp_streams, &rt->nb_rtsp_streams, rtsp_st);

        rtsp_st->sdp_ip = s1->default_ip;
        rtsp_st->sdp_ttl = s1->default_ttl;

        get_word(buf1, sizeof(buf1), &p); /* port */
        rtsp_st->sdp_port = atoi(buf1);

        get_word(buf1, sizeof(buf1), &p); /* protocol (ignored) */

        /* XXX: handle list of formats */
        get_word(buf1, sizeof(buf1), &p); /* format list */
        rtsp_st->sdp_payload_type = atoi(buf1);

        if (!strcmp(ff_rtp_enc_name(rtsp_st->sdp_payload_type), "MP2T")) {
            /* no corresponding stream */
        } else {
            st = av_new_stream(s, 0);
            if (!st)
                return;
            st->priv_data = rtsp_st;
            rtsp_st->stream_index = st->index;
            st->codec->codec_type = codec_type;
            if (rtsp_st->sdp_payload_type < RTP_PT_PRIVATE) {
                /* if standard payload type, we can find the codec right now */
                ff_rtp_get_codec_info(st->codec, rtsp_st->sdp_payload_type);
            }
        }
        /* put a default control url */
        av_strlcpy(rtsp_st->control_url, rt->control_uri,
                   sizeof(rtsp_st->control_url));
        break;
    case 'a':
        if (av_strstart(p, "control:", &p)) {
            if (s->nb_streams == 0) {
                if (!strncmp(p, "rtsp://", 7))
                    av_strlcpy(rt->control_uri, p,
                               sizeof(rt->control_uri));
            } else {
            char proto[32];
            /* get the control url */
            st = s->streams[s->nb_streams - 1];
            rtsp_st = st->priv_data;

            /* XXX: may need to add full url resolution */
            ff_url_split(proto, sizeof(proto), NULL, 0, NULL, 0,
                         NULL, NULL, 0, p);
            if (proto[0] == '\0') {
                /* relative control URL */
                if (rtsp_st->control_url[strlen(rtsp_st->control_url)-1]!='/')
                av_strlcat(rtsp_st->control_url, "/",
                           sizeof(rtsp_st->control_url));
                av_strlcat(rtsp_st->control_url, p,
                           sizeof(rtsp_st->control_url));
            } else
                av_strlcpy(rtsp_st->control_url, p,
                           sizeof(rtsp_st->control_url));
            }
        } else if (av_strstart(p, "rtpmap:", &p) && s->nb_streams > 0) {
            /* NOTE: rtpmap is only supported AFTER the 'm=' tag */
            get_word(buf1, sizeof(buf1), &p);
            payload_type = atoi(buf1);
            st = s->streams[s->nb_streams - 1];
            rtsp_st = st->priv_data;
            sdp_parse_rtpmap(s, st->codec, rtsp_st, payload_type, p);
        } else if (av_strstart(p, "fmtp:", &p)) {
            /* NOTE: fmtp is only supported AFTER the 'a=rtpmap:xxx' tag */
            get_word(buf1, sizeof(buf1), &p);
            payload_type = atoi(buf1);
            for (i = 0; i < s->nb_streams; i++) {
                st      = s->streams[i];
                rtsp_st = st->priv_data;
                if (rtsp_st->sdp_payload_type == payload_type) {
                    if (!(rtsp_st->dynamic_handler &&
                          rtsp_st->dynamic_handler->parse_sdp_a_line &&
                          rtsp_st->dynamic_handler->parse_sdp_a_line(s,
                              i, rtsp_st->dynamic_protocol_context, buf)))
                        sdp_parse_fmtp(st, p);
                }
            }
        } else if (av_strstart(p, "framesize:", &p)) {
            // let dynamic protocol handlers have a stab at the line.
            get_word(buf1, sizeof(buf1), &p);
            payload_type = atoi(buf1);
            for (i = 0; i < s->nb_streams; i++) {
                st      = s->streams[i];
                rtsp_st = st->priv_data;
                if (rtsp_st->sdp_payload_type == payload_type &&
                    rtsp_st->dynamic_handler &&
                    rtsp_st->dynamic_handler->parse_sdp_a_line)
                    rtsp_st->dynamic_handler->parse_sdp_a_line(s, i,
                        rtsp_st->dynamic_protocol_context, buf);
            }
        } else if (av_strstart(p, "range:", &p)) {
            int64_t start, end;

            // this is so that seeking on a streamed file can work.
            rtsp_parse_range_npt(p, &start, &end);
            s->start_time = start;
            /* AV_NOPTS_VALUE means live broadcast (and can't seek) */
            s->duration   = (end == AV_NOPTS_VALUE) ?
                            AV_NOPTS_VALUE : end - start;
        } else if (av_strstart(p, "IsRealDataType:integer;",&p)) {
            if (atoi(p) == 1)
                rt->transport = RTSP_TRANSPORT_RDT;
        } else {
            if (rt->server_type == RTSP_SERVER_WMS)
                ff_wms_parse_sdp_a_line(s, p);
            if (s->nb_streams > 0) {
                if (rt->server_type == RTSP_SERVER_REAL)
                    ff_real_parse_sdp_a_line(s, s->nb_streams - 1, p);

                rtsp_st = s->streams[s->nb_streams - 1]->priv_data;
                if (rtsp_st->dynamic_handler &&
                    rtsp_st->dynamic_handler->parse_sdp_a_line)
                    rtsp_st->dynamic_handler->parse_sdp_a_line(s,
                        s->nb_streams - 1,
                        rtsp_st->dynamic_protocol_context, buf);
            }
        }
        break;
    }
}

static int sdp_parse(AVFormatContext *s, const char *content)
{
    const char *p;
    int letter;
    /* Some SDP lines, particularly for Realmedia or ASF RTSP streams,
     * contain long SDP lines containing complete ASF Headers (several
     * kB) or arrays of MDPR (RM stream descriptor) headers plus
     * "rulebooks" describing their properties. Therefore, the SDP line
     * buffer is large.
     *
     * The Vorbis FMTP line can be up to 16KB - see sdp_parse_fmtp. */
    char buf[16384], *q;
    SDPParseState sdp_parse_state, *s1 = &sdp_parse_state;

    memset(s1, 0, sizeof(SDPParseState));
    p = content;
    for (;;) {
        skip_spaces(&p);
        letter = *p;
        if (letter == '\0')
            break;
        p++;
        if (*p != '=')
            goto next_line;
        p++;
        /* get the content */
        q = buf;
        while (*p != '\n' && *p != '\r' && *p != '\0') {
            if ((q - buf) < sizeof(buf) - 1)
                *q++ = *p;
            p++;
        }
        *q = '\0';
        sdp_parse_line(s, s1, letter, buf);
    next_line:
        while (*p != '\n' && *p != '\0')
            p++;
        if (*p == '\n')
            p++;
    }
    return 0;
}

/* close and free RTSP streams */
void ff_rtsp_close_streams(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int i;
    RTSPStream *rtsp_st;

    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        rtsp_st = rt->rtsp_streams[i];
        if (rtsp_st) {
            if (rtsp_st->transport_priv) {
                if (s->oformat) {
                    AVFormatContext *rtpctx = rtsp_st->transport_priv;
                    av_write_trailer(rtpctx);
                    if (rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP) {
                        uint8_t *ptr;
                        url_close_dyn_buf(rtpctx->pb, &ptr);
                        av_free(ptr);
                    } else {
                        url_fclose(rtpctx->pb);
                    }
                    av_metadata_free(&rtpctx->streams[0]->metadata);
                    av_metadata_free(&rtpctx->metadata);
                    av_free(rtpctx->streams[0]);
                    av_free(rtpctx);
                } else if (rt->transport == RTSP_TRANSPORT_RDT)
                    ff_rdt_parse_close(rtsp_st->transport_priv);
                else
                    rtp_parse_close(rtsp_st->transport_priv);
            }
            if (rtsp_st->rtp_handle)
                url_close(rtsp_st->rtp_handle);
            if (rtsp_st->dynamic_handler && rtsp_st->dynamic_protocol_context)
                rtsp_st->dynamic_handler->close(
                    rtsp_st->dynamic_protocol_context);
        }
    }
    av_free(rt->rtsp_streams);
    if (rt->asf_ctx) {
        av_close_input_stream (rt->asf_ctx);
        rt->asf_ctx = NULL;
    }
}

static void *rtsp_rtp_mux_open(AVFormatContext *s, AVStream *st,
                               URLContext *handle)
{
    RTSPState *rt = s->priv_data;
    AVFormatContext *rtpctx;
    int ret;
    AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);

    if (!rtp_format)
        return NULL;

    /* Allocate an AVFormatContext for each output stream */
    rtpctx = avformat_alloc_context();
    if (!rtpctx)
        return NULL;

    rtpctx->oformat = rtp_format;
    if (!av_new_stream(rtpctx, 0)) {
        av_free(rtpctx);
        return NULL;
    }
    /* Copy the max delay setting; the rtp muxer reads this. */
    rtpctx->max_delay = s->max_delay;
    /* Copy other stream parameters. */
    rtpctx->streams[0]->sample_aspect_ratio = st->sample_aspect_ratio;

    /* Set the synchronized start time. */
    rtpctx->start_time_realtime = rt->start_time;

    /* Remove the local codec, link to the original codec
     * context instead, to give the rtp muxer access to
     * codec parameters. */
    av_free(rtpctx->streams[0]->codec);
    rtpctx->streams[0]->codec = st->codec;

    if (handle) {
        url_fdopen(&rtpctx->pb, handle);
    } else
        url_open_dyn_packet_buf(&rtpctx->pb, RTSP_TCP_MAX_PACKET_SIZE);
    ret = av_write_header(rtpctx);

    if (ret) {
        if (handle) {
            url_fclose(rtpctx->pb);
        } else {
            uint8_t *ptr;
            url_close_dyn_buf(rtpctx->pb, &ptr);
            av_free(ptr);
        }
        av_free(rtpctx->streams[0]);
        av_free(rtpctx);
        return NULL;
    }

    /* Copy the RTP AVStream timebase back to the original AVStream */
    st->time_base = rtpctx->streams[0]->time_base;
    return rtpctx;
}

static int rtsp_open_transport_ctx(AVFormatContext *s, RTSPStream *rtsp_st)
{
    RTSPState *rt = s->priv_data;
    AVStream *st = NULL;

    /* open the RTP context */
    if (rtsp_st->stream_index >= 0)
        st = s->streams[rtsp_st->stream_index];
    if (!st)
        s->ctx_flags |= AVFMTCTX_NOHEADER;

    if (s->oformat) {
        rtsp_st->transport_priv = rtsp_rtp_mux_open(s, st, rtsp_st->rtp_handle);
        /* Ownership of rtp_handle is passed to the rtp mux context */
        rtsp_st->rtp_handle = NULL;
    } else if (rt->transport == RTSP_TRANSPORT_RDT)
        rtsp_st->transport_priv = ff_rdt_parse_open(s, st->index,
                                            rtsp_st->dynamic_protocol_context,
                                            rtsp_st->dynamic_handler);
    else
        rtsp_st->transport_priv = rtp_parse_open(s, st, rtsp_st->rtp_handle,
                                         rtsp_st->sdp_payload_type,
                                         &rtsp_st->rtp_payload_data);

    if (!rtsp_st->transport_priv) {
         return AVERROR(ENOMEM);
    } else if (rt->transport != RTSP_TRANSPORT_RDT) {
        if (rtsp_st->dynamic_handler) {
            rtp_parse_set_dynamic_protocol(rtsp_st->transport_priv,
                                           rtsp_st->dynamic_protocol_context,
                                           rtsp_st->dynamic_handler);
        }
    }

    return 0;
}

#if CONFIG_RTSP_DEMUXER || CONFIG_RTSP_MUXER
static int rtsp_probe(AVProbeData *p)
{
    if (av_strstart(p->filename, "rtsp:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static void rtsp_parse_range(int *min_ptr, int *max_ptr, const char **pp)
{
    const char *p;
    int v;

    p = *pp;
    skip_spaces(&p);
    v = strtol(p, (char **)&p, 10);
    if (*p == '-') {
        p++;
        *min_ptr = v;
        v = strtol(p, (char **)&p, 10);
        *max_ptr = v;
    } else {
        *min_ptr = v;
        *max_ptr = v;
    }
    *pp = p;
}

/* XXX: only one transport specification is parsed */
static void rtsp_parse_transport(RTSPMessageHeader *reply, const char *p)
{
    char transport_protocol[16];
    char profile[16];
    char lower_transport[16];
    char parameter[16];
    RTSPTransportField *th;
    char buf[256];

    reply->nb_transports = 0;

    for (;;) {
        skip_spaces(&p);
        if (*p == '\0')
            break;

        th = &reply->transports[reply->nb_transports];

        get_word_sep(transport_protocol, sizeof(transport_protocol),
                     "/", &p);
        if (!strcasecmp (transport_protocol, "rtp")) {
            get_word_sep(profile, sizeof(profile), "/;,", &p);
            lower_transport[0] = '\0';
            /* rtp/avp/<protocol> */
            if (*p == '/') {
                get_word_sep(lower_transport, sizeof(lower_transport),
                             ";,", &p);
            }
            th->transport = RTSP_TRANSPORT_RTP;
        } else if (!strcasecmp (transport_protocol, "x-pn-tng") ||
                   !strcasecmp (transport_protocol, "x-real-rdt")) {
            /* x-pn-tng/<protocol> */
            get_word_sep(lower_transport, sizeof(lower_transport), "/;,", &p);
            profile[0] = '\0';
            th->transport = RTSP_TRANSPORT_RDT;
        }
        if (!strcasecmp(lower_transport, "TCP"))
            th->lower_transport = RTSP_LOWER_TRANSPORT_TCP;
        else
            th->lower_transport = RTSP_LOWER_TRANSPORT_UDP;

        if (*p == ';')
            p++;
        /* get each parameter */
        while (*p != '\0' && *p != ',') {
            get_word_sep(parameter, sizeof(parameter), "=;,", &p);
            if (!strcmp(parameter, "port")) {
                if (*p == '=') {
                    p++;
                    rtsp_parse_range(&th->port_min, &th->port_max, &p);
                }
            } else if (!strcmp(parameter, "client_port")) {
                if (*p == '=') {
                    p++;
                    rtsp_parse_range(&th->client_port_min,
                                     &th->client_port_max, &p);
                }
            } else if (!strcmp(parameter, "server_port")) {
                if (*p == '=') {
                    p++;
                    rtsp_parse_range(&th->server_port_min,
                                     &th->server_port_max, &p);
                }
            } else if (!strcmp(parameter, "interleaved")) {
                if (*p == '=') {
                    p++;
                    rtsp_parse_range(&th->interleaved_min,
                                     &th->interleaved_max, &p);
                }
            } else if (!strcmp(parameter, "multicast")) {
                if (th->lower_transport == RTSP_LOWER_TRANSPORT_UDP)
                    th->lower_transport = RTSP_LOWER_TRANSPORT_UDP_MULTICAST;
            } else if (!strcmp(parameter, "ttl")) {
                if (*p == '=') {
                    p++;
                    th->ttl = strtol(p, (char **)&p, 10);
                }
            } else if (!strcmp(parameter, "destination")) {
                struct in_addr ipaddr;

                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";,", &p);
                    if (ff_inet_aton(buf, &ipaddr))
                        th->destination = ntohl(ipaddr.s_addr);
                }
            }
            while (*p != ';' && *p != '\0' && *p != ',')
                p++;
            if (*p == ';')
                p++;
        }
        if (*p == ',')
            p++;

        reply->nb_transports++;
    }
}

void ff_rtsp_parse_line(RTSPMessageHeader *reply, const char *buf,
                        HTTPAuthState *auth_state)
{
    const char *p;

    /* NOTE: we do case independent match for broken servers */
    p = buf;
    if (av_stristart(p, "Session:", &p)) {
        int t;
        get_word_sep(reply->session_id, sizeof(reply->session_id), ";", &p);
        if (av_stristart(p, ";timeout=", &p) &&
            (t = strtol(p, NULL, 10)) > 0) {
            reply->timeout = t;
        }
    } else if (av_stristart(p, "Content-Length:", &p)) {
        reply->content_length = strtol(p, NULL, 10);
    } else if (av_stristart(p, "Transport:", &p)) {
        rtsp_parse_transport(reply, p);
    } else if (av_stristart(p, "CSeq:", &p)) {
        reply->seq = strtol(p, NULL, 10);
    } else if (av_stristart(p, "Range:", &p)) {
        rtsp_parse_range_npt(p, &reply->range_start, &reply->range_end);
    } else if (av_stristart(p, "RealChallenge1:", &p)) {
        skip_spaces(&p);
        av_strlcpy(reply->real_challenge, p, sizeof(reply->real_challenge));
    } else if (av_stristart(p, "Server:", &p)) {
        skip_spaces(&p);
        av_strlcpy(reply->server, p, sizeof(reply->server));
    } else if (av_stristart(p, "Notice:", &p) ||
               av_stristart(p, "X-Notice:", &p)) {
        reply->notice = strtol(p, NULL, 10);
    } else if (av_stristart(p, "Location:", &p)) {
        skip_spaces(&p);
        av_strlcpy(reply->location, p , sizeof(reply->location));
    } else if (av_stristart(p, "WWW-Authenticate:", &p) && auth_state) {
        skip_spaces(&p);
        ff_http_auth_handle_header(auth_state, "WWW-Authenticate", p);
    } else if (av_stristart(p, "Authentication-Info:", &p) && auth_state) {
        skip_spaces(&p);
        ff_http_auth_handle_header(auth_state, "Authentication-Info", p);
    }
}

/* skip a RTP/TCP interleaved packet */
void ff_rtsp_skip_packet(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int ret, len, len1;
    uint8_t buf[1024];

    ret = url_read_complete(rt->rtsp_hd, buf, 3);
    if (ret != 3)
        return;
    len = AV_RB16(buf + 1);

    dprintf(s, "skipping RTP packet len=%d\n", len);

    /* skip payload */
    while (len > 0) {
        len1 = len;
        if (len1 > sizeof(buf))
            len1 = sizeof(buf);
        ret = url_read_complete(rt->rtsp_hd, buf, len1);
        if (ret != len1)
            return;
        len -= len1;
    }
}

int ff_rtsp_read_reply(AVFormatContext *s, RTSPMessageHeader *reply,
                       unsigned char **content_ptr,
                       int return_on_interleaved_data)
{
    RTSPState *rt = s->priv_data;
    char buf[4096], buf1[1024], *q;
    unsigned char ch;
    const char *p;
    int ret, content_length, line_count = 0;
    unsigned char *content = NULL;

    memset(reply, 0, sizeof(*reply));

    /* parse reply (XXX: use buffers) */
    rt->last_reply[0] = '\0';
    for (;;) {
        q = buf;
        for (;;) {
            ret = url_read_complete(rt->rtsp_hd, &ch, 1);
#ifdef DEBUG_RTP_TCP
            dprintf(s, "ret=%d c=%02x [%c]\n", ret, ch, ch);
#endif
            if (ret != 1)
                return -1;
            if (ch == '\n')
                break;
            if (ch == '$') {
                /* XXX: only parse it if first char on line ? */
                if (return_on_interleaved_data) {
                    return 1;
                } else
                    ff_rtsp_skip_packet(s);
            } else if (ch != '\r') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = ch;
            }
        }
        *q = '\0';

        dprintf(s, "line='%s'\n", buf);

        /* test if last line */
        if (buf[0] == '\0')
            break;
        p = buf;
        if (line_count == 0) {
            /* get reply code */
            get_word(buf1, sizeof(buf1), &p);
            get_word(buf1, sizeof(buf1), &p);
            reply->status_code = atoi(buf1);
        } else {
            ff_rtsp_parse_line(reply, p, &rt->auth_state);
            av_strlcat(rt->last_reply, p,    sizeof(rt->last_reply));
            av_strlcat(rt->last_reply, "\n", sizeof(rt->last_reply));
        }
        line_count++;
    }

    if (rt->session_id[0] == '\0' && reply->session_id[0] != '\0')
        av_strlcpy(rt->session_id, reply->session_id, sizeof(rt->session_id));

    content_length = reply->content_length;
    if (content_length > 0) {
        /* leave some room for a trailing '\0' (useful for simple parsing) */
        content = av_malloc(content_length + 1);
        (void)url_read_complete(rt->rtsp_hd, content, content_length);
        content[content_length] = '\0';
    }
    if (content_ptr)
        *content_ptr = content;
    else
        av_free(content);

    if (rt->seq != reply->seq) {
        av_log(s, AV_LOG_WARNING, "CSeq %d expected, %d received.\n",
            rt->seq, reply->seq);
    }

    /* EOS */
    if (reply->notice == 2101 /* End-of-Stream Reached */      ||
        reply->notice == 2104 /* Start-of-Stream Reached */    ||
        reply->notice == 2306 /* Continuous Feed Terminated */) {
        rt->state = RTSP_STATE_IDLE;
    } else if (reply->notice >= 4400 && reply->notice < 5500) {
        return AVERROR(EIO); /* data or server error */
    } else if (reply->notice == 2401 /* Ticket Expired */ ||
             (reply->notice >= 5500 && reply->notice < 5600) /* end of term */ )
        return AVERROR(EPERM);

    return 0;
}

void ff_rtsp_send_cmd_with_content_async(AVFormatContext *s,
                                         const char *method, const char *url,
                                         const char *headers,
                                         const unsigned char *send_content,
                                         int send_content_length)
{
    RTSPState *rt = s->priv_data;
    char buf[4096];

    rt->seq++;
    snprintf(buf, sizeof(buf), "%s %s RTSP/1.0\r\n", method, url);
    if (headers)
        av_strlcat(buf, headers, sizeof(buf));
    av_strlcatf(buf, sizeof(buf), "CSeq: %d\r\n", rt->seq);
    if (rt->session_id[0] != '\0' && (!headers ||
        !strstr(headers, "\nIf-Match:"))) {
        av_strlcatf(buf, sizeof(buf), "Session: %s\r\n", rt->session_id);
    }
    if (rt->auth[0]) {
        char *str = ff_http_auth_create_response(&rt->auth_state,
                                                 rt->auth, url, method);
        if (str)
            av_strlcat(buf, str, sizeof(buf));
        av_free(str);
    }
    if (send_content_length > 0 && send_content)
        av_strlcatf(buf, sizeof(buf), "Content-Length: %d\r\n", send_content_length);
    av_strlcat(buf, "\r\n", sizeof(buf));

    dprintf(s, "Sending:\n%s--\n", buf);

    url_write(rt->rtsp_hd, buf, strlen(buf));
    if (send_content_length > 0 && send_content)
        url_write(rt->rtsp_hd, send_content, send_content_length);
    rt->last_cmd_time = av_gettime();
}

void ff_rtsp_send_cmd_async(AVFormatContext *s, const char *method,
                            const char *url, const char *headers)
{
    ff_rtsp_send_cmd_with_content_async(s, method, url, headers, NULL, 0);
}

void ff_rtsp_send_cmd(AVFormatContext *s, const char *method, const char *url,
                      const char *headers, RTSPMessageHeader *reply,
                      unsigned char **content_ptr)
{
    ff_rtsp_send_cmd_with_content(s, method, url, headers, reply,
                                  content_ptr, NULL, 0);
}

void ff_rtsp_send_cmd_with_content(AVFormatContext *s,
                                   const char *method, const char *url,
                                   const char *header,
                                   RTSPMessageHeader *reply,
                                   unsigned char **content_ptr,
                                   const unsigned char *send_content,
                                   int send_content_length)
{
    RTSPState *rt = s->priv_data;
    HTTPAuthType cur_auth_type;

retry:
    cur_auth_type = rt->auth_state.auth_type;
    ff_rtsp_send_cmd_with_content_async(s, method, url, header,
                                        send_content, send_content_length);

    ff_rtsp_read_reply(s, reply, content_ptr, 0);

    if (reply->status_code == 401 && cur_auth_type == HTTP_AUTH_NONE &&
        rt->auth_state.auth_type != HTTP_AUTH_NONE)
        goto retry;
}

/**
 * @return 0 on success, <0 on error, 1 if protocol is unavailable.
 */
static int make_setup_request(AVFormatContext *s, const char *host, int port,
                              int lower_transport, const char *real_challenge)
{
    RTSPState *rt = s->priv_data;
    int rtx, j, i, err, interleave = 0;
    RTSPStream *rtsp_st;
    RTSPMessageHeader reply1, *reply = &reply1;
    char cmd[2048];
    const char *trans_pref;

    if (rt->transport == RTSP_TRANSPORT_RDT)
        trans_pref = "x-pn-tng";
    else
        trans_pref = "RTP/AVP";

    /* default timeout: 1 minute */
    rt->timeout = 60;

    /* for each stream, make the setup request */
    /* XXX: we assume the same server is used for the control of each
     * RTSP stream */

    for (j = RTSP_RTP_PORT_MIN, i = 0; i < rt->nb_rtsp_streams; ++i) {
        char transport[2048];

        /**
         * WMS serves all UDP data over a single connection, the RTX, which
         * isn't necessarily the first in the SDP but has to be the first
         * to be set up, else the second/third SETUP will fail with a 461.
         */
        if (lower_transport == RTSP_LOWER_TRANSPORT_UDP &&
             rt->server_type == RTSP_SERVER_WMS) {
            if (i == 0) {
                /* rtx first */
                for (rtx = 0; rtx < rt->nb_rtsp_streams; rtx++) {
                    int len = strlen(rt->rtsp_streams[rtx]->control_url);
                    if (len >= 4 &&
                        !strcmp(rt->rtsp_streams[rtx]->control_url + len - 4,
                                "/rtx"))
                        break;
                }
                if (rtx == rt->nb_rtsp_streams)
                    return -1; /* no RTX found */
                rtsp_st = rt->rtsp_streams[rtx];
            } else
                rtsp_st = rt->rtsp_streams[i > rtx ? i : i - 1];
        } else
            rtsp_st = rt->rtsp_streams[i];

        /* RTP/UDP */
        if (lower_transport == RTSP_LOWER_TRANSPORT_UDP) {
            char buf[256];

            if (rt->server_type == RTSP_SERVER_WMS && i > 1) {
                port = reply->transports[0].client_port_min;
                goto have_port;
            }

            /* first try in specified port range */
            if (RTSP_RTP_PORT_MIN != 0) {
                while (j <= RTSP_RTP_PORT_MAX) {
                    ff_url_join(buf, sizeof(buf), "rtp", NULL, host, -1,
                                "?localport=%d", j);
                    /* we will use two ports per rtp stream (rtp and rtcp) */
                    j += 2;
                    if (url_open(&rtsp_st->rtp_handle, buf, URL_RDWR) == 0)
                        goto rtp_opened;
                }
            }

#if 0
            /* then try on any port */
            if (url_open(&rtsp_st->rtp_handle, "rtp://", URL_RDONLY) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
#endif

        rtp_opened:
            port = rtp_get_local_port(rtsp_st->rtp_handle);
        have_port:
            snprintf(transport, sizeof(transport) - 1,
                     "%s/UDP;", trans_pref);
            if (rt->server_type != RTSP_SERVER_REAL)
                av_strlcat(transport, "unicast;", sizeof(transport));
            av_strlcatf(transport, sizeof(transport),
                     "client_port=%d", port);
            if (rt->transport == RTSP_TRANSPORT_RTP &&
                !(rt->server_type == RTSP_SERVER_WMS && i > 0))
                av_strlcatf(transport, sizeof(transport), "-%d", port + 1);
        }

        /* RTP/TCP */
        else if (lower_transport == RTSP_LOWER_TRANSPORT_TCP) {
            /** For WMS streams, the application streams are only used for
             * UDP. When trying to set it up for TCP streams, the server
             * will return an error. Therefore, we skip those streams. */
            if (rt->server_type == RTSP_SERVER_WMS &&
                s->streams[rtsp_st->stream_index]->codec->codec_type ==
                    AVMEDIA_TYPE_DATA)
                continue;
            snprintf(transport, sizeof(transport) - 1,
                     "%s/TCP;", trans_pref);
            if (rt->server_type == RTSP_SERVER_WMS)
                av_strlcat(transport, "unicast;", sizeof(transport));
            av_strlcatf(transport, sizeof(transport),
                        "interleaved=%d-%d",
                        interleave, interleave + 1);
            interleave += 2;
        }

        else if (lower_transport == RTSP_LOWER_TRANSPORT_UDP_MULTICAST) {
            snprintf(transport, sizeof(transport) - 1,
                     "%s/UDP;multicast", trans_pref);
        }
        if (s->oformat) {
            av_strlcat(transport, ";mode=receive", sizeof(transport));
        } else if (rt->server_type == RTSP_SERVER_REAL ||
                   rt->server_type == RTSP_SERVER_WMS)
            av_strlcat(transport, ";mode=play", sizeof(transport));
        snprintf(cmd, sizeof(cmd),
                 "Transport: %s\r\n",
                 transport);
        if (i == 0 && rt->server_type == RTSP_SERVER_REAL) {
            char real_res[41], real_csum[9];
            ff_rdt_calc_response_and_checksum(real_res, real_csum,
                                              real_challenge);
            av_strlcatf(cmd, sizeof(cmd),
                        "If-Match: %s\r\n"
                        "RealChallenge2: %s, sd=%s\r\n",
                        rt->session_id, real_res, real_csum);
        }
        ff_rtsp_send_cmd(s, "SETUP", rtsp_st->control_url, cmd, reply, NULL);
        if (reply->status_code == 461 /* Unsupported protocol */ && i == 0) {
            err = 1;
            goto fail;
        } else if (reply->status_code != RTSP_STATUS_OK ||
                   reply->nb_transports != 1) {
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        /* XXX: same protocol for all streams is required */
        if (i > 0) {
            if (reply->transports[0].lower_transport != rt->lower_transport ||
                reply->transports[0].transport != rt->transport) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
        } else {
            rt->lower_transport = reply->transports[0].lower_transport;
            rt->transport = reply->transports[0].transport;
        }

        /* close RTP connection if not choosen */
        if (reply->transports[0].lower_transport != RTSP_LOWER_TRANSPORT_UDP &&
            (lower_transport == RTSP_LOWER_TRANSPORT_UDP)) {
            url_close(rtsp_st->rtp_handle);
            rtsp_st->rtp_handle = NULL;
        }

        switch(reply->transports[0].lower_transport) {
        case RTSP_LOWER_TRANSPORT_TCP:
            rtsp_st->interleaved_min = reply->transports[0].interleaved_min;
            rtsp_st->interleaved_max = reply->transports[0].interleaved_max;
            break;

        case RTSP_LOWER_TRANSPORT_UDP: {
            char url[1024];

            /* XXX: also use address if specified */
            ff_url_join(url, sizeof(url), "rtp", NULL, host,
                        reply->transports[0].server_port_min, NULL);
            if (!(rt->server_type == RTSP_SERVER_WMS && i > 1) &&
                rtp_set_remote_url(rtsp_st->rtp_handle, url) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            /* Try to initialize the connection state in a
             * potential NAT router by sending dummy packets.
             * RTP/RTCP dummy packets are used for RDT, too.
             */
            if (!(rt->server_type == RTSP_SERVER_WMS && i > 1) && s->iformat)
                rtp_send_punch_packets(rtsp_st->rtp_handle);
            break;
        }
        case RTSP_LOWER_TRANSPORT_UDP_MULTICAST: {
            char url[1024];
            struct in_addr in;
            int port, ttl;

            if (reply->transports[0].destination) {
                in.s_addr = htonl(reply->transports[0].destination);
                port      = reply->transports[0].port_min;
                ttl       = reply->transports[0].ttl;
            } else {
                in        = rtsp_st->sdp_ip;
                port      = rtsp_st->sdp_port;
                ttl       = rtsp_st->sdp_ttl;
            }
            ff_url_join(url, sizeof(url), "rtp", NULL, inet_ntoa(in),
                        port, "?ttl=%d", ttl);
            if (url_open(&rtsp_st->rtp_handle, url, URL_RDWR) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            break;
        }
        }

        if ((err = rtsp_open_transport_ctx(s, rtsp_st)))
            goto fail;
    }

    if (reply->timeout > 0)
        rt->timeout = reply->timeout;

    if (rt->server_type == RTSP_SERVER_REAL)
        rt->need_subscription = 1;

    return 0;

fail:
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        if (rt->rtsp_streams[i]->rtp_handle) {
            url_close(rt->rtsp_streams[i]->rtp_handle);
            rt->rtsp_streams[i]->rtp_handle = NULL;
        }
    }
    return err;
}

static int rtsp_read_play(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    int i;
    char cmd[1024];

    av_log(s, AV_LOG_DEBUG, "hello state=%d\n", rt->state);

    if (!(rt->server_type == RTSP_SERVER_REAL && rt->need_subscription)) {
        if (rt->state == RTSP_STATE_PAUSED) {
            cmd[0] = 0;
        } else {
            snprintf(cmd, sizeof(cmd),
                     "Range: npt=%0.3f-\r\n",
                     (double)rt->seek_timestamp / AV_TIME_BASE);
        }
        ff_rtsp_send_cmd(s, "PLAY", rt->control_uri, cmd, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            return -1;
        }
        if (reply->range_start != AV_NOPTS_VALUE &&
            rt->transport == RTSP_TRANSPORT_RTP) {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTSPStream *rtsp_st = rt->rtsp_streams[i];
                RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
                AVStream *st = NULL;
                if (!rtpctx)
                    continue;
                if (rtsp_st->stream_index >= 0)
                    st = s->streams[rtsp_st->stream_index];
                rtpctx->last_rtcp_ntp_time  = AV_NOPTS_VALUE;
                rtpctx->first_rtcp_ntp_time = AV_NOPTS_VALUE;
                if (st)
                    rtpctx->range_start_offset = av_rescale_q(reply->range_start,
                                                              AV_TIME_BASE_Q,
                                                              st->time_base);
            }
        }
    }
    rt->state = RTSP_STATE_STREAMING;
    return 0;
}

static int rtsp_setup_input_streams(AVFormatContext *s, RTSPMessageHeader *reply)
{
    RTSPState *rt = s->priv_data;
    char cmd[1024];
    unsigned char *content = NULL;
    int ret;

    /* describe the stream */
    snprintf(cmd, sizeof(cmd),
             "Accept: application/sdp\r\n");
    if (rt->server_type == RTSP_SERVER_REAL) {
        /**
         * The Require: attribute is needed for proper streaming from
         * Realmedia servers.
         */
        av_strlcat(cmd,
                   "Require: com.real.retain-entity-for-setup\r\n",
                   sizeof(cmd));
    }
    ff_rtsp_send_cmd(s, "DESCRIBE", rt->control_uri, cmd, reply, &content);
    if (!content)
        return AVERROR_INVALIDDATA;
    if (reply->status_code != RTSP_STATUS_OK) {
        av_freep(&content);
        return AVERROR_INVALIDDATA;
    }

    /* now we got the SDP description, we parse it */
    ret = sdp_parse(s, (const char *)content);
    av_freep(&content);
    if (ret < 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int rtsp_setup_output_streams(AVFormatContext *s, const char *addr)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    int i;
    char *sdp;
    AVFormatContext sdp_ctx, *ctx_array[1];

    rt->start_time = av_gettime();

    /* Announce the stream */
    sdp = av_mallocz(8192);
    if (sdp == NULL)
        return AVERROR(ENOMEM);
    /* We create the SDP based on the RTSP AVFormatContext where we
     * aren't allowed to change the filename field. (We create the SDP
     * based on the RTSP context since the contexts for the RTP streams
     * don't exist yet.) In order to specify a custom URL with the actual
     * peer IP instead of the originally specified hostname, we create
     * a temporary copy of the AVFormatContext, where the custom URL is set.
     *
     * FIXME: Create the SDP without copying the AVFormatContext.
     * This either requires setting up the RTP stream AVFormatContexts
     * already here (complicating things immensely) or getting a more
     * flexible SDP creation interface.
     */
    sdp_ctx = *s;
    ff_url_join(sdp_ctx.filename, sizeof(sdp_ctx.filename),
                "rtsp", NULL, addr, -1, NULL);
    ctx_array[0] = &sdp_ctx;
    if (avf_sdp_create(ctx_array, 1, sdp, 8192)) {
        av_free(sdp);
        return AVERROR_INVALIDDATA;
    }
    av_log(s, AV_LOG_INFO, "SDP:\n%s\n", sdp);
    ff_rtsp_send_cmd_with_content(s, "ANNOUNCE", rt->control_uri,
                                  "Content-Type: application/sdp\r\n",
                                  reply, NULL, sdp, strlen(sdp));
    av_free(sdp);
    if (reply->status_code != RTSP_STATUS_OK)
        return AVERROR_INVALIDDATA;

    /* Set up the RTSPStreams for each AVStream */
    for (i = 0; i < s->nb_streams; i++) {
        RTSPStream *rtsp_st;
        AVStream *st = s->streams[i];

        rtsp_st = av_mallocz(sizeof(RTSPStream));
        if (!rtsp_st)
            return AVERROR(ENOMEM);
        dynarray_add(&rt->rtsp_streams, &rt->nb_rtsp_streams, rtsp_st);

        st->priv_data = rtsp_st;
        rtsp_st->stream_index = i;

        av_strlcpy(rtsp_st->control_url, rt->control_uri, sizeof(rtsp_st->control_url));
        /* Note, this must match the relative uri set in the sdp content */
        av_strlcatf(rtsp_st->control_url, sizeof(rtsp_st->control_url),
                    "/streamid=%d", i);
    }

    return 0;
}

int ff_rtsp_connect(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    char host[1024], path[1024], tcpname[1024], cmd[2048], auth[128];
    char *option_list, *option, *filename;
    URLContext *rtsp_hd;
    int port, err, tcp_fd;
    RTSPMessageHeader reply1 = {}, *reply = &reply1;
    int lower_transport_mask = 0;
    char real_challenge[64];
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);

    if (!ff_network_init())
        return AVERROR(EIO);
redirect:
    /* extract hostname and port */
    ff_url_split(NULL, 0, auth, sizeof(auth),
                 host, sizeof(host), &port, path, sizeof(path), s->filename);
    if (*auth) {
        av_strlcpy(rt->auth, auth, sizeof(rt->auth));
    }
    if (port < 0)
        port = RTSP_DEFAULT_PORT;

    /* search for options */
    option_list = strrchr(path, '?');
    if (option_list) {
        /* Strip out the RTSP specific options, write out the rest of
         * the options back into the same string. */
        filename = option_list;
        while (option_list) {
            /* move the option pointer */
            option = ++option_list;
            option_list = strchr(option_list, '&');
            if (option_list)
                *option_list = 0;

            /* handle the options */
            if (!strcmp(option, "udp")) {
                lower_transport_mask |= (1<< RTSP_LOWER_TRANSPORT_UDP);
            } else if (!strcmp(option, "multicast")) {
                lower_transport_mask |= (1<< RTSP_LOWER_TRANSPORT_UDP_MULTICAST);
            } else if (!strcmp(option, "tcp")) {
                lower_transport_mask |= (1<< RTSP_LOWER_TRANSPORT_TCP);
            } else {
                /* Write options back into the buffer, using memmove instead
                 * of strcpy since the strings may overlap. */
                int len = strlen(option);
                memmove(++filename, option, len);
                filename += len;
                if (option_list) *filename = '&';
            }
        }
        *filename = 0;
    }

    if (!lower_transport_mask)
        lower_transport_mask = (1 << RTSP_LOWER_TRANSPORT_NB) - 1;

    if (s->oformat) {
        /* Only UDP or TCP - UDP multicast isn't supported. */
        lower_transport_mask &= (1 << RTSP_LOWER_TRANSPORT_UDP) |
                                (1 << RTSP_LOWER_TRANSPORT_TCP);
        if (!lower_transport_mask) {
            av_log(s, AV_LOG_ERROR, "Unsupported lower transport method, "
                                    "only UDP and TCP are supported for output.\n");
            err = AVERROR(EINVAL);
            goto fail;
        }
    }

    /* Construct the URI used in request; this is similar to s->filename,
     * but with authentication credentials removed and RTSP specific options
     * stripped out. */
    ff_url_join(rt->control_uri, sizeof(rt->control_uri), "rtsp", NULL,
                host, port, "%s", path);

    /* open the tcp connexion */
    ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, host, port, NULL);
    if (url_open(&rtsp_hd, tcpname, URL_RDWR) < 0) {
        err = AVERROR(EIO);
        goto fail;
    }
    rt->rtsp_hd = rtsp_hd;
    rt->seq = 0;

    tcp_fd = url_get_file_handle(rtsp_hd);
    if (!getpeername(tcp_fd, (struct sockaddr*) &peer, &peer_len)) {
        getnameinfo((struct sockaddr*) &peer, peer_len, host, sizeof(host),
                    NULL, 0, NI_NUMERICHOST);
    }

    /* request options supported by the server; this also detects server
     * type */
    for (rt->server_type = RTSP_SERVER_RTP;;) {
        cmd[0] = 0;
        if (rt->server_type == RTSP_SERVER_REAL)
            av_strlcat(cmd,
                       /**
                        * The following entries are required for proper
                        * streaming from a Realmedia server. They are
                        * interdependent in some way although we currently
                        * don't quite understand how. Values were copied
                        * from mplayer SVN r23589.
                        * @param CompanyID is a 16-byte ID in base64
                        * @param ClientChallenge is a 16-byte ID in hex
                        */
                       "ClientChallenge: 9e26d33f2984236010ef6253fb1887f7\r\n"
                       "PlayerStarttime: [28/03/2003:22:50:23 00:00]\r\n"
                       "CompanyID: KnKV4M4I/B2FjJ1TToLycw==\r\n"
                       "GUID: 00000000-0000-0000-0000-000000000000\r\n",
                       sizeof(cmd));
        ff_rtsp_send_cmd(s, "OPTIONS", rt->control_uri, cmd, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        /* detect server type if not standard-compliant RTP */
        if (rt->server_type != RTSP_SERVER_REAL && reply->real_challenge[0]) {
            rt->server_type = RTSP_SERVER_REAL;
            continue;
        } else if (!strncasecmp(reply->server, "WMServer/", 9)) {
            rt->server_type = RTSP_SERVER_WMS;
        } else if (rt->server_type == RTSP_SERVER_REAL)
            strcpy(real_challenge, reply->real_challenge);
        break;
    }

    if (s->iformat)
        err = rtsp_setup_input_streams(s, reply);
    else
        err = rtsp_setup_output_streams(s, host);
    if (err)
        goto fail;

    do {
        int lower_transport = ff_log2_tab[lower_transport_mask &
                                  ~(lower_transport_mask - 1)];

        err = make_setup_request(s, host, port, lower_transport,
                                 rt->server_type == RTSP_SERVER_REAL ?
                                     real_challenge : NULL);
        if (err < 0)
            goto fail;
        lower_transport_mask &= ~(1 << lower_transport);
        if (lower_transport_mask == 0 && err == 1) {
            err = FF_NETERROR(EPROTONOSUPPORT);
            goto fail;
        }
    } while (err);

    rt->state = RTSP_STATE_IDLE;
    rt->seek_timestamp = 0; /* default is to start stream at position zero */
    return 0;
 fail:
    ff_rtsp_close_streams(s);
    url_close(rt->rtsp_hd);
    if (reply->status_code >=300 && reply->status_code < 400 && s->iformat) {
        av_strlcpy(s->filename, reply->location, sizeof(s->filename));
        av_log(s, AV_LOG_INFO, "Status %d: Redirecting to %s\n",
               reply->status_code,
               s->filename);
        goto redirect;
    }
    ff_network_close();
    return err;
}
#endif

#if CONFIG_RTSP_DEMUXER
static int rtsp_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    RTSPState *rt = s->priv_data;
    int ret;

    ret = ff_rtsp_connect(s);
    if (ret)
        return ret;

    if (ap->initial_pause) {
         /* do not start immediately */
    } else {
         if (rtsp_read_play(s) < 0) {
            ff_rtsp_close_streams(s);
            url_close(rt->rtsp_hd);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int udp_read_packet(AVFormatContext *s, RTSPStream **prtsp_st,
                           uint8_t *buf, int buf_size)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    fd_set rfds;
    int fd, fd_max, n, i, ret, tcp_fd, timeout_cnt = 0;
    struct timeval tv;

    for (;;) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        FD_ZERO(&rfds);
        if (rt->rtsp_hd) {
            tcp_fd = fd_max = url_get_file_handle(rt->rtsp_hd);
            FD_SET(tcp_fd, &rfds);
        } else {
            fd_max = 0;
            tcp_fd = -1;
        }
        for (i = 0; i < rt->nb_rtsp_streams; i++) {
            rtsp_st = rt->rtsp_streams[i];
            if (rtsp_st->rtp_handle) {
                /* currently, we cannot probe RTCP handle because of
                 * blocking restrictions */
                fd = url_get_file_handle(rtsp_st->rtp_handle);
                if (fd > fd_max)
                    fd_max = fd;
                FD_SET(fd, &rfds);
            }
        }
        tv.tv_sec = 0;
        tv.tv_usec = SELECT_TIMEOUT_MS * 1000;
        n = select(fd_max + 1, &rfds, NULL, NULL, &tv);
        if (n > 0) {
            timeout_cnt = 0;
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                rtsp_st = rt->rtsp_streams[i];
                if (rtsp_st->rtp_handle) {
                    fd = url_get_file_handle(rtsp_st->rtp_handle);
                    if (FD_ISSET(fd, &rfds)) {
                        ret = url_read(rtsp_st->rtp_handle, buf, buf_size);
                        if (ret > 0) {
                            *prtsp_st = rtsp_st;
                            return ret;
                        }
                    }
                }
            }
#if CONFIG_RTSP_DEMUXER
            if (tcp_fd != -1 && FD_ISSET(tcp_fd, &rfds)) {
                RTSPMessageHeader reply;

                ret = ff_rtsp_read_reply(s, &reply, NULL, 0);
                if (ret < 0)
                    return ret;
                /* XXX: parse message */
                if (rt->state != RTSP_STATE_STREAMING)
                    return 0;
            }
#endif
        } else if (n == 0 && ++timeout_cnt >= MAX_TIMEOUTS) {
            return FF_NETERROR(ETIMEDOUT);
        } else if (n < 0 && errno != EINTR)
            return AVERROR(errno);
    }
}

static int tcp_read_packet(AVFormatContext *s, RTSPStream **prtsp_st,
                           uint8_t *buf, int buf_size)
{
    RTSPState *rt = s->priv_data;
    int id, len, i, ret;
    RTSPStream *rtsp_st;

#ifdef DEBUG_RTP_TCP
    dprintf(s, "tcp_read_packet:\n");
#endif
redo:
    for (;;) {
        RTSPMessageHeader reply;

        ret = ff_rtsp_read_reply(s, &reply, NULL, 1);
        if (ret == -1)
            return -1;
        if (ret == 1) /* received '$' */
            break;
        /* XXX: parse message */
        if (rt->state != RTSP_STATE_STREAMING)
            return 0;
    }
    ret = url_read_complete(rt->rtsp_hd, buf, 3);
    if (ret != 3)
        return -1;
    id  = buf[0];
    len = AV_RB16(buf + 1);
#ifdef DEBUG_RTP_TCP
    dprintf(s, "id=%d len=%d\n", id, len);
#endif
    if (len > buf_size || len < 12)
        goto redo;
    /* get the data */
    ret = url_read_complete(rt->rtsp_hd, buf, len);
    if (ret != len)
        return -1;
    if (rt->transport == RTSP_TRANSPORT_RDT &&
        ff_rdt_parse_header(buf, len, &id, NULL, NULL, NULL, NULL) < 0)
        return -1;

    /* find the matching stream */
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        rtsp_st = rt->rtsp_streams[i];
        if (id >= rtsp_st->interleaved_min &&
            id <= rtsp_st->interleaved_max)
            goto found;
    }
    goto redo;
found:
    *prtsp_st = rtsp_st;
    return len;
}

static int rtsp_fetch_packet(AVFormatContext *s, AVPacket *pkt)
{
    RTSPState *rt = s->priv_data;
    int ret, len;
    uint8_t buf[10 * RTP_MAX_PACKET_LENGTH];
    RTSPStream *rtsp_st;

    /* get next frames from the same RTP packet */
    if (rt->cur_transport_priv) {
        if (rt->transport == RTSP_TRANSPORT_RDT) {
            ret = ff_rdt_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
        } else
            ret = rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
        if (ret == 0) {
            rt->cur_transport_priv = NULL;
            return 0;
        } else if (ret == 1) {
            return 0;
        } else
            rt->cur_transport_priv = NULL;
    }

    /* read next RTP packet */
 redo:
    switch(rt->lower_transport) {
    default:
#if CONFIG_RTSP_DEMUXER
    case RTSP_LOWER_TRANSPORT_TCP:
        len = tcp_read_packet(s, &rtsp_st, buf, sizeof(buf));
        break;
#endif
    case RTSP_LOWER_TRANSPORT_UDP:
    case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
        len = udp_read_packet(s, &rtsp_st, buf, sizeof(buf));
        if (len >=0 && rtsp_st->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
            rtp_check_and_send_back_rr(rtsp_st->transport_priv, len);
        break;
    }
    if (len < 0)
        return len;
    if (len == 0)
        return AVERROR_EOF;
    if (rt->transport == RTSP_TRANSPORT_RDT) {
        ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, buf, len);
    } else {
        ret = rtp_parse_packet(rtsp_st->transport_priv, pkt, buf, len);
        if (ret < 0) {
            /* Either bad packet, or a RTCP packet. Check if the
             * first_rtcp_ntp_time field was initialized. */
            RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
            if (rtpctx->first_rtcp_ntp_time != AV_NOPTS_VALUE) {
                /* first_rtcp_ntp_time has been initialized for this stream,
                 * copy the same value to all other uninitialized streams,
                 * in order to map their timestamp origin to the same ntp time
                 * as this one. */
                int i;
                for (i = 0; i < rt->nb_rtsp_streams; i++) {
                    RTPDemuxContext *rtpctx2 = rtsp_st->transport_priv;
                    if (rtpctx2 &&
                        rtpctx2->first_rtcp_ntp_time == AV_NOPTS_VALUE)
                        rtpctx2->first_rtcp_ntp_time = rtpctx->first_rtcp_ntp_time;
                }
            }
        }
    }
    if (ret < 0)
        goto redo;
    if (ret == 1)
        /* more packets may follow, so we save the RTP context */
        rt->cur_transport_priv = rtsp_st->transport_priv;

    return ret;
}

static int rtsp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RTSPState *rt = s->priv_data;
    int ret;
    RTSPMessageHeader reply1, *reply = &reply1;
    char cmd[1024];

    if (rt->server_type == RTSP_SERVER_REAL) {
        int i;
        enum AVDiscard cache[MAX_STREAMS];

        for (i = 0; i < s->nb_streams; i++)
            cache[i] = s->streams[i]->discard;

        if (!rt->need_subscription) {
            if (memcmp (cache, rt->real_setup_cache,
                        sizeof(enum AVDiscard) * s->nb_streams)) {
                snprintf(cmd, sizeof(cmd),
                         "Unsubscribe: %s\r\n",
                         rt->last_subscription);
                ff_rtsp_send_cmd(s, "SET_PARAMETER", rt->control_uri,
                                 cmd, reply, NULL);
                if (reply->status_code != RTSP_STATUS_OK)
                    return AVERROR_INVALIDDATA;
                rt->need_subscription = 1;
            }
        }

        if (rt->need_subscription) {
            int r, rule_nr, first = 1;

            memcpy(rt->real_setup_cache, cache,
                   sizeof(enum AVDiscard) * s->nb_streams);
            rt->last_subscription[0] = 0;

            snprintf(cmd, sizeof(cmd),
                     "Subscribe: ");
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                rule_nr = 0;
                for (r = 0; r < s->nb_streams; r++) {
                    if (s->streams[r]->priv_data == rt->rtsp_streams[i]) {
                        if (s->streams[r]->discard != AVDISCARD_ALL) {
                            if (!first)
                                av_strlcat(rt->last_subscription, ",",
                                           sizeof(rt->last_subscription));
                            ff_rdt_subscribe_rule(
                                rt->last_subscription,
                                sizeof(rt->last_subscription), i, rule_nr);
                            first = 0;
                        }
                        rule_nr++;
                    }
                }
            }
            av_strlcatf(cmd, sizeof(cmd), "%s\r\n", rt->last_subscription);
            ff_rtsp_send_cmd(s, "SET_PARAMETER", rt->control_uri,
                             cmd, reply, NULL);
            if (reply->status_code != RTSP_STATUS_OK)
                return AVERROR_INVALIDDATA;
            rt->need_subscription = 0;

            if (rt->state == RTSP_STATE_STREAMING)
                rtsp_read_play (s);
        }
    }

    ret = rtsp_fetch_packet(s, pkt);
    if (ret < 0)
        return ret;

    /* send dummy request to keep TCP connection alive */
    if ((rt->server_type == RTSP_SERVER_WMS ||
         rt->server_type == RTSP_SERVER_REAL) &&
        (av_gettime() - rt->last_cmd_time) / 1000000 >= rt->timeout / 2) {
        if (rt->server_type == RTSP_SERVER_WMS) {
            ff_rtsp_send_cmd_async(s, "GET_PARAMETER", rt->control_uri, NULL);
        } else {
            ff_rtsp_send_cmd_async(s, "OPTIONS", "*", NULL);
        }
    }

    return 0;
}

/* pause the stream */
static int rtsp_read_pause(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;

    if (rt->state != RTSP_STATE_STREAMING)
        return 0;
    else if (!(rt->server_type == RTSP_SERVER_REAL && rt->need_subscription)) {
        ff_rtsp_send_cmd(s, "PAUSE", rt->control_uri, NULL, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            return -1;
        }
    }
    rt->state = RTSP_STATE_PAUSED;
    return 0;
}

static int rtsp_read_seek(AVFormatContext *s, int stream_index,
                          int64_t timestamp, int flags)
{
    RTSPState *rt = s->priv_data;

    rt->seek_timestamp = av_rescale_q(timestamp,
                                      s->streams[stream_index]->time_base,
                                      AV_TIME_BASE_Q);
    switch(rt->state) {
    default:
    case RTSP_STATE_IDLE:
        break;
    case RTSP_STATE_STREAMING:
        if (rtsp_read_pause(s) != 0)
            return -1;
        rt->state = RTSP_STATE_SEEKING;
        if (rtsp_read_play(s) != 0)
            return -1;
        break;
    case RTSP_STATE_PAUSED:
        rt->state = RTSP_STATE_IDLE;
        break;
    }
    return 0;
}

static int rtsp_read_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;

#if 0
    /* NOTE: it is valid to flush the buffer here */
    if (rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP) {
        url_fclose(&rt->rtsp_gb);
    }
#endif
    ff_rtsp_send_cmd_async(s, "TEARDOWN", rt->control_uri, NULL);

    ff_rtsp_close_streams(s);
    url_close(rt->rtsp_hd);
    ff_network_close();
    return 0;
}

AVInputFormat rtsp_demuxer = {
    "rtsp",
    NULL_IF_CONFIG_SMALL("RTSP input format"),
    sizeof(RTSPState),
    rtsp_probe,
    rtsp_read_header,
    rtsp_read_packet,
    rtsp_read_close,
    rtsp_read_seek,
    .flags = AVFMT_NOFILE,
    .read_play = rtsp_read_play,
    .read_pause = rtsp_read_pause,
};
#endif

static int sdp_probe(AVProbeData *p1)
{
    const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;

    /* we look for a line beginning "c=IN IP4" */
    while (p < p_end && *p != '\0') {
        if (p + sizeof("c=IN IP4") - 1 < p_end &&
            av_strstart(p, "c=IN IP4", NULL))
            return AVPROBE_SCORE_MAX / 2;

        while (p < p_end - 1 && *p != '\n') p++;
        if (++p >= p_end)
            break;
        if (*p == '\r')
            p++;
    }
    return 0;
}

#define SDP_MAX_SIZE 8192

static int sdp_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    int size, i, err;
    char *content;
    char url[1024];

    if (!ff_network_init())
        return AVERROR(EIO);

    /* read the whole sdp file */
    /* XXX: better loading */
    content = av_malloc(SDP_MAX_SIZE);
    size = get_buffer(s->pb, content, SDP_MAX_SIZE - 1);
    if (size <= 0) {
        av_free(content);
        return AVERROR_INVALIDDATA;
    }
    content[size] ='\0';

    sdp_parse(s, content);
    av_free(content);

    /* open each RTP stream */
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        rtsp_st = rt->rtsp_streams[i];

        ff_url_join(url, sizeof(url), "rtp", NULL,
                    inet_ntoa(rtsp_st->sdp_ip), rtsp_st->sdp_port,
                    "?localport=%d&ttl=%d", rtsp_st->sdp_port,
                    rtsp_st->sdp_ttl);
        if (url_open(&rtsp_st->rtp_handle, url, URL_RDWR) < 0) {
            err = AVERROR_INVALIDDATA;
            goto fail;
        }
        if ((err = rtsp_open_transport_ctx(s, rtsp_st)))
            goto fail;
    }
    return 0;
fail:
    ff_rtsp_close_streams(s);
    ff_network_close();
    return err;
}

static int sdp_read_close(AVFormatContext *s)
{
    ff_rtsp_close_streams(s);
    ff_network_close();
    return 0;
}

AVInputFormat sdp_demuxer = {
    "sdp",
    NULL_IF_CONFIG_SMALL("SDP"),
    sizeof(RTSPState),
    sdp_probe,
    sdp_read_header,
    rtsp_fetch_packet,
    sdp_read_close,
};
