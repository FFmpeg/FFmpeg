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
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "avio_internal.h"

#include <sys/time.h>
#if HAVE_POLL_H
#include <poll.h>
#endif
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "http.h"
#include "rtsp.h"

#include "rtpdec.h"
#include "rdt.h"
#include "rtpdec_formats.h"
#include "rtpenc_chain.h"
#include "url.h"
#include "rtpenc.h"

//#define DEBUG

/* Timeout values for socket poll, in ms,
 * and read_packet(), in seconds  */
#define POLL_TIMEOUT_MS 100
#define READ_PACKET_TIMEOUT_S 10
#define MAX_TIMEOUTS READ_PACKET_TIMEOUT_S * 1000 / POLL_TIMEOUT_MS
#define SDP_MAX_SIZE 16384
#define RECVBUF_SIZE 10 * RTP_MAX_PACKET_LENGTH

#define OFFSET(x) offsetof(RTSPState, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define ENC AV_OPT_FLAG_ENCODING_PARAM

#define RTSP_FLAG_OPTS(name, longname) \
    { name, longname, OFFSET(rtsp_flags), AV_OPT_TYPE_FLAGS, {0}, INT_MIN, INT_MAX, DEC, "rtsp_flags" }, \
    { "filter_src", "Only receive packets from the negotiated peer IP", 0, AV_OPT_TYPE_CONST, {RTSP_FLAG_FILTER_SRC}, 0, 0, DEC, "rtsp_flags" }

#define RTSP_MEDIATYPE_OPTS(name, longname) \
    { name, longname, OFFSET(media_type_mask), AV_OPT_TYPE_FLAGS, { (1 << (AVMEDIA_TYPE_DATA+1)) - 1 }, INT_MIN, INT_MAX, DEC, "allowed_media_types" }, \
    { "video", "Video", 0, AV_OPT_TYPE_CONST, {1 << AVMEDIA_TYPE_VIDEO}, 0, 0, DEC, "allowed_media_types" }, \
    { "audio", "Audio", 0, AV_OPT_TYPE_CONST, {1 << AVMEDIA_TYPE_AUDIO}, 0, 0, DEC, "allowed_media_types" }, \
    { "data", "Data", 0, AV_OPT_TYPE_CONST, {1 << AVMEDIA_TYPE_DATA}, 0, 0, DEC, "allowed_media_types" }

const AVOption ff_rtsp_options[] = {
    { "initial_pause",  "Don't start playing the stream immediately", OFFSET(initial_pause), AV_OPT_TYPE_INT, {0}, 0, 1, DEC },
    FF_RTP_FLAG_OPTS(RTSPState, rtp_muxer_flags),
    { "rtsp_transport", "RTSP transport protocols", OFFSET(lower_transport_mask), AV_OPT_TYPE_FLAGS, {0}, INT_MIN, INT_MAX, DEC|ENC, "rtsp_transport" }, \
    { "udp", "UDP", 0, AV_OPT_TYPE_CONST, {1 << RTSP_LOWER_TRANSPORT_UDP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
    { "tcp", "TCP", 0, AV_OPT_TYPE_CONST, {1 << RTSP_LOWER_TRANSPORT_TCP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
    { "udp_multicast", "UDP multicast", 0, AV_OPT_TYPE_CONST, {1 << RTSP_LOWER_TRANSPORT_UDP_MULTICAST}, 0, 0, DEC, "rtsp_transport" },
    { "http", "HTTP tunneling", 0, AV_OPT_TYPE_CONST, {(1 << RTSP_LOWER_TRANSPORT_HTTP)}, 0, 0, DEC, "rtsp_transport" },
    RTSP_FLAG_OPTS("rtsp_flags", "RTSP flags"),
    RTSP_MEDIATYPE_OPTS("allowed_media_types", "Media types to accept from the server"),
    { "min_port", "Minimum local UDP port", OFFSET(rtp_port_min), AV_OPT_TYPE_INT, {RTSP_RTP_PORT_MIN}, 0, 65535, DEC|ENC },
    { "max_port", "Maximum local UDP port", OFFSET(rtp_port_max), AV_OPT_TYPE_INT, {RTSP_RTP_PORT_MAX}, 0, 65535, DEC|ENC },
    { NULL },
};

static const AVOption sdp_options[] = {
    RTSP_FLAG_OPTS("sdp_flags", "SDP flags"),
    RTSP_MEDIATYPE_OPTS("allowed_media_types", "Media types to accept from the server"),
    { NULL },
};

static const AVOption rtp_options[] = {
    RTSP_FLAG_OPTS("rtp_flags", "RTP flags"),
    { NULL },
};

static void get_word_until_chars(char *buf, int buf_size,
                                 const char *sep, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    p += strspn(p, SPACE_CHARS);
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

/** Parse a string p in the form of Range:npt=xx-xx, and determine the start
 *  and end time.
 *  Used for seeking in the rtp stream.
 */
static void rtsp_parse_range_npt(const char *p, int64_t *start, int64_t *end)
{
    char buf[256];

    p += strspn(p, SPACE_CHARS);
    if (!av_stristart(p, "npt=", &p))
        return;

    *start = AV_NOPTS_VALUE;
    *end = AV_NOPTS_VALUE;

    get_word_sep(buf, sizeof(buf), "-", &p);
    av_parse_time(start, buf, 1);
    if (*p == '-') {
        p++;
        get_word_sep(buf, sizeof(buf), "-", &p);
        av_parse_time(end, buf, 1);
    }
//    av_log(NULL, AV_LOG_DEBUG, "Range Start: %lld\n", *start);
//    av_log(NULL, AV_LOG_DEBUG, "Range End: %lld\n", *end);
}

static int get_sockaddr(const char *buf, struct sockaddr_storage *sock)
{
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(buf, NULL, &hints, &ai))
        return -1;
    memcpy(sock, ai->ai_addr, FFMIN(sizeof(*sock), ai->ai_addrlen));
    freeaddrinfo(ai);
    return 0;
}

#if CONFIG_RTPDEC
static void init_rtp_handler(RTPDynamicProtocolHandler *handler,
                             RTSPStream *rtsp_st, AVCodecContext *codec)
{
    if (!handler)
        return;
    codec->codec_id          = handler->codec_id;
    rtsp_st->dynamic_handler = handler;
    if (handler->alloc) {
        rtsp_st->dynamic_protocol_context = handler->alloc();
        if (!rtsp_st->dynamic_protocol_context)
            rtsp_st->dynamic_handler = NULL;
    }
}

/* parse the rtpmap description: <codec_name>/<clock_rate>[/<other params>] */
static int sdp_parse_rtpmap(AVFormatContext *s,
                            AVStream *st, RTSPStream *rtsp_st,
                            int payload_type, const char *p)
{
    AVCodecContext *codec = st->codec;
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
        RTPDynamicProtocolHandler *handler =
            ff_rtp_handler_find_by_name(buf, codec->codec_type);
        init_rtp_handler(handler, rtsp_st, codec);
        /* If no dynamic handler was found, check with the list of standard
         * allocated types, if such a stream for some reason happens to
         * use a private payload type. This isn't handled in rtpdec.c, since
         * the format name from the rtpmap line never is passed into rtpdec. */
        if (!rtsp_st->dynamic_handler)
            codec->codec_id = ff_rtp_codec_id(buf, codec->codec_type);
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
            avpriv_set_pts_info(st, 32, 1, codec->sample_rate);
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
        if (i > 0)
            avpriv_set_pts_info(st, 32, 1, i);
        break;
    default:
        break;
    }
    if (rtsp_st->dynamic_handler && rtsp_st->dynamic_handler->init)
        rtsp_st->dynamic_handler->init(s, st->index,
                                       rtsp_st->dynamic_protocol_context);
    return 0;
}

/* parse the attribute line from the fmtp a line of an sdp response. This
 * is broken out as a function because it is used in rtp_h264.c, which is
 * forthcoming. */
int ff_rtsp_next_attr_and_value(const char **p, char *attr, int attr_size,
                                char *value, int value_size)
{
    *p += strspn(*p, SPACE_CHARS);
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

typedef struct SDPParseState {
    /* SDP only */
    struct sockaddr_storage default_ip;
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
    struct sockaddr_storage sdp_ip;
    int ttl;

    av_dlog(s, "sdp: %c='%s'\n", letter, buf);

    p = buf;
    if (s1->skip_media && letter != 'm')
        return;
    switch (letter) {
    case 'c':
        get_word(buf1, sizeof(buf1), &p);
        if (strcmp(buf1, "IN") != 0)
            return;
        get_word(buf1, sizeof(buf1), &p);
        if (strcmp(buf1, "IP4") && strcmp(buf1, "IP6"))
            return;
        get_word_sep(buf1, sizeof(buf1), "/", &p);
        if (get_sockaddr(buf1, &sdp_ip))
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
            rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
            rtsp_st->sdp_ip = sdp_ip;
            rtsp_st->sdp_ttl = ttl;
        }
        break;
    case 's':
        av_dict_set(&s->metadata, "title", p, 0);
        break;
    case 'i':
        if (s->nb_streams == 0) {
            av_dict_set(&s->metadata, "comment", p, 0);
            break;
        }
        break;
    case 'm':
        /* new stream */
        s1->skip_media = 0;
        codec_type = AVMEDIA_TYPE_UNKNOWN;
        get_word(st_type, sizeof(st_type), &p);
        if (!strcmp(st_type, "audio")) {
            codec_type = AVMEDIA_TYPE_AUDIO;
        } else if (!strcmp(st_type, "video")) {
            codec_type = AVMEDIA_TYPE_VIDEO;
        } else if (!strcmp(st_type, "application")) {
            codec_type = AVMEDIA_TYPE_DATA;
        }
        if (codec_type == AVMEDIA_TYPE_UNKNOWN || !(rt->media_type_mask & (1 << codec_type))) {
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
            st = avformat_new_stream(s, NULL);
            if (!st)
                return;
            st->id = rt->nb_rtsp_streams - 1;
            rtsp_st->stream_index = st->index;
            st->codec->codec_type = codec_type;
            if (rtsp_st->sdp_payload_type < RTP_PT_PRIVATE) {
                RTPDynamicProtocolHandler *handler;
                /* if standard payload type, we can find the codec right now */
                ff_rtp_get_codec_info(st->codec, rtsp_st->sdp_payload_type);
                if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                    st->codec->sample_rate > 0)
                    avpriv_set_pts_info(st, 32, 1, st->codec->sample_rate);
                /* Even static payload types may need a custom depacketizer */
                handler = ff_rtp_handler_find_by_id(
                              rtsp_st->sdp_payload_type, st->codec->codec_type);
                init_rtp_handler(handler, rtsp_st, st->codec);
                if (handler && handler->init)
                    handler->init(s, st->index,
                                  rtsp_st->dynamic_protocol_context);
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
                rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];

                /* XXX: may need to add full url resolution */
                av_url_split(proto, sizeof(proto), NULL, 0, NULL, 0,
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
            rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
            sdp_parse_rtpmap(s, st, rtsp_st, payload_type, p);
        } else if (av_strstart(p, "fmtp:", &p) ||
                   av_strstart(p, "framesize:", &p)) {
            /* NOTE: fmtp is only supported AFTER the 'a=rtpmap:xxx' tag */
            // let dynamic protocol handlers have a stab at the line.
            get_word(buf1, sizeof(buf1), &p);
            payload_type = atoi(buf1);
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                rtsp_st = rt->rtsp_streams[i];
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
        } else if (av_strstart(p, "SampleRate:integer;", &p) &&
                   s->nb_streams > 0) {
            st = s->streams[s->nb_streams - 1];
            st->codec->sample_rate = atoi(p);
        } else {
            if (rt->server_type == RTSP_SERVER_WMS)
                ff_wms_parse_sdp_a_line(s, p);
            if (s->nb_streams > 0) {
                if (rt->server_type == RTSP_SERVER_REAL)
                    ff_real_parse_sdp_a_line(s, s->nb_streams - 1, p);

                rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
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

int ff_sdp_parse(AVFormatContext *s, const char *content)
{
    RTSPState *rt = s->priv_data;
    const char *p;
    int letter;
    /* Some SDP lines, particularly for Realmedia or ASF RTSP streams,
     * contain long SDP lines containing complete ASF Headers (several
     * kB) or arrays of MDPR (RM stream descriptor) headers plus
     * "rulebooks" describing their properties. Therefore, the SDP line
     * buffer is large.
     *
     * The Vorbis FMTP line can be up to 16KB - see xiph_parse_sdp_line
     * in rtpdec_xiph.c. */
    char buf[16384], *q;
    SDPParseState sdp_parse_state, *s1 = &sdp_parse_state;

    memset(s1, 0, sizeof(SDPParseState));
    p = content;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
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
    rt->p = av_malloc(sizeof(struct pollfd)*2*(rt->nb_rtsp_streams+1));
    if (!rt->p) return AVERROR(ENOMEM);
    return 0;
}
#endif /* CONFIG_RTPDEC */

void ff_rtsp_undo_setup(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int i;

    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        RTSPStream *rtsp_st = rt->rtsp_streams[i];
        if (!rtsp_st)
            continue;
        if (rtsp_st->transport_priv) {
            if (s->oformat) {
                AVFormatContext *rtpctx = rtsp_st->transport_priv;
                av_write_trailer(rtpctx);
                if (rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP) {
                    uint8_t *ptr;
                    avio_close_dyn_buf(rtpctx->pb, &ptr);
                    av_free(ptr);
                } else {
                    avio_close(rtpctx->pb);
                }
                avformat_free_context(rtpctx);
            } else if (rt->transport == RTSP_TRANSPORT_RDT && CONFIG_RTPDEC)
                ff_rdt_parse_close(rtsp_st->transport_priv);
            else if (CONFIG_RTPDEC)
                ff_rtp_parse_close(rtsp_st->transport_priv);
        }
        rtsp_st->transport_priv = NULL;
        if (rtsp_st->rtp_handle)
            ffurl_close(rtsp_st->rtp_handle);
        rtsp_st->rtp_handle = NULL;
    }
}

/* close and free RTSP streams */
void ff_rtsp_close_streams(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int i;
    RTSPStream *rtsp_st;

    ff_rtsp_undo_setup(s);
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        rtsp_st = rt->rtsp_streams[i];
        if (rtsp_st) {
            if (rtsp_st->dynamic_handler && rtsp_st->dynamic_protocol_context)
                rtsp_st->dynamic_handler->free(
                    rtsp_st->dynamic_protocol_context);
            av_free(rtsp_st);
        }
    }
    av_free(rt->rtsp_streams);
    if (rt->asf_ctx) {
        avformat_close_input(&rt->asf_ctx);
    }
    av_free(rt->p);
    av_free(rt->recvbuf);
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

    if (s->oformat && CONFIG_RTSP_MUXER) {
        rtsp_st->transport_priv = ff_rtp_chain_mux_open(s, st,
                                      rtsp_st->rtp_handle,
                                      RTSP_TCP_MAX_PACKET_SIZE);
        /* Ownership of rtp_handle is passed to the rtp mux context */
        rtsp_st->rtp_handle = NULL;
    } else if (rt->transport == RTSP_TRANSPORT_RDT && CONFIG_RTPDEC)
        rtsp_st->transport_priv = ff_rdt_parse_open(s, st->index,
                                            rtsp_st->dynamic_protocol_context,
                                            rtsp_st->dynamic_handler);
    else if (CONFIG_RTPDEC)
        rtsp_st->transport_priv = ff_rtp_parse_open(s, st, rtsp_st->rtp_handle,
                                         rtsp_st->sdp_payload_type,
            (rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP || !s->max_delay)
            ? 0 : RTP_REORDER_QUEUE_DEFAULT_SIZE);

    if (!rtsp_st->transport_priv) {
         return AVERROR(ENOMEM);
    } else if (rt->transport != RTSP_TRANSPORT_RDT && CONFIG_RTPDEC) {
        if (rtsp_st->dynamic_handler) {
            ff_rtp_parse_set_dynamic_protocol(rtsp_st->transport_priv,
                                              rtsp_st->dynamic_protocol_context,
                                              rtsp_st->dynamic_handler);
        }
    }

    return 0;
}

#if CONFIG_RTSP_DEMUXER || CONFIG_RTSP_MUXER
static void rtsp_parse_range(int *min_ptr, int *max_ptr, const char **pp)
{
    const char *p;
    int v;

    p = *pp;
    p += strspn(p, SPACE_CHARS);
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
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;

        th = &reply->transports[reply->nb_transports];

        get_word_sep(transport_protocol, sizeof(transport_protocol),
                     "/", &p);
        if (!av_strcasecmp (transport_protocol, "rtp")) {
            get_word_sep(profile, sizeof(profile), "/;,", &p);
            lower_transport[0] = '\0';
            /* rtp/avp/<protocol> */
            if (*p == '/') {
                get_word_sep(lower_transport, sizeof(lower_transport),
                             ";,", &p);
            }
            th->transport = RTSP_TRANSPORT_RTP;
        } else if (!av_strcasecmp (transport_protocol, "x-pn-tng") ||
                   !av_strcasecmp (transport_protocol, "x-real-rdt")) {
            /* x-pn-tng/<protocol> */
            get_word_sep(lower_transport, sizeof(lower_transport), "/;,", &p);
            profile[0] = '\0';
            th->transport = RTSP_TRANSPORT_RDT;
        }
        if (!av_strcasecmp(lower_transport, "TCP"))
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
                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";,", &p);
                    get_sockaddr(buf, &th->destination);
                }
            } else if (!strcmp(parameter, "source")) {
                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";,", &p);
                    av_strlcpy(th->source, buf, sizeof(th->source));
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

static void handle_rtp_info(RTSPState *rt, const char *url,
                            uint32_t seq, uint32_t rtptime)
{
    int i;
    if (!rtptime || !url[0])
        return;
    if (rt->transport != RTSP_TRANSPORT_RTP)
        return;
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        RTSPStream *rtsp_st = rt->rtsp_streams[i];
        RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
        if (!rtpctx)
            continue;
        if (!strcmp(rtsp_st->control_url, url)) {
            rtpctx->base_timestamp = rtptime;
            break;
        }
    }
}

static void rtsp_parse_rtp_info(RTSPState *rt, const char *p)
{
    int read = 0;
    char key[20], value[1024], url[1024] = "";
    uint32_t seq = 0, rtptime = 0;

    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (!*p)
            break;
        get_word_sep(key, sizeof(key), "=", &p);
        if (*p != '=')
            break;
        p++;
        get_word_sep(value, sizeof(value), ";, ", &p);
        read++;
        if (!strcmp(key, "url"))
            av_strlcpy(url, value, sizeof(url));
        else if (!strcmp(key, "seq"))
            seq = strtoul(value, NULL, 10);
        else if (!strcmp(key, "rtptime"))
            rtptime = strtoul(value, NULL, 10);
        if (*p == ',') {
            handle_rtp_info(rt, url, seq, rtptime);
            url[0] = '\0';
            seq = rtptime = 0;
            read = 0;
        }
        if (*p)
            p++;
    }
    if (read > 0)
        handle_rtp_info(rt, url, seq, rtptime);
}

void ff_rtsp_parse_line(RTSPMessageHeader *reply, const char *buf,
                        RTSPState *rt, const char *method)
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
        p += strspn(p, SPACE_CHARS);
        av_strlcpy(reply->real_challenge, p, sizeof(reply->real_challenge));
    } else if (av_stristart(p, "Server:", &p)) {
        p += strspn(p, SPACE_CHARS);
        av_strlcpy(reply->server, p, sizeof(reply->server));
    } else if (av_stristart(p, "Notice:", &p) ||
               av_stristart(p, "X-Notice:", &p)) {
        reply->notice = strtol(p, NULL, 10);
    } else if (av_stristart(p, "Location:", &p)) {
        p += strspn(p, SPACE_CHARS);
        av_strlcpy(reply->location, p , sizeof(reply->location));
    } else if (av_stristart(p, "WWW-Authenticate:", &p) && rt) {
        p += strspn(p, SPACE_CHARS);
        ff_http_auth_handle_header(&rt->auth_state, "WWW-Authenticate", p);
    } else if (av_stristart(p, "Authentication-Info:", &p) && rt) {
        p += strspn(p, SPACE_CHARS);
        ff_http_auth_handle_header(&rt->auth_state, "Authentication-Info", p);
    } else if (av_stristart(p, "Content-Base:", &p) && rt) {
        p += strspn(p, SPACE_CHARS);
        if (method && !strcmp(method, "DESCRIBE"))
            av_strlcpy(rt->control_uri, p , sizeof(rt->control_uri));
    } else if (av_stristart(p, "RTP-Info:", &p) && rt) {
        p += strspn(p, SPACE_CHARS);
        if (method && !strcmp(method, "PLAY"))
            rtsp_parse_rtp_info(rt, p);
    } else if (av_stristart(p, "Public:", &p) && rt) {
        if (strstr(p, "GET_PARAMETER") &&
            method && !strcmp(method, "OPTIONS"))
            rt->get_parameter_supported = 1;
    } else if (av_stristart(p, "x-Accept-Dynamic-Rate:", &p) && rt) {
        p += strspn(p, SPACE_CHARS);
        rt->accept_dynamic_rate = atoi(p);
    }
}

/* skip a RTP/TCP interleaved packet */
void ff_rtsp_skip_packet(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int ret, len, len1;
    uint8_t buf[1024];

    ret = ffurl_read_complete(rt->rtsp_hd, buf, 3);
    if (ret != 3)
        return;
    len = AV_RB16(buf + 1);

    av_dlog(s, "skipping RTP packet len=%d\n", len);

    /* skip payload */
    while (len > 0) {
        len1 = len;
        if (len1 > sizeof(buf))
            len1 = sizeof(buf);
        ret = ffurl_read_complete(rt->rtsp_hd, buf, len1);
        if (ret != len1)
            return;
        len -= len1;
    }
}

int ff_rtsp_read_reply(AVFormatContext *s, RTSPMessageHeader *reply,
                       unsigned char **content_ptr,
                       int return_on_interleaved_data, const char *method)
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
            ret = ffurl_read_complete(rt->rtsp_hd, &ch, 1);
            av_dlog(s, "ret=%d c=%02x [%c]\n", ret, ch, ch);
            if (ret != 1)
                return AVERROR_EOF;
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

        av_dlog(s, "line='%s'\n", buf);

        /* test if last line */
        if (buf[0] == '\0')
            break;
        p = buf;
        if (line_count == 0) {
            /* get reply code */
            get_word(buf1, sizeof(buf1), &p);
            get_word(buf1, sizeof(buf1), &p);
            reply->status_code = atoi(buf1);
            av_strlcpy(reply->reason, p, sizeof(reply->reason));
        } else {
            ff_rtsp_parse_line(reply, p, rt, method);
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
        ffurl_read_complete(rt->rtsp_hd, content, content_length);
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

/**
 * Send a command to the RTSP server without waiting for the reply.
 *
 * @param s RTSP (de)muxer context
 * @param method the method for the request
 * @param url the target url for the request
 * @param headers extra header lines to include in the request
 * @param send_content if non-null, the data to send as request body content
 * @param send_content_length the length of the send_content data, or 0 if
 *                            send_content is null
 *
 * @return zero if success, nonzero otherwise
 */
static int ff_rtsp_send_cmd_with_content_async(AVFormatContext *s,
                                               const char *method, const char *url,
                                               const char *headers,
                                               const unsigned char *send_content,
                                               int send_content_length)
{
    RTSPState *rt = s->priv_data;
    char buf[4096], *out_buf;
    char base64buf[AV_BASE64_SIZE(sizeof(buf))];

    /* Add in RTSP headers */
    out_buf = buf;
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

    /* base64 encode rtsp if tunneling */
    if (rt->control_transport == RTSP_MODE_TUNNEL) {
        av_base64_encode(base64buf, sizeof(base64buf), buf, strlen(buf));
        out_buf = base64buf;
    }

    av_dlog(s, "Sending:\n%s--\n", buf);

    ffurl_write(rt->rtsp_hd_out, out_buf, strlen(out_buf));
    if (send_content_length > 0 && send_content) {
        if (rt->control_transport == RTSP_MODE_TUNNEL) {
            av_log(s, AV_LOG_ERROR, "tunneling of RTSP requests "
                                    "with content data not supported\n");
            return AVERROR_PATCHWELCOME;
        }
        ffurl_write(rt->rtsp_hd_out, send_content, send_content_length);
    }
    rt->last_cmd_time = av_gettime();

    return 0;
}

int ff_rtsp_send_cmd_async(AVFormatContext *s, const char *method,
                           const char *url, const char *headers)
{
    return ff_rtsp_send_cmd_with_content_async(s, method, url, headers, NULL, 0);
}

int ff_rtsp_send_cmd(AVFormatContext *s, const char *method, const char *url,
                     const char *headers, RTSPMessageHeader *reply,
                     unsigned char **content_ptr)
{
    return ff_rtsp_send_cmd_with_content(s, method, url, headers, reply,
                                         content_ptr, NULL, 0);
}

int ff_rtsp_send_cmd_with_content(AVFormatContext *s,
                                  const char *method, const char *url,
                                  const char *header,
                                  RTSPMessageHeader *reply,
                                  unsigned char **content_ptr,
                                  const unsigned char *send_content,
                                  int send_content_length)
{
    RTSPState *rt = s->priv_data;
    HTTPAuthType cur_auth_type;
    int ret;

retry:
    cur_auth_type = rt->auth_state.auth_type;
    if ((ret = ff_rtsp_send_cmd_with_content_async(s, method, url, header,
                                                   send_content,
                                                   send_content_length)))
        return ret;

    if ((ret = ff_rtsp_read_reply(s, reply, content_ptr, 0, method) ) < 0)
        return ret;

    if (reply->status_code == 401 && cur_auth_type == HTTP_AUTH_NONE &&
        rt->auth_state.auth_type != HTTP_AUTH_NONE)
        goto retry;

    if (reply->status_code > 400){
        av_log(s, AV_LOG_ERROR, "method %s failed: %d%s\n",
               method,
               reply->status_code,
               reply->reason);
        av_log(s, AV_LOG_DEBUG, "%s\n", rt->last_reply);
    }

    return 0;
}

int ff_rtsp_make_setup_request(AVFormatContext *s, const char *host, int port,
                              int lower_transport, const char *real_challenge)
{
    RTSPState *rt = s->priv_data;
    int rtx = 0, j, i, err, interleave = 0, port_off;
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

    /* Choose a random starting offset within the first half of the
     * port range, to allow for a number of ports to try even if the offset
     * happens to be at the end of the random range. */
    port_off = av_get_random_seed() % ((rt->rtp_port_max - rt->rtp_port_min)/2);
    /* even random offset */
    port_off -= port_off & 0x01;

    for (j = rt->rtp_port_min + port_off, i = 0; i < rt->nb_rtsp_streams; ++i) {
        char transport[2048];

        /*
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
            while (j <= rt->rtp_port_max) {
                ff_url_join(buf, sizeof(buf), "rtp", NULL, host, -1,
                            "?localport=%d", j);
                /* we will use two ports per rtp stream (rtp and rtcp) */
                j += 2;
                if (!ffurl_open(&rtsp_st->rtp_handle, buf, AVIO_FLAG_READ_WRITE,
                               &s->interrupt_callback, NULL))
                    goto rtp_opened;
            }
            av_log(s, AV_LOG_ERROR, "Unable to open an input RTP port\n");
            err = AVERROR(EIO);
            goto fail;

        rtp_opened:
            port = ff_rtp_get_local_rtp_port(rtsp_st->rtp_handle);
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
            /* For WMS streams, the application streams are only used for
             * UDP. When trying to set it up for TCP streams, the server
             * will return an error. Therefore, we skip those streams. */
            if (rt->server_type == RTSP_SERVER_WMS &&
                s->streams[rtsp_st->stream_index]->codec->codec_type ==
                    AVMEDIA_TYPE_DATA)
                continue;
            snprintf(transport, sizeof(transport) - 1,
                     "%s/TCP;", trans_pref);
            if (rt->transport != RTSP_TRANSPORT_RDT)
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
        if (rt->accept_dynamic_rate)
            av_strlcat(cmd, "x-Dynamic-Rate: 0\r\n", sizeof(cmd));
        if (i == 0 && rt->server_type == RTSP_SERVER_REAL && CONFIG_RTPDEC) {
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

        /* Fail if the server responded with another lower transport mode
         * than what we requested. */
        if (reply->transports[0].lower_transport != lower_transport) {
            av_log(s, AV_LOG_ERROR, "Nonmatching transport in server reply\n");
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        switch(reply->transports[0].lower_transport) {
        case RTSP_LOWER_TRANSPORT_TCP:
            rtsp_st->interleaved_min = reply->transports[0].interleaved_min;
            rtsp_st->interleaved_max = reply->transports[0].interleaved_max;
            break;

        case RTSP_LOWER_TRANSPORT_UDP: {
            char url[1024], options[30] = "";

            if (rt->rtsp_flags & RTSP_FLAG_FILTER_SRC)
                av_strlcpy(options, "?connect=1", sizeof(options));
            /* Use source address if specified */
            if (reply->transports[0].source[0]) {
                ff_url_join(url, sizeof(url), "rtp", NULL,
                            reply->transports[0].source,
                            reply->transports[0].server_port_min, "%s", options);
            } else {
                ff_url_join(url, sizeof(url), "rtp", NULL, host,
                            reply->transports[0].server_port_min, "%s", options);
            }
            if (!(rt->server_type == RTSP_SERVER_WMS && i > 1) &&
                ff_rtp_set_remote_url(rtsp_st->rtp_handle, url) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            /* Try to initialize the connection state in a
             * potential NAT router by sending dummy packets.
             * RTP/RTCP dummy packets are used for RDT, too.
             */
            if (!(rt->server_type == RTSP_SERVER_WMS && i > 1) && s->iformat &&
                CONFIG_RTPDEC)
                ff_rtp_send_punch_packets(rtsp_st->rtp_handle);
            break;
        }
        case RTSP_LOWER_TRANSPORT_UDP_MULTICAST: {
            char url[1024], namebuf[50];
            struct sockaddr_storage addr;
            int port, ttl;

            if (reply->transports[0].destination.ss_family) {
                addr      = reply->transports[0].destination;
                port      = reply->transports[0].port_min;
                ttl       = reply->transports[0].ttl;
            } else {
                addr      = rtsp_st->sdp_ip;
                port      = rtsp_st->sdp_port;
                ttl       = rtsp_st->sdp_ttl;
            }
            getnameinfo((struct sockaddr*) &addr, sizeof(addr),
                        namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
            ff_url_join(url, sizeof(url), "rtp", NULL, namebuf,
                        port, "?ttl=%d", ttl);
            if (ffurl_open(&rtsp_st->rtp_handle, url, AVIO_FLAG_READ_WRITE,
                           &s->interrupt_callback, NULL) < 0) {
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
    ff_rtsp_undo_setup(s);
    return err;
}

void ff_rtsp_close_connections(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    if (rt->rtsp_hd_out != rt->rtsp_hd) ffurl_close(rt->rtsp_hd_out);
    ffurl_close(rt->rtsp_hd);
    rt->rtsp_hd = rt->rtsp_hd_out = NULL;
}

int ff_rtsp_connect(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    char host[1024], path[1024], tcpname[1024], cmd[2048], auth[128];
    char *option_list, *option, *filename;
    int port, err, tcp_fd;
    RTSPMessageHeader reply1 = {0}, *reply = &reply1;
    int lower_transport_mask = 0;
    char real_challenge[64] = "";
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);

    if (rt->rtp_port_max < rt->rtp_port_min) {
        av_log(s, AV_LOG_ERROR, "Invalid UDP port range, max port %d less "
                                "than min port %d\n", rt->rtp_port_max,
                                                      rt->rtp_port_min);
        return AVERROR(EINVAL);
    }

    if (!ff_network_init())
        return AVERROR(EIO);

    rt->control_transport = RTSP_MODE_PLAIN;
    if (rt->lower_transport_mask & (1 << RTSP_LOWER_TRANSPORT_HTTP)) {
        rt->lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
        rt->control_transport = RTSP_MODE_TUNNEL;
    }
    /* Only pass through valid flags from here */
    rt->lower_transport_mask &= (1 << RTSP_LOWER_TRANSPORT_NB) - 1;

redirect:
    lower_transport_mask = rt->lower_transport_mask;
    /* extract hostname and port */
    av_url_split(NULL, 0, auth, sizeof(auth),
                 host, sizeof(host), &port, path, sizeof(path), s->filename);
    if (*auth) {
        av_strlcpy(rt->auth, auth, sizeof(rt->auth));
    }
    if (port < 0)
        port = RTSP_DEFAULT_PORT;

#if FF_API_RTSP_URL_OPTIONS
    /* search for options */
    option_list = strrchr(path, '?');
    if (option_list) {
        /* Strip out the RTSP specific options, write out the rest of
         * the options back into the same string. */
        filename = option_list;
        while (option_list) {
            int handled = 1;
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
            } else if(!strcmp(option, "http")) {
                lower_transport_mask |= (1<< RTSP_LOWER_TRANSPORT_TCP);
                rt->control_transport = RTSP_MODE_TUNNEL;
            } else if (!strcmp(option, "filter_src")) {
                rt->rtsp_flags |= RTSP_FLAG_FILTER_SRC;
            } else {
                /* Write options back into the buffer, using memmove instead
                 * of strcpy since the strings may overlap. */
                int len = strlen(option);
                memmove(++filename, option, len);
                filename += len;
                if (option_list) *filename = '&';
                handled = 0;
            }
            if (handled)
                av_log(s, AV_LOG_WARNING, "Options passed via URL are "
                                          "deprecated, use -rtsp_transport "
                                          "and -rtsp_flags instead.\n");
        }
        *filename = 0;
    }
#endif

    if (!lower_transport_mask)
        lower_transport_mask = (1 << RTSP_LOWER_TRANSPORT_NB) - 1;

    if (s->oformat) {
        /* Only UDP or TCP - UDP multicast isn't supported. */
        lower_transport_mask &= (1 << RTSP_LOWER_TRANSPORT_UDP) |
                                (1 << RTSP_LOWER_TRANSPORT_TCP);
        if (!lower_transport_mask || rt->control_transport == RTSP_MODE_TUNNEL) {
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

    if (rt->control_transport == RTSP_MODE_TUNNEL) {
        /* set up initial handshake for tunneling */
        char httpname[1024];
        char sessioncookie[17];
        char headers[1024];

        ff_url_join(httpname, sizeof(httpname), "http", auth, host, port, "%s", path);
        snprintf(sessioncookie, sizeof(sessioncookie), "%08x%08x",
                 av_get_random_seed(), av_get_random_seed());

        /* GET requests */
        if (ffurl_alloc(&rt->rtsp_hd, httpname, AVIO_FLAG_READ,
                        &s->interrupt_callback) < 0) {
            err = AVERROR(EIO);
            goto fail;
        }

        /* generate GET headers */
        snprintf(headers, sizeof(headers),
                 "x-sessioncookie: %s\r\n"
                 "Accept: application/x-rtsp-tunnelled\r\n"
                 "Pragma: no-cache\r\n"
                 "Cache-Control: no-cache\r\n",
                 sessioncookie);
        av_opt_set(rt->rtsp_hd->priv_data, "headers", headers, 0);

        /* complete the connection */
        if (ffurl_connect(rt->rtsp_hd, NULL)) {
            err = AVERROR(EIO);
            goto fail;
        }

        /* POST requests */
        if (ffurl_alloc(&rt->rtsp_hd_out, httpname, AVIO_FLAG_WRITE,
                        &s->interrupt_callback) < 0 ) {
            err = AVERROR(EIO);
            goto fail;
        }

        /* generate POST headers */
        snprintf(headers, sizeof(headers),
                 "x-sessioncookie: %s\r\n"
                 "Content-Type: application/x-rtsp-tunnelled\r\n"
                 "Pragma: no-cache\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Content-Length: 32767\r\n"
                 "Expires: Sun, 9 Jan 1972 00:00:00 GMT\r\n",
                 sessioncookie);
        av_opt_set(rt->rtsp_hd_out->priv_data, "headers", headers, 0);
        av_opt_set(rt->rtsp_hd_out->priv_data, "chunked_post", "0", 0);

        /* Initialize the authentication state for the POST session. The HTTP
         * protocol implementation doesn't properly handle multi-pass
         * authentication for POST requests, since it would require one of
         * the following:
         * - implementing Expect: 100-continue, which many HTTP servers
         *   don't support anyway, even less the RTSP servers that do HTTP
         *   tunneling
         * - sending the whole POST data until getting a 401 reply specifying
         *   what authentication method to use, then resending all that data
         * - waiting for potential 401 replies directly after sending the
         *   POST header (waiting for some unspecified time)
         * Therefore, we copy the full auth state, which works for both basic
         * and digest. (For digest, we would have to synchronize the nonce
         * count variable between the two sessions, if we'd do more requests
         * with the original session, though.)
         */
        ff_http_init_auth_state(rt->rtsp_hd_out, rt->rtsp_hd);

        /* complete the connection */
        if (ffurl_connect(rt->rtsp_hd_out, NULL)) {
            err = AVERROR(EIO);
            goto fail;
        }
    } else {
        /* open the tcp connection */
        ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, host, port, NULL);
        if (ffurl_open(&rt->rtsp_hd, tcpname, AVIO_FLAG_READ_WRITE,
                       &s->interrupt_callback, NULL) < 0) {
            err = AVERROR(EIO);
            goto fail;
        }
        rt->rtsp_hd_out = rt->rtsp_hd;
    }
    rt->seq = 0;

    tcp_fd = ffurl_get_file_handle(rt->rtsp_hd);
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
                       /*
                        * The following entries are required for proper
                        * streaming from a Realmedia server. They are
                        * interdependent in some way although we currently
                        * don't quite understand how. Values were copied
                        * from mplayer SVN r23589.
                        *   ClientChallenge is a 16-byte ID in hex
                        *   CompanyID is a 16-byte ID in base64
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
        } else if (!av_strncasecmp(reply->server, "WMServer/", 9)) {
            rt->server_type = RTSP_SERVER_WMS;
        } else if (rt->server_type == RTSP_SERVER_REAL)
            strcpy(real_challenge, reply->real_challenge);
        break;
    }

    if (s->iformat && CONFIG_RTSP_DEMUXER)
        err = ff_rtsp_setup_input_streams(s, reply);
    else if (CONFIG_RTSP_MUXER)
        err = ff_rtsp_setup_output_streams(s, host);
    if (err)
        goto fail;

    do {
        int lower_transport = ff_log2_tab[lower_transport_mask &
                                  ~(lower_transport_mask - 1)];

        err = ff_rtsp_make_setup_request(s, host, port, lower_transport,
                                 rt->server_type == RTSP_SERVER_REAL ?
                                     real_challenge : NULL);
        if (err < 0)
            goto fail;
        lower_transport_mask &= ~(1 << lower_transport);
        if (lower_transport_mask == 0 && err == 1) {
            err = AVERROR(EPROTONOSUPPORT);
            goto fail;
        }
    } while (err);

    rt->lower_transport_mask = lower_transport_mask;
    av_strlcpy(rt->real_challenge, real_challenge, sizeof(rt->real_challenge));
    rt->state = RTSP_STATE_IDLE;
    rt->seek_timestamp = 0; /* default is to start stream at position zero */
    return 0;
 fail:
    ff_rtsp_close_streams(s);
    ff_rtsp_close_connections(s);
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
#endif /* CONFIG_RTSP_DEMUXER || CONFIG_RTSP_MUXER */

#if CONFIG_RTPDEC
static int udp_read_packet(AVFormatContext *s, RTSPStream **prtsp_st,
                           uint8_t *buf, int buf_size, int64_t wait_end)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    int n, i, ret, tcp_fd, timeout_cnt = 0;
    int max_p = 0;
    struct pollfd *p = rt->p;

    for (;;) {
        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;
        if (wait_end && wait_end - av_gettime() < 0)
            return AVERROR(EAGAIN);
        max_p = 0;
        if (rt->rtsp_hd) {
            tcp_fd = ffurl_get_file_handle(rt->rtsp_hd);
            p[max_p].fd = tcp_fd;
            p[max_p++].events = POLLIN;
        } else {
            tcp_fd = -1;
        }
        for (i = 0; i < rt->nb_rtsp_streams; i++) {
            rtsp_st = rt->rtsp_streams[i];
            if (rtsp_st->rtp_handle) {
                p[max_p].fd = ffurl_get_file_handle(rtsp_st->rtp_handle);
                p[max_p++].events = POLLIN;
                p[max_p].fd = ff_rtp_get_rtcp_file_handle(rtsp_st->rtp_handle);
                p[max_p++].events = POLLIN;
            }
        }
        n = poll(p, max_p, POLL_TIMEOUT_MS);
        if (n > 0) {
            int j = 1 - (tcp_fd == -1);
            timeout_cnt = 0;
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                rtsp_st = rt->rtsp_streams[i];
                if (rtsp_st->rtp_handle) {
                    if (p[j].revents & POLLIN || p[j+1].revents & POLLIN) {
                        ret = ffurl_read(rtsp_st->rtp_handle, buf, buf_size);
                        if (ret > 0) {
                            *prtsp_st = rtsp_st;
                            return ret;
                        }
                    }
                    j+=2;
                }
            }
#if CONFIG_RTSP_DEMUXER
            if (tcp_fd != -1 && p[0].revents & POLLIN) {
                RTSPMessageHeader reply;

                ret = ff_rtsp_read_reply(s, &reply, NULL, 0, NULL);
                if (ret < 0)
                    return ret;
                /* XXX: parse message */
                if (rt->state != RTSP_STATE_STREAMING)
                    return 0;
            }
#endif
        } else if (n == 0 && ++timeout_cnt >= MAX_TIMEOUTS) {
            return AVERROR(ETIMEDOUT);
        } else if (n < 0 && errno != EINTR)
            return AVERROR(errno);
    }
}

int ff_rtsp_fetch_packet(AVFormatContext *s, AVPacket *pkt)
{
    RTSPState *rt = s->priv_data;
    int ret, len;
    RTSPStream *rtsp_st, *first_queue_st = NULL;
    int64_t wait_end = 0;

    if (rt->nb_byes == rt->nb_rtsp_streams)
        return AVERROR_EOF;

    /* get next frames from the same RTP packet */
    if (rt->cur_transport_priv) {
        if (rt->transport == RTSP_TRANSPORT_RDT) {
            ret = ff_rdt_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
        } else
            ret = ff_rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
        if (ret == 0) {
            rt->cur_transport_priv = NULL;
            return 0;
        } else if (ret == 1) {
            return 0;
        } else
            rt->cur_transport_priv = NULL;
    }

    if (rt->transport == RTSP_TRANSPORT_RTP) {
        int i;
        int64_t first_queue_time = 0;
        for (i = 0; i < rt->nb_rtsp_streams; i++) {
            RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
            int64_t queue_time;
            if (!rtpctx)
                continue;
            queue_time = ff_rtp_queued_packet_time(rtpctx);
            if (queue_time && (queue_time - first_queue_time < 0 ||
                               !first_queue_time)) {
                first_queue_time = queue_time;
                first_queue_st   = rt->rtsp_streams[i];
            }
        }
        if (first_queue_time)
            wait_end = first_queue_time + s->max_delay;
    }

    /* read next RTP packet */
 redo:
    if (!rt->recvbuf) {
        rt->recvbuf = av_malloc(RECVBUF_SIZE);
        if (!rt->recvbuf)
            return AVERROR(ENOMEM);
    }

    switch(rt->lower_transport) {
    default:
#if CONFIG_RTSP_DEMUXER
    case RTSP_LOWER_TRANSPORT_TCP:
        len = ff_rtsp_tcp_read_packet(s, &rtsp_st, rt->recvbuf, RECVBUF_SIZE);
        break;
#endif
    case RTSP_LOWER_TRANSPORT_UDP:
    case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
        len = udp_read_packet(s, &rtsp_st, rt->recvbuf, RECVBUF_SIZE, wait_end);
        if (len > 0 && rtsp_st->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
            ff_rtp_check_and_send_back_rr(rtsp_st->transport_priv, len);
        break;
    }
    if (len == AVERROR(EAGAIN) && first_queue_st &&
        rt->transport == RTSP_TRANSPORT_RTP) {
        rtsp_st = first_queue_st;
        ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, NULL, 0);
        goto end;
    }
    if (len < 0)
        return len;
    if (len == 0)
        return AVERROR_EOF;
    if (rt->transport == RTSP_TRANSPORT_RDT) {
        ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
    } else {
        ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
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
                AVStream *st = NULL;
                if (rtsp_st->stream_index >= 0)
                    st = s->streams[rtsp_st->stream_index];
                for (i = 0; i < rt->nb_rtsp_streams; i++) {
                    RTPDemuxContext *rtpctx2 = rt->rtsp_streams[i]->transport_priv;
                    AVStream *st2 = NULL;
                    if (rt->rtsp_streams[i]->stream_index >= 0)
                        st2 = s->streams[rt->rtsp_streams[i]->stream_index];
                    if (rtpctx2 && st && st2 &&
                        rtpctx2->first_rtcp_ntp_time == AV_NOPTS_VALUE) {
                        rtpctx2->first_rtcp_ntp_time = rtpctx->first_rtcp_ntp_time;
                        rtpctx2->rtcp_ts_offset = av_rescale_q(
                            rtpctx->rtcp_ts_offset, st->time_base,
                            st2->time_base);
                    }
                }
            }
            if (ret == -RTCP_BYE) {
                rt->nb_byes++;

                av_log(s, AV_LOG_DEBUG, "Received BYE for stream %d (%d/%d)\n",
                       rtsp_st->stream_index, rt->nb_byes, rt->nb_rtsp_streams);

                if (rt->nb_byes == rt->nb_rtsp_streams)
                    return AVERROR_EOF;
            }
        }
    }
end:
    if (ret < 0)
        goto redo;
    if (ret == 1)
        /* more packets may follow, so we save the RTP context */
        rt->cur_transport_priv = rtsp_st->transport_priv;

    return ret;
}
#endif /* CONFIG_RTPDEC */

#if CONFIG_SDP_DEMUXER
static int sdp_probe(AVProbeData *p1)
{
    const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;

    /* we look for a line beginning "c=IN IP" */
    while (p < p_end && *p != '\0') {
        if (p + sizeof("c=IN IP") - 1 < p_end &&
            av_strstart(p, "c=IN IP", NULL))
            return AVPROBE_SCORE_MAX / 2;

        while (p < p_end - 1 && *p != '\n') p++;
        if (++p >= p_end)
            break;
        if (*p == '\r')
            p++;
    }
    return 0;
}

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
    size = avio_read(s->pb, content, SDP_MAX_SIZE - 1);
    if (size <= 0) {
        av_free(content);
        return AVERROR_INVALIDDATA;
    }
    content[size] ='\0';

    err = ff_sdp_parse(s, content);
    av_free(content);
    if (err) goto fail;

    /* open each RTP stream */
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        char namebuf[50];
        rtsp_st = rt->rtsp_streams[i];

        getnameinfo((struct sockaddr*) &rtsp_st->sdp_ip, sizeof(rtsp_st->sdp_ip),
                    namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
        ff_url_join(url, sizeof(url), "rtp", NULL,
                    namebuf, rtsp_st->sdp_port,
                    "?localport=%d&ttl=%d&connect=%d", rtsp_st->sdp_port,
                    rtsp_st->sdp_ttl,
                    rt->rtsp_flags & RTSP_FLAG_FILTER_SRC ? 1 : 0);
        if (ffurl_open(&rtsp_st->rtp_handle, url, AVIO_FLAG_READ_WRITE,
                       &s->interrupt_callback, NULL) < 0) {
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

static const AVClass sdp_demuxer_class = {
    .class_name     = "SDP demuxer",
    .item_name      = av_default_item_name,
    .option         = sdp_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_sdp_demuxer = {
    .name           = "sdp",
    .long_name      = NULL_IF_CONFIG_SMALL("SDP"),
    .priv_data_size = sizeof(RTSPState),
    .read_probe     = sdp_probe,
    .read_header    = sdp_read_header,
    .read_packet    = ff_rtsp_fetch_packet,
    .read_close     = sdp_read_close,
    .priv_class     = &sdp_demuxer_class
};
#endif /* CONFIG_SDP_DEMUXER */

#if CONFIG_RTP_DEMUXER
static int rtp_probe(AVProbeData *p)
{
    if (av_strstart(p->filename, "rtp:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int rtp_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    uint8_t recvbuf[1500];
    char host[500], sdp[500];
    int ret, port;
    URLContext* in = NULL;
    int payload_type;
    AVCodecContext codec;
    struct sockaddr_storage addr;
    AVIOContext pb;
    socklen_t addrlen = sizeof(addr);
    RTSPState *rt = s->priv_data;

    if (!ff_network_init())
        return AVERROR(EIO);

    ret = ffurl_open(&in, s->filename, AVIO_FLAG_READ,
                     &s->interrupt_callback, NULL);
    if (ret)
        goto fail;

    while (1) {
        ret = ffurl_read(in, recvbuf, sizeof(recvbuf));
        if (ret == AVERROR(EAGAIN))
            continue;
        if (ret < 0)
            goto fail;
        if (ret < 12) {
            av_log(s, AV_LOG_WARNING, "Received too short packet\n");
            continue;
        }

        if ((recvbuf[0] & 0xc0) != 0x80) {
            av_log(s, AV_LOG_WARNING, "Unsupported RTP version packet "
                                      "received\n");
            continue;
        }

        payload_type = recvbuf[1] & 0x7f;
        break;
    }
    getsockname(ffurl_get_file_handle(in), (struct sockaddr*) &addr, &addrlen);
    ffurl_close(in);
    in = NULL;

    memset(&codec, 0, sizeof(codec));
    if (ff_rtp_get_codec_info(&codec, payload_type)) {
        av_log(s, AV_LOG_ERROR, "Unable to receive RTP payload type %d "
                                "without an SDP file describing it\n",
                                 payload_type);
        goto fail;
    }
    if (codec.codec_type != AVMEDIA_TYPE_DATA) {
        av_log(s, AV_LOG_WARNING, "Guessing on RTP content - if not received "
                                  "properly you need an SDP file "
                                  "describing it\n");
    }

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port,
                 NULL, 0, s->filename);

    snprintf(sdp, sizeof(sdp),
             "v=0\r\nc=IN IP%d %s\r\nm=%s %d RTP/AVP %d\r\n",
             addr.ss_family == AF_INET ? 4 : 6, host,
             codec.codec_type == AVMEDIA_TYPE_DATA  ? "application" :
             codec.codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
             port, payload_type);
    av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", sdp);

    ffio_init_context(&pb, sdp, strlen(sdp), 0, NULL, NULL, NULL, NULL);
    s->pb = &pb;

    /* sdp_read_header initializes this again */
    ff_network_close();

    rt->media_type_mask = (1 << (AVMEDIA_TYPE_DATA+1)) - 1;

    ret = sdp_read_header(s, ap);
    s->pb = NULL;
    return ret;

fail:
    if (in)
        ffurl_close(in);
    ff_network_close();
    return ret;
}

static const AVClass rtp_demuxer_class = {
    .class_name     = "RTP demuxer",
    .item_name      = av_default_item_name,
    .option         = rtp_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_rtp_demuxer = {
    .name           = "rtp",
    .long_name      = NULL_IF_CONFIG_SMALL("RTP input format"),
    .priv_data_size = sizeof(RTSPState),
    .read_probe     = rtp_probe,
    .read_header    = rtp_read_header,
    .read_packet    = ff_rtsp_fetch_packet,
    .read_close     = sdp_read_close,
    .flags = AVFMT_NOFILE,
    .priv_class     = &rtp_demuxer_class
};
#endif /* CONFIG_RTP_DEMUXER */

