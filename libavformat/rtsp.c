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

#include "libavutil/avassert.h"
#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avformat.h"
#include "avio_internal.h"

#if HAVE_POLL_H
#include <poll.h>
#endif
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "http.h"
#include "rtsp.h"

#include "rtpdec.h"
#include "rtpproto.h"
#include "rdt.h"
#include "rtpdec_formats.h"
#include "rtpenc_chain.h"
#include "url.h"
#include "rtpenc.h"
#include "mpegts.h"

/* Timeout values for socket poll, in ms,
 * and read_packet(), in seconds  */
#define POLL_TIMEOUT_MS 100
#define READ_PACKET_TIMEOUT_S 10
#define MAX_TIMEOUTS READ_PACKET_TIMEOUT_S * 1000 / POLL_TIMEOUT_MS
#define SDP_MAX_SIZE 16384
#define RECVBUF_SIZE 10 * RTP_MAX_PACKET_LENGTH
#define DEFAULT_REORDERING_DELAY 100000

#define OFFSET(x) offsetof(RTSPState, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define ENC AV_OPT_FLAG_ENCODING_PARAM

#define RTSP_FLAG_OPTS(name, longname) \
    { name, longname, OFFSET(rtsp_flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtsp_flags" }, \
    { "filter_src", "only receive packets from the negotiated peer IP", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_FILTER_SRC}, 0, 0, DEC, "rtsp_flags" }

#define RTSP_MEDIATYPE_OPTS(name, longname) \
    { name, longname, OFFSET(media_type_mask), AV_OPT_TYPE_FLAGS, { .i64 = (1 << (AVMEDIA_TYPE_SUBTITLE+1)) - 1 }, INT_MIN, INT_MAX, DEC, "allowed_media_types" }, \
    { "video", "Video", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << AVMEDIA_TYPE_VIDEO}, 0, 0, DEC, "allowed_media_types" }, \
    { "audio", "Audio", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << AVMEDIA_TYPE_AUDIO}, 0, 0, DEC, "allowed_media_types" }, \
    { "data", "Data", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << AVMEDIA_TYPE_DATA}, 0, 0, DEC, "allowed_media_types" }, \
    { "subtitle", "Subtitle", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << AVMEDIA_TYPE_SUBTITLE}, 0, 0, DEC, "allowed_media_types" }

#define COMMON_OPTS() \
    { "reorder_queue_size", "set number of packets to buffer for handling of reordered packets", OFFSET(reordering_queue_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC }, \
    { "buffer_size",        "Underlying protocol send/receive buffer size",                  OFFSET(buffer_size),           AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC|ENC } \


const AVOption ff_rtsp_options[] = {
    { "initial_pause",  "do not start playing the stream immediately", OFFSET(initial_pause), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    FF_RTP_FLAG_OPTS(RTSPState, rtp_muxer_flags),
    { "rtsp_transport", "set RTSP transport protocols", OFFSET(lower_transport_mask), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, DEC|ENC, "rtsp_transport" }, \
    { "udp", "UDP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
    { "tcp", "TCP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_TCP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
    { "udp_multicast", "UDP multicast", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP_MULTICAST}, 0, 0, DEC, "rtsp_transport" },
    { "http", "HTTP tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTP)}, 0, 0, DEC, "rtsp_transport" },
    { "https", "HTTPS tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTPS )}, 0, 0, DEC, "rtsp_transport" },
    RTSP_FLAG_OPTS("rtsp_flags", "set RTSP flags"),
    { "listen", "wait for incoming connections", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_LISTEN}, 0, 0, DEC, "rtsp_flags" },
    { "prefer_tcp", "try RTP via TCP first, if available", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_PREFER_TCP}, 0, 0, DEC|ENC, "rtsp_flags" },
    RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
    { "min_port", "set minimum local UDP port", OFFSET(rtp_port_min), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MIN}, 0, 65535, DEC|ENC },
    { "max_port", "set maximum local UDP port", OFFSET(rtp_port_max), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MAX}, 0, 65535, DEC|ENC },
    { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections (-1 is infinite, imply flag listen)", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC },
#if FF_API_OLD_RTSP_OPTIONS
    { "timeout", "set maximum timeout (in seconds) to wait for incoming connections (-1 is infinite, imply flag listen) (deprecated, use listen_timeout)", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC },
    { "stimeout", "set timeout (in microseconds) of socket TCP I/O operations", OFFSET(stimeout), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC },
#else
    { "timeout", "set timeout (in microseconds) of socket TCP I/O operations", OFFSET(stimeout), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC },
#endif
    COMMON_OPTS(),
    { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT}, 0, 0, DEC },
#if FF_API_OLD_RTSP_OPTIONS
    { "user-agent", "override User-Agent header (deprecated, use user_agent)", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT}, 0, 0, DEC },
#endif
    { NULL },
};

static const AVOption sdp_options[] = {
    RTSP_FLAG_OPTS("sdp_flags", "SDP flags"),
    { "custom_io", "use custom I/O", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_CUSTOM_IO}, 0, 0, DEC, "rtsp_flags" },
    { "rtcp_to_source", "send RTCP packets to the source address of received packets", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_RTCP_TO_SOURCE}, 0, 0, DEC, "rtsp_flags" },
    RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
    COMMON_OPTS(),
    { NULL },
};

static const AVOption rtp_options[] = {
    RTSP_FLAG_OPTS("rtp_flags", "set RTP flags"),
    COMMON_OPTS(),
    { NULL },
};


static AVDictionary *map_to_opts(RTSPState *rt)
{
    AVDictionary *opts = NULL;
    char buf[256];

    snprintf(buf, sizeof(buf), "%d", rt->buffer_size);
    av_dict_set(&opts, "buffer_size", buf, 0);

    return opts;
}

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
    if (av_parse_time(start, buf, 1) < 0)
        return;
    if (*p == '-') {
        p++;
        get_word_sep(buf, sizeof(buf), "-", &p);
        if (av_parse_time(end, buf, 1) < 0)
            av_log(NULL, AV_LOG_DEBUG, "Failed to parse interval end specification '%s'\n", buf);
    }
}

static int get_sockaddr(AVFormatContext *s,
                        const char *buf, struct sockaddr_storage *sock)
{
    struct addrinfo hints = { 0 }, *ai = NULL;
    int ret;

    hints.ai_flags = AI_NUMERICHOST;
    if ((ret = getaddrinfo(buf, NULL, &hints, &ai))) {
        av_log(s, AV_LOG_ERROR, "getaddrinfo(%s): %s\n",
               buf,
               gai_strerror(ret));
        return -1;
    }
    memcpy(sock, ai->ai_addr, FFMIN(sizeof(*sock), ai->ai_addrlen));
    freeaddrinfo(ai);
    return 0;
}

#if CONFIG_RTPDEC
static void init_rtp_handler(const RTPDynamicProtocolHandler *handler,
                             RTSPStream *rtsp_st, AVStream *st)
{
    AVCodecParameters *par = st ? st->codecpar : NULL;
    if (!handler)
        return;
    if (par)
        par->codec_id          = handler->codec_id;
    rtsp_st->dynamic_handler = handler;
    if (st)
        st->need_parsing = handler->need_parsing;
    if (handler->priv_data_size) {
        rtsp_st->dynamic_protocol_context = av_mallocz(handler->priv_data_size);
        if (!rtsp_st->dynamic_protocol_context)
            rtsp_st->dynamic_handler = NULL;
    }
}

static void finalize_rtp_handler_init(AVFormatContext *s, RTSPStream *rtsp_st,
                                      AVStream *st)
{
    if (rtsp_st->dynamic_handler && rtsp_st->dynamic_handler->init) {
        int ret = rtsp_st->dynamic_handler->init(s, st ? st->index : -1,
                                                 rtsp_st->dynamic_protocol_context);
        if (ret < 0) {
            if (rtsp_st->dynamic_protocol_context) {
                if (rtsp_st->dynamic_handler->close)
                    rtsp_st->dynamic_handler->close(
                        rtsp_st->dynamic_protocol_context);
                av_free(rtsp_st->dynamic_protocol_context);
            }
            rtsp_st->dynamic_protocol_context = NULL;
            rtsp_st->dynamic_handler = NULL;
        }
    }
}

/* parse the rtpmap description: <codec_name>/<clock_rate>[/<other params>] */
static int sdp_parse_rtpmap(AVFormatContext *s,
                            AVStream *st, RTSPStream *rtsp_st,
                            int payload_type, const char *p)
{
    AVCodecParameters *par = st->codecpar;
    char buf[256];
    int i;
    const AVCodecDescriptor *desc;
    const char *c_name;

    /* See if we can handle this kind of payload.
     * The space should normally not be there but some Real streams or
     * particular servers ("RealServer Version 6.1.3.970", see issue 1658)
     * have a trailing space. */
    get_word_sep(buf, sizeof(buf), "/ ", &p);
    if (payload_type < RTP_PT_PRIVATE) {
        /* We are in a standard case
         * (from http://www.iana.org/assignments/rtp-parameters). */
        par->codec_id = ff_rtp_codec_id(buf, par->codec_type);
    }

    if (par->codec_id == AV_CODEC_ID_NONE) {
        const RTPDynamicProtocolHandler *handler =
            ff_rtp_handler_find_by_name(buf, par->codec_type);
        init_rtp_handler(handler, rtsp_st, st);
        /* If no dynamic handler was found, check with the list of standard
         * allocated types, if such a stream for some reason happens to
         * use a private payload type. This isn't handled in rtpdec.c, since
         * the format name from the rtpmap line never is passed into rtpdec. */
        if (!rtsp_st->dynamic_handler)
            par->codec_id = ff_rtp_codec_id(buf, par->codec_type);
    }

    desc = avcodec_descriptor_get(par->codec_id);
    if (desc && desc->name)
        c_name = desc->name;
    else
        c_name = "(null)";

    get_word_sep(buf, sizeof(buf), "/", &p);
    i = atoi(buf);
    switch (par->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_log(s, AV_LOG_DEBUG, "audio codec set to: %s\n", c_name);
        par->sample_rate = RTSP_DEFAULT_AUDIO_SAMPLERATE;
        par->channels = RTSP_DEFAULT_NB_AUDIO_CHANNELS;
        if (i > 0) {
            par->sample_rate = i;
            avpriv_set_pts_info(st, 32, 1, par->sample_rate);
            get_word_sep(buf, sizeof(buf), "/", &p);
            i = atoi(buf);
            if (i > 0)
                par->channels = i;
        }
        av_log(s, AV_LOG_DEBUG, "audio samplerate set to: %i\n",
               par->sample_rate);
        av_log(s, AV_LOG_DEBUG, "audio channels set to: %i\n",
               par->channels);
        break;
    case AVMEDIA_TYPE_VIDEO:
        av_log(s, AV_LOG_DEBUG, "video codec set to: %s\n", c_name);
        if (i > 0)
            avpriv_set_pts_info(st, 32, 1, i);
        break;
    default:
        break;
    }
    finalize_rtp_handler_init(s, rtsp_st, st);
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
    int nb_default_include_source_addrs; /**< Number of source-specific multicast include source IP address (from SDP content) */
    struct RTSPSource **default_include_source_addrs; /**< Source-specific multicast include source IP address (from SDP content) */
    int nb_default_exclude_source_addrs; /**< Number of source-specific multicast exclude source IP address (from SDP content) */
    struct RTSPSource **default_exclude_source_addrs; /**< Source-specific multicast exclude source IP address (from SDP content) */
    int seen_rtpmap;
    int seen_fmtp;
    char delayed_fmtp[2048];
} SDPParseState;

static void copy_default_source_addrs(struct RTSPSource **addrs, int count,
                                      struct RTSPSource ***dest, int *dest_count)
{
    RTSPSource *rtsp_src, *rtsp_src2;
    int i;
    for (i = 0; i < count; i++) {
        rtsp_src = addrs[i];
        rtsp_src2 = av_malloc(sizeof(*rtsp_src2));
        if (!rtsp_src2)
            continue;
        memcpy(rtsp_src2, rtsp_src, sizeof(*rtsp_src));
        dynarray_add(dest, dest_count, rtsp_src2);
    }
}

static void parse_fmtp(AVFormatContext *s, RTSPState *rt,
                       int payload_type, const char *line)
{
    int i;

    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        RTSPStream *rtsp_st = rt->rtsp_streams[i];
        if (rtsp_st->sdp_payload_type == payload_type &&
            rtsp_st->dynamic_handler &&
            rtsp_st->dynamic_handler->parse_sdp_a_line) {
            rtsp_st->dynamic_handler->parse_sdp_a_line(s, i,
                rtsp_st->dynamic_protocol_context, line);
        }
    }
}

static void sdp_parse_line(AVFormatContext *s, SDPParseState *s1,
                           int letter, const char *buf)
{
    RTSPState *rt = s->priv_data;
    char buf1[64], st_type[64];
    const char *p;
    enum AVMediaType codec_type;
    int payload_type;
    AVStream *st;
    RTSPStream *rtsp_st;
    RTSPSource *rtsp_src;
    struct sockaddr_storage sdp_ip;
    int ttl;

    av_log(s, AV_LOG_TRACE, "sdp: %c='%s'\n", letter, buf);

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
        if (get_sockaddr(s, buf1, &sdp_ip))
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
        s1->skip_media  = 0;
        s1->seen_fmtp   = 0;
        s1->seen_rtpmap = 0;
        codec_type = AVMEDIA_TYPE_UNKNOWN;
        get_word(st_type, sizeof(st_type), &p);
        if (!strcmp(st_type, "audio")) {
            codec_type = AVMEDIA_TYPE_AUDIO;
        } else if (!strcmp(st_type, "video")) {
            codec_type = AVMEDIA_TYPE_VIDEO;
        } else if (!strcmp(st_type, "application")) {
            codec_type = AVMEDIA_TYPE_DATA;
        } else if (!strcmp(st_type, "text")) {
            codec_type = AVMEDIA_TYPE_SUBTITLE;
        }
        if (codec_type == AVMEDIA_TYPE_UNKNOWN ||
            !(rt->media_type_mask & (1 << codec_type)) ||
            rt->nb_rtsp_streams >= s->max_streams
        ) {
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

        copy_default_source_addrs(s1->default_include_source_addrs,
                                  s1->nb_default_include_source_addrs,
                                  &rtsp_st->include_source_addrs,
                                  &rtsp_st->nb_include_source_addrs);
        copy_default_source_addrs(s1->default_exclude_source_addrs,
                                  s1->nb_default_exclude_source_addrs,
                                  &rtsp_st->exclude_source_addrs,
                                  &rtsp_st->nb_exclude_source_addrs);

        get_word(buf1, sizeof(buf1), &p); /* port */
        rtsp_st->sdp_port = atoi(buf1);

        get_word(buf1, sizeof(buf1), &p); /* protocol */
        if (!strcmp(buf1, "udp"))
            rt->transport = RTSP_TRANSPORT_RAW;
        else if (strstr(buf1, "/AVPF") || strstr(buf1, "/SAVPF"))
            rtsp_st->feedback = 1;

        /* XXX: handle list of formats */
        get_word(buf1, sizeof(buf1), &p); /* format list */
        rtsp_st->sdp_payload_type = atoi(buf1);

        if (!strcmp(ff_rtp_enc_name(rtsp_st->sdp_payload_type), "MP2T")) {
            /* no corresponding stream */
            if (rt->transport == RTSP_TRANSPORT_RAW) {
                if (CONFIG_RTPDEC && !rt->ts)
                    rt->ts = avpriv_mpegts_parse_open(s);
            } else {
                const RTPDynamicProtocolHandler *handler;
                handler = ff_rtp_handler_find_by_id(
                              rtsp_st->sdp_payload_type, AVMEDIA_TYPE_DATA);
                init_rtp_handler(handler, rtsp_st, NULL);
                finalize_rtp_handler_init(s, rtsp_st, NULL);
            }
        } else if (rt->server_type == RTSP_SERVER_WMS &&
                   codec_type == AVMEDIA_TYPE_DATA) {
            /* RTX stream, a stream that carries all the other actual
             * audio/video streams. Don't expose this to the callers. */
        } else {
            st = avformat_new_stream(s, NULL);
            if (!st)
                return;
            st->id = rt->nb_rtsp_streams - 1;
            rtsp_st->stream_index = st->index;
            st->codecpar->codec_type = codec_type;
            if (rtsp_st->sdp_payload_type < RTP_PT_PRIVATE) {
                const RTPDynamicProtocolHandler *handler;
                /* if standard payload type, we can find the codec right now */
                ff_rtp_get_codec_info(st->codecpar, rtsp_st->sdp_payload_type);
                if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                    st->codecpar->sample_rate > 0)
                    avpriv_set_pts_info(st, 32, 1, st->codecpar->sample_rate);
                /* Even static payload types may need a custom depacketizer */
                handler = ff_rtp_handler_find_by_id(
                              rtsp_st->sdp_payload_type, st->codecpar->codec_type);
                init_rtp_handler(handler, rtsp_st, st);
                finalize_rtp_handler_init(s, rtsp_st, st);
            }
            if (rt->default_lang[0])
                av_dict_set(&st->metadata, "language", rt->default_lang, 0);
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
            rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
            if (rtsp_st->stream_index >= 0) {
                st = s->streams[rtsp_st->stream_index];
                sdp_parse_rtpmap(s, st, rtsp_st, payload_type, p);
            }
            s1->seen_rtpmap = 1;
            if (s1->seen_fmtp) {
                parse_fmtp(s, rt, payload_type, s1->delayed_fmtp);
            }
        } else if (av_strstart(p, "fmtp:", &p) ||
                   av_strstart(p, "framesize:", &p)) {
            // let dynamic protocol handlers have a stab at the line.
            get_word(buf1, sizeof(buf1), &p);
            payload_type = atoi(buf1);
            if (s1->seen_rtpmap) {
                parse_fmtp(s, rt, payload_type, buf);
            } else {
                s1->seen_fmtp = 1;
                av_strlcpy(s1->delayed_fmtp, buf, sizeof(s1->delayed_fmtp));
            }
        } else if (av_strstart(p, "ssrc:", &p) && s->nb_streams > 0) {
            rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
            get_word(buf1, sizeof(buf1), &p);
            rtsp_st->ssrc = strtoll(buf1, NULL, 10);
        } else if (av_strstart(p, "range:", &p)) {
            int64_t start, end;

            // this is so that seeking on a streamed file can work.
            rtsp_parse_range_npt(p, &start, &end);
            s->start_time = start;
            /* AV_NOPTS_VALUE means live broadcast (and can't seek) */
            s->duration   = (end == AV_NOPTS_VALUE) ?
                            AV_NOPTS_VALUE : end - start;
        } else if (av_strstart(p, "lang:", &p)) {
            if (s->nb_streams > 0) {
                get_word(buf1, sizeof(buf1), &p);
                rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
                if (rtsp_st->stream_index >= 0) {
                    st = s->streams[rtsp_st->stream_index];
                    av_dict_set(&st->metadata, "language", buf1, 0);
                }
            } else
                get_word(rt->default_lang, sizeof(rt->default_lang), &p);
        } else if (av_strstart(p, "IsRealDataType:integer;",&p)) {
            if (atoi(p) == 1)
                rt->transport = RTSP_TRANSPORT_RDT;
        } else if (av_strstart(p, "SampleRate:integer;", &p) &&
                   s->nb_streams > 0) {
            st = s->streams[s->nb_streams - 1];
            st->codecpar->sample_rate = atoi(p);
        } else if (av_strstart(p, "crypto:", &p) && s->nb_streams > 0) {
            // RFC 4568
            rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
            get_word(buf1, sizeof(buf1), &p); // ignore tag
            get_word(rtsp_st->crypto_suite, sizeof(rtsp_st->crypto_suite), &p);
            p += strspn(p, SPACE_CHARS);
            if (av_strstart(p, "inline:", &p))
                get_word(rtsp_st->crypto_params, sizeof(rtsp_st->crypto_params), &p);
        } else if (av_strstart(p, "source-filter:", &p)) {
            int exclude = 0;
            get_word(buf1, sizeof(buf1), &p);
            if (strcmp(buf1, "incl") && strcmp(buf1, "excl"))
                return;
            exclude = !strcmp(buf1, "excl");

            get_word(buf1, sizeof(buf1), &p);
            if (strcmp(buf1, "IN") != 0)
                return;
            get_word(buf1, sizeof(buf1), &p);
            if (strcmp(buf1, "IP4") && strcmp(buf1, "IP6") && strcmp(buf1, "*"))
                return;
            // not checking that the destination address actually matches or is wildcard
            get_word(buf1, sizeof(buf1), &p);

            while (*p != '\0') {
                rtsp_src = av_mallocz(sizeof(*rtsp_src));
                if (!rtsp_src)
                    return;
                get_word(rtsp_src->addr, sizeof(rtsp_src->addr), &p);
                if (exclude) {
                    if (s->nb_streams == 0) {
                        dynarray_add(&s1->default_exclude_source_addrs, &s1->nb_default_exclude_source_addrs, rtsp_src);
                    } else {
                        rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
                        dynarray_add(&rtsp_st->exclude_source_addrs, &rtsp_st->nb_exclude_source_addrs, rtsp_src);
                    }
                } else {
                    if (s->nb_streams == 0) {
                        dynarray_add(&s1->default_include_source_addrs, &s1->nb_default_include_source_addrs, rtsp_src);
                    } else {
                        rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
                        dynarray_add(&rtsp_st->include_source_addrs, &rtsp_st->nb_include_source_addrs, rtsp_src);
                    }
                }
            }
        } else {
            if (rt->server_type == RTSP_SERVER_WMS)
                ff_wms_parse_sdp_a_line(s, p);
            if (s->nb_streams > 0) {
                rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];

                if (rt->server_type == RTSP_SERVER_REAL)
                    ff_real_parse_sdp_a_line(s, rtsp_st->stream_index, p);

                if (rtsp_st->dynamic_handler &&
                    rtsp_st->dynamic_handler->parse_sdp_a_line)
                    rtsp_st->dynamic_handler->parse_sdp_a_line(s,
                        rtsp_st->stream_index,
                        rtsp_st->dynamic_protocol_context, buf);
            }
        }
        break;
    }
}

int ff_sdp_parse(AVFormatContext *s, const char *content)
{
    const char *p;
    int letter, i;
    /* Some SDP lines, particularly for Realmedia or ASF RTSP streams,
     * contain long SDP lines containing complete ASF Headers (several
     * kB) or arrays of MDPR (RM stream descriptor) headers plus
     * "rulebooks" describing their properties. Therefore, the SDP line
     * buffer is large.
     *
     * The Vorbis FMTP line can be up to 16KB - see xiph_parse_sdp_line
     * in rtpdec_xiph.c. */
    char buf[16384], *q;
    SDPParseState sdp_parse_state = { { 0 } }, *s1 = &sdp_parse_state;

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

    for (i = 0; i < s1->nb_default_include_source_addrs; i++)
        av_freep(&s1->default_include_source_addrs[i]);
    av_freep(&s1->default_include_source_addrs);
    for (i = 0; i < s1->nb_default_exclude_source_addrs; i++)
        av_freep(&s1->default_exclude_source_addrs[i]);
    av_freep(&s1->default_exclude_source_addrs);

    return 0;
}
#endif /* CONFIG_RTPDEC */

void ff_rtsp_undo_setup(AVFormatContext *s, int send_packets)
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
                    if (CONFIG_RTSP_MUXER && rtpctx->pb && send_packets)
                        ff_rtsp_tcp_write_packet(s, rtsp_st);
                    ffio_free_dyn_buf(&rtpctx->pb);
                } else {
                    avio_closep(&rtpctx->pb);
                }
                avformat_free_context(rtpctx);
            } else if (CONFIG_RTPDEC && rt->transport == RTSP_TRANSPORT_RDT)
                ff_rdt_parse_close(rtsp_st->transport_priv);
            else if (CONFIG_RTPDEC && rt->transport == RTSP_TRANSPORT_RTP)
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
    int i, j;
    RTSPStream *rtsp_st;

    ff_rtsp_undo_setup(s, 0);
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        rtsp_st = rt->rtsp_streams[i];
        if (rtsp_st) {
            if (rtsp_st->dynamic_handler && rtsp_st->dynamic_protocol_context) {
                if (rtsp_st->dynamic_handler->close)
                    rtsp_st->dynamic_handler->close(
                        rtsp_st->dynamic_protocol_context);
                av_free(rtsp_st->dynamic_protocol_context);
            }
            for (j = 0; j < rtsp_st->nb_include_source_addrs; j++)
                av_freep(&rtsp_st->include_source_addrs[j]);
            av_freep(&rtsp_st->include_source_addrs);
            for (j = 0; j < rtsp_st->nb_exclude_source_addrs; j++)
                av_freep(&rtsp_st->exclude_source_addrs[j]);
            av_freep(&rtsp_st->exclude_source_addrs);

            av_freep(&rtsp_st);
        }
    }
    av_freep(&rt->rtsp_streams);
    if (rt->asf_ctx) {
        avformat_close_input(&rt->asf_ctx);
    }
    if (CONFIG_RTPDEC && rt->ts)
        avpriv_mpegts_parse_close(rt->ts);
    av_freep(&rt->p);
    av_freep(&rt->recvbuf);
}

int ff_rtsp_open_transport_ctx(AVFormatContext *s, RTSPStream *rtsp_st)
{
    RTSPState *rt = s->priv_data;
    AVStream *st = NULL;
    int reordering_queue_size = rt->reordering_queue_size;
    if (reordering_queue_size < 0) {
        if (rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP || !s->max_delay)
            reordering_queue_size = 0;
        else
            reordering_queue_size = RTP_REORDER_QUEUE_DEFAULT_SIZE;
    }

    /* open the RTP context */
    if (rtsp_st->stream_index >= 0)
        st = s->streams[rtsp_st->stream_index];
    if (!st)
        s->ctx_flags |= AVFMTCTX_NOHEADER;

    if (CONFIG_RTSP_MUXER && s->oformat && st) {
        int ret = ff_rtp_chain_mux_open((AVFormatContext **)&rtsp_st->transport_priv,
                                        s, st, rtsp_st->rtp_handle,
                                        RTSP_TCP_MAX_PACKET_SIZE,
                                        rtsp_st->stream_index);
        /* Ownership of rtp_handle is passed to the rtp mux context */
        rtsp_st->rtp_handle = NULL;
        if (ret < 0)
            return ret;
        st->time_base = ((AVFormatContext*)rtsp_st->transport_priv)->streams[0]->time_base;
    } else if (rt->transport == RTSP_TRANSPORT_RAW) {
        return 0; // Don't need to open any parser here
    } else if (CONFIG_RTPDEC && rt->transport == RTSP_TRANSPORT_RDT && st)
        rtsp_st->transport_priv = ff_rdt_parse_open(s, st->index,
                                            rtsp_st->dynamic_protocol_context,
                                            rtsp_st->dynamic_handler);
    else if (CONFIG_RTPDEC)
        rtsp_st->transport_priv = ff_rtp_parse_open(s, st,
                                         rtsp_st->sdp_payload_type,
                                         reordering_queue_size);

    if (!rtsp_st->transport_priv) {
         return AVERROR(ENOMEM);
    } else if (CONFIG_RTPDEC && rt->transport == RTSP_TRANSPORT_RTP &&
               s->iformat) {
        RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
        rtpctx->ssrc = rtsp_st->ssrc;
        if (rtsp_st->dynamic_handler) {
            ff_rtp_parse_set_dynamic_protocol(rtsp_st->transport_priv,
                                              rtsp_st->dynamic_protocol_context,
                                              rtsp_st->dynamic_handler);
        }
        if (rtsp_st->crypto_suite[0])
            ff_rtp_parse_set_crypto(rtsp_st->transport_priv,
                                    rtsp_st->crypto_suite,
                                    rtsp_st->crypto_params);
    }

    return 0;
}

#if CONFIG_RTSP_DEMUXER || CONFIG_RTSP_MUXER
static void rtsp_parse_range(int *min_ptr, int *max_ptr, const char **pp)
{
    const char *q;
    char *p;
    int v;

    q = *pp;
    q += strspn(q, SPACE_CHARS);
    v = strtol(q, &p, 10);
    if (*p == '-') {
        p++;
        *min_ptr = v;
        v = strtol(p, &p, 10);
        *max_ptr = v;
    } else {
        *min_ptr = v;
        *max_ptr = v;
    }
    *pp = p;
}

/* XXX: only one transport specification is parsed */
static void rtsp_parse_transport(AVFormatContext *s,
                                 RTSPMessageHeader *reply, const char *p)
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
        } else if (!av_strcasecmp(transport_protocol, "raw")) {
            get_word_sep(profile, sizeof(profile), "/;,", &p);
            lower_transport[0] = '\0';
            /* raw/raw/<protocol> */
            if (*p == '/') {
                get_word_sep(lower_transport, sizeof(lower_transport),
                             ";,", &p);
            }
            th->transport = RTSP_TRANSPORT_RAW;
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
                    char *end;
                    p++;
                    th->ttl = strtol(p, &end, 10);
                    p = end;
                }
            } else if (!strcmp(parameter, "destination")) {
                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";,", &p);
                    get_sockaddr(s, buf, &th->destination);
                }
            } else if (!strcmp(parameter, "source")) {
                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";,", &p);
                    av_strlcpy(th->source, buf, sizeof(th->source));
                }
            } else if (!strcmp(parameter, "mode")) {
                if (*p == '=') {
                    p++;
                    get_word_sep(buf, sizeof(buf), ";, ", &p);
                    if (!strcmp(buf, "record") ||
                        !strcmp(buf, "receive"))
                        th->mode_record = 1;
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
        if (reply->nb_transports >= RTSP_MAX_TRANSPORTS)
            break;
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

void ff_rtsp_parse_line(AVFormatContext *s,
                        RTSPMessageHeader *reply, const char *buf,
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
        rtsp_parse_transport(s, reply, p);
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
    } else if (av_stristart(p, "Content-Type:", &p)) {
        p += strspn(p, SPACE_CHARS);
        av_strlcpy(reply->content_type, p, sizeof(reply->content_type));
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

    av_log(s, AV_LOG_TRACE, "skipping RTP packet len=%d\n", len);

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
    int ret, content_length, line_count = 0, request = 0;
    unsigned char *content = NULL;

start:
    line_count = 0;
    request = 0;
    content = NULL;
    memset(reply, 0, sizeof(*reply));

    /* parse reply (XXX: use buffers) */
    rt->last_reply[0] = '\0';
    for (;;) {
        q = buf;
        for (;;) {
            ret = ffurl_read_complete(rt->rtsp_hd, &ch, 1);
            av_log(s, AV_LOG_TRACE, "ret=%d c=%02x [%c]\n", ret, ch, ch);
            if (ret != 1)
                return AVERROR_EOF;
            if (ch == '\n')
                break;
            if (ch == '$' && q == buf) {
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

        av_log(s, AV_LOG_TRACE, "line='%s'\n", buf);

        /* test if last line */
        if (buf[0] == '\0')
            break;
        p = buf;
        if (line_count == 0) {
            /* get reply code */
            get_word(buf1, sizeof(buf1), &p);
            if (!strncmp(buf1, "RTSP/", 5)) {
                get_word(buf1, sizeof(buf1), &p);
                reply->status_code = atoi(buf1);
                av_strlcpy(reply->reason, p, sizeof(reply->reason));
            } else {
                av_strlcpy(reply->reason, buf1, sizeof(reply->reason)); // method
                get_word(buf1, sizeof(buf1), &p); // object
                request = 1;
            }
        } else {
            ff_rtsp_parse_line(s, reply, p, rt, method);
            av_strlcat(rt->last_reply, p,    sizeof(rt->last_reply));
            av_strlcat(rt->last_reply, "\n", sizeof(rt->last_reply));
        }
        line_count++;
    }

    if (rt->session_id[0] == '\0' && reply->session_id[0] != '\0' && !request)
        av_strlcpy(rt->session_id, reply->session_id, sizeof(rt->session_id));

    content_length = reply->content_length;
    if (content_length > 0) {
        /* leave some room for a trailing '\0' (useful for simple parsing) */
        content = av_malloc(content_length + 1);
        if (!content)
            return AVERROR(ENOMEM);
        ffurl_read_complete(rt->rtsp_hd, content, content_length);
        content[content_length] = '\0';
    }
    if (content_ptr)
        *content_ptr = content;
    else
        av_freep(&content);

    if (request) {
        char buf[1024];
        char base64buf[AV_BASE64_SIZE(sizeof(buf))];
        const char* ptr = buf;

        if (!strcmp(reply->reason, "OPTIONS")) {
            snprintf(buf, sizeof(buf), "RTSP/1.0 200 OK\r\n");
            if (reply->seq)
                av_strlcatf(buf, sizeof(buf), "CSeq: %d\r\n", reply->seq);
            if (reply->session_id[0])
                av_strlcatf(buf, sizeof(buf), "Session: %s\r\n",
                                              reply->session_id);
        } else {
            snprintf(buf, sizeof(buf), "RTSP/1.0 501 Not Implemented\r\n");
        }
        av_strlcat(buf, "\r\n", sizeof(buf));

        if (rt->control_transport == RTSP_MODE_TUNNEL) {
            av_base64_encode(base64buf, sizeof(base64buf), buf, strlen(buf));
            ptr = base64buf;
        }
        ffurl_write(rt->rtsp_hd_out, ptr, strlen(ptr));

        rt->last_cmd_time = av_gettime_relative();
        /* Even if the request from the server had data, it is not the data
         * that the caller wants or expects. The memory could also be leaked
         * if the actual following reply has content data. */
        if (content_ptr)
            av_freep(content_ptr);
        /* If method is set, this is called from ff_rtsp_send_cmd,
         * where a reply to exactly this request is awaited. For
         * callers from within packet receiving, we just want to
         * return to the caller and go back to receiving packets. */
        if (method)
            goto start;
        return 0;
    }

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
static int rtsp_send_cmd_with_content_async(AVFormatContext *s,
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
    av_strlcatf(buf, sizeof(buf), "User-Agent: %s\r\n",  rt->user_agent);
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

    av_log(s, AV_LOG_TRACE, "Sending:\n%s--\n", buf);

    ffurl_write(rt->rtsp_hd_out, out_buf, strlen(out_buf));
    if (send_content_length > 0 && send_content) {
        if (rt->control_transport == RTSP_MODE_TUNNEL) {
            avpriv_report_missing_feature(s, "Tunneling of RTSP requests with content data");
            return AVERROR_PATCHWELCOME;
        }
        ffurl_write(rt->rtsp_hd_out, send_content, send_content_length);
    }
    rt->last_cmd_time = av_gettime_relative();

    return 0;
}

int ff_rtsp_send_cmd_async(AVFormatContext *s, const char *method,
                           const char *url, const char *headers)
{
    return rtsp_send_cmd_with_content_async(s, method, url, headers, NULL, 0);
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
    int ret, attempts = 0;

retry:
    cur_auth_type = rt->auth_state.auth_type;
    if ((ret = rtsp_send_cmd_with_content_async(s, method, url, header,
                                                send_content,
                                                send_content_length)))
        return ret;

    if ((ret = ff_rtsp_read_reply(s, reply, content_ptr, 0, method) ) < 0)
        return ret;
    attempts++;

    if (reply->status_code == 401 &&
        (cur_auth_type == HTTP_AUTH_NONE || rt->auth_state.stale) &&
        rt->auth_state.auth_type != HTTP_AUTH_NONE && attempts < 2)
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
    else if (rt->transport == RTSP_TRANSPORT_RAW)
        trans_pref = "RAW/RAW";
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
                AVDictionary *opts = map_to_opts(rt);

                ff_url_join(buf, sizeof(buf), "rtp", NULL, host, -1,
                            "?localport=%d", j);
                /* we will use two ports per rtp stream (rtp and rtcp) */
                j += 2;
                err = ffurl_open_whitelist(&rtsp_st->rtp_handle, buf, AVIO_FLAG_READ_WRITE,
                                 &s->interrupt_callback, &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);

                av_dict_free(&opts);

                if (!err)
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
                (rtsp_st->stream_index < 0 ||
                 s->streams[rtsp_st->stream_index]->codecpar->codec_type ==
                    AVMEDIA_TYPE_DATA))
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
            av_strlcat(transport, ";mode=record", sizeof(transport));
        } else if (rt->server_type == RTSP_SERVER_REAL ||
                   rt->server_type == RTSP_SERVER_WMS)
            av_strlcat(transport, ";mode=play", sizeof(transport));
        snprintf(cmd, sizeof(cmd),
                 "Transport: %s\r\n",
                 transport);
        if (rt->accept_dynamic_rate)
            av_strlcat(cmd, "x-Dynamic-Rate: 0\r\n", sizeof(cmd));
        if (CONFIG_RTPDEC && i == 0 && rt->server_type == RTSP_SERVER_REAL) {
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
            err = ff_rtsp_averror(reply->status_code, AVERROR_INVALIDDATA);
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
            const char *peer = host;

            if (rt->rtsp_flags & RTSP_FLAG_FILTER_SRC)
                av_strlcpy(options, "?connect=1", sizeof(options));
            /* Use source address if specified */
            if (reply->transports[0].source[0])
                peer = reply->transports[0].source;
            ff_url_join(url, sizeof(url), "rtp", NULL, peer,
                        reply->transports[0].server_port_min, "%s", options);
            if (!(rt->server_type == RTSP_SERVER_WMS && i > 1) &&
                ff_rtp_set_remote_url(rtsp_st->rtp_handle, url) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            break;
        }
        case RTSP_LOWER_TRANSPORT_UDP_MULTICAST: {
            char url[1024], namebuf[50], optbuf[20] = "";
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
            if (ttl > 0)
                snprintf(optbuf, sizeof(optbuf), "?ttl=%d", ttl);
            getnameinfo((struct sockaddr*) &addr, sizeof(addr),
                        namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
            ff_url_join(url, sizeof(url), "rtp", NULL, namebuf,
                        port, "%s", optbuf);
            if (ffurl_open_whitelist(&rtsp_st->rtp_handle, url, AVIO_FLAG_READ_WRITE,
                           &s->interrupt_callback, NULL, s->protocol_whitelist, s->protocol_blacklist, NULL) < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            break;
        }
        }

        if ((err = ff_rtsp_open_transport_ctx(s, rtsp_st)))
            goto fail;
    }

    if (rt->nb_rtsp_streams && reply->timeout > 0)
        rt->timeout = reply->timeout;

    if (rt->server_type == RTSP_SERVER_REAL)
        rt->need_subscription = 1;

    return 0;

fail:
    ff_rtsp_undo_setup(s, 0);
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
    char proto[128], host[1024], path[1024];
    char tcpname[1024], cmd[2048], auth[128];
    const char *lower_rtsp_proto = "tcp";
    int port, err, tcp_fd;
    RTSPMessageHeader reply1, *reply = &reply1;
    int lower_transport_mask = 0;
    int default_port = RTSP_DEFAULT_PORT;
    int https_tunnel = 0;
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

    if (s->max_delay < 0) /* Not set by the caller */
        s->max_delay = s->iformat ? DEFAULT_REORDERING_DELAY : 0;

    rt->control_transport = RTSP_MODE_PLAIN;
    if (rt->lower_transport_mask & ((1 << RTSP_LOWER_TRANSPORT_HTTP) |
                                    (1 << RTSP_LOWER_TRANSPORT_HTTPS))) {
        https_tunnel = !!(rt->lower_transport_mask & (1 << RTSP_LOWER_TRANSPORT_HTTPS));
        rt->lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
        rt->control_transport = RTSP_MODE_TUNNEL;
    }
    /* Only pass through valid flags from here */
    rt->lower_transport_mask &= (1 << RTSP_LOWER_TRANSPORT_NB) - 1;

redirect:
    memset(&reply1, 0, sizeof(reply1));
    /* extract hostname and port */
    av_url_split(proto, sizeof(proto), auth, sizeof(auth),
                 host, sizeof(host), &port, path, sizeof(path), s->url);

    if (!strcmp(proto, "rtsps")) {
        lower_rtsp_proto         = "tls";
        default_port             = RTSPS_DEFAULT_PORT;
        rt->lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
    }

    if (*auth) {
        av_strlcpy(rt->auth, auth, sizeof(rt->auth));
    }
    if (port < 0)
        port = default_port;

    lower_transport_mask = rt->lower_transport_mask;

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

    /* Construct the URI used in request; this is similar to s->url,
     * but with authentication credentials removed and RTSP specific options
     * stripped out. */
    ff_url_join(rt->control_uri, sizeof(rt->control_uri), proto, NULL,
                host, port, "%s", path);

    if (rt->control_transport == RTSP_MODE_TUNNEL) {
        /* set up initial handshake for tunneling */
        char httpname[1024];
        char sessioncookie[17];
        char headers[1024];

        ff_url_join(httpname, sizeof(httpname), https_tunnel ? "https" : "http", auth, host, port, "%s", path);
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

        if (!rt->rtsp_hd->protocol_whitelist && s->protocol_whitelist) {
            rt->rtsp_hd->protocol_whitelist = av_strdup(s->protocol_whitelist);
            if (!rt->rtsp_hd->protocol_whitelist) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
        }

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
        int ret;
        /* open the tcp connection */
        ff_url_join(tcpname, sizeof(tcpname), lower_rtsp_proto, NULL,
                    host, port,
                    "?timeout=%d", rt->stimeout);
        if ((ret = ffurl_open_whitelist(&rt->rtsp_hd, tcpname, AVIO_FLAG_READ_WRITE,
                       &s->interrupt_callback, NULL, s->protocol_whitelist, s->protocol_blacklist, NULL)) < 0) {
            err = ret;
            goto fail;
        }
        rt->rtsp_hd_out = rt->rtsp_hd;
    }
    rt->seq = 0;

    tcp_fd = ffurl_get_file_handle(rt->rtsp_hd);
    if (tcp_fd < 0) {
        err = tcp_fd;
        goto fail;
    }
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
            err = ff_rtsp_averror(reply->status_code, AVERROR_INVALIDDATA);
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

    if (CONFIG_RTSP_DEMUXER && s->iformat)
        err = ff_rtsp_setup_input_streams(s, reply);
    else if (CONFIG_RTSP_MUXER)
        err = ff_rtsp_setup_output_streams(s, host);
    else
        av_assert0(0);
    if (err)
        goto fail;

    do {
        int lower_transport = ff_log2_tab[lower_transport_mask &
                                  ~(lower_transport_mask - 1)];

        if ((lower_transport_mask & (1 << RTSP_LOWER_TRANSPORT_TCP))
                && (rt->rtsp_flags & RTSP_FLAG_PREFER_TCP))
            lower_transport = RTSP_LOWER_TRANSPORT_TCP;

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
        char *new_url = av_strdup(reply->location);
        if (!new_url) {
            err = AVERROR(ENOMEM);
            goto fail2;
        }
        ff_format_set_url(s, new_url);
        rt->session_id[0] = '\0';
        av_log(s, AV_LOG_INFO, "Status %d: Redirecting to %s\n",
               reply->status_code,
               s->url);
        goto redirect;
    }
 fail2:
    ff_network_close();
    return err;
}
#endif /* CONFIG_RTSP_DEMUXER || CONFIG_RTSP_MUXER */

#if CONFIG_RTPDEC
static int parse_rtsp_message(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int ret;

    if (rt->rtsp_flags & RTSP_FLAG_LISTEN) {
        if (rt->state == RTSP_STATE_STREAMING) {
            if (!ff_rtsp_parse_streaming_commands(s))
                return AVERROR_EOF;
            else
                av_log(s, AV_LOG_WARNING,
                       "Unable to answer to TEARDOWN\n");
        } else
            return 0;
    } else {
        RTSPMessageHeader reply;
        ret = ff_rtsp_read_reply(s, &reply, NULL, 0, NULL);
        if (ret < 0)
            return ret;
        /* XXX: parse message */
        if (rt->state != RTSP_STATE_STREAMING)
            return 0;
    }

    return 0;
}

static int udp_read_packet(AVFormatContext *s, RTSPStream **prtsp_st,
                           uint8_t *buf, int buf_size, int64_t wait_end)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    int n, i, ret, timeout_cnt = 0;
    struct pollfd *p = rt->p;
    int *fds = NULL, fdsnum, fdsidx;

    if (!p) {
        p = rt->p = av_malloc_array(2 * (rt->nb_rtsp_streams + 1), sizeof(struct pollfd));
        if (!p)
            return AVERROR(ENOMEM);

        if (rt->rtsp_hd) {
            p[rt->max_p].fd = ffurl_get_file_handle(rt->rtsp_hd);
            p[rt->max_p++].events = POLLIN;
        }
        for (i = 0; i < rt->nb_rtsp_streams; i++) {
            rtsp_st = rt->rtsp_streams[i];
            if (rtsp_st->rtp_handle) {
                if (ret = ffurl_get_multi_file_handle(rtsp_st->rtp_handle,
                                                      &fds, &fdsnum)) {
                    av_log(s, AV_LOG_ERROR, "Unable to recover rtp ports\n");
                    return ret;
                }
                if (fdsnum != 2) {
                    av_log(s, AV_LOG_ERROR,
                           "Number of fds %d not supported\n", fdsnum);
                    return AVERROR_INVALIDDATA;
                }
                for (fdsidx = 0; fdsidx < fdsnum; fdsidx++) {
                    p[rt->max_p].fd       = fds[fdsidx];
                    p[rt->max_p++].events = POLLIN;
                }
                av_freep(&fds);
            }
        }
    }

    for (;;) {
        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;
        if (wait_end && wait_end - av_gettime_relative() < 0)
            return AVERROR(EAGAIN);
        n = poll(p, rt->max_p, POLL_TIMEOUT_MS);
        if (n > 0) {
            int j = rt->rtsp_hd ? 1 : 0;
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
            if (rt->rtsp_hd && p[0].revents & POLLIN) {
                if ((ret = parse_rtsp_message(s)) < 0) {
                    return ret;
                }
            }
#endif
        } else if (n == 0 && ++timeout_cnt >= MAX_TIMEOUTS) {
            return AVERROR(ETIMEDOUT);
        } else if (n < 0 && errno != EINTR)
            return AVERROR(errno);
    }
}

static int pick_stream(AVFormatContext *s, RTSPStream **rtsp_st,
                       const uint8_t *buf, int len)
{
    RTSPState *rt = s->priv_data;
    int i;
    if (len < 0)
        return len;
    if (rt->nb_rtsp_streams == 1) {
        *rtsp_st = rt->rtsp_streams[0];
        return len;
    }
    if (len >= 8 && rt->transport == RTSP_TRANSPORT_RTP) {
        if (RTP_PT_IS_RTCP(rt->recvbuf[1])) {
            int no_ssrc = 0;
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
                if (!rtpctx)
                    continue;
                if (rtpctx->ssrc == AV_RB32(&buf[4])) {
                    *rtsp_st = rt->rtsp_streams[i];
                    return len;
                }
                if (!rtpctx->ssrc)
                    no_ssrc = 1;
            }
            if (no_ssrc) {
                av_log(s, AV_LOG_WARNING,
                       "Unable to pick stream for packet - SSRC not known for "
                       "all streams\n");
                return AVERROR(EAGAIN);
            }
        } else {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                if ((buf[1] & 0x7f) == rt->rtsp_streams[i]->sdp_payload_type) {
                    *rtsp_st = rt->rtsp_streams[i];
                    return len;
                }
            }
        }
    }
    av_log(s, AV_LOG_WARNING, "Unable to pick stream for packet\n");
    return AVERROR(EAGAIN);
}

static int read_packet(AVFormatContext *s,
                       RTSPStream **rtsp_st, RTSPStream *first_queue_st,
                       int64_t wait_end)
{
    RTSPState *rt = s->priv_data;
    int len;

    switch(rt->lower_transport) {
    default:
#if CONFIG_RTSP_DEMUXER
    case RTSP_LOWER_TRANSPORT_TCP:
        len = ff_rtsp_tcp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE);
        break;
#endif
    case RTSP_LOWER_TRANSPORT_UDP:
    case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
        len = udp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE, wait_end);
        if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
            ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, (*rtsp_st)->rtp_handle, NULL, len);
        break;
    case RTSP_LOWER_TRANSPORT_CUSTOM:
        if (first_queue_st && rt->transport == RTSP_TRANSPORT_RTP &&
            wait_end && wait_end < av_gettime_relative())
            len = AVERROR(EAGAIN);
        else
            len = avio_read_partial(s->pb, rt->recvbuf, RECVBUF_SIZE);
        len = pick_stream(s, rtsp_st, rt->recvbuf, len);
        if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
            ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, NULL, s->pb, len);
        break;
    }

    if (len == 0)
        return AVERROR_EOF;

    return len;
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
        } else if (rt->transport == RTSP_TRANSPORT_RTP) {
            ret = ff_rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
        } else if (CONFIG_RTPDEC && rt->ts) {
            ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf + rt->recvbuf_pos, rt->recvbuf_len - rt->recvbuf_pos);
            if (ret >= 0) {
                rt->recvbuf_pos += ret;
                ret = rt->recvbuf_pos < rt->recvbuf_len;
            }
        } else
            ret = -1;
        if (ret == 0) {
            rt->cur_transport_priv = NULL;
            return 0;
        } else if (ret == 1) {
            return 0;
        } else
            rt->cur_transport_priv = NULL;
    }

redo:
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
        if (first_queue_time) {
            wait_end = first_queue_time + s->max_delay;
        } else {
            wait_end = 0;
            first_queue_st = NULL;
        }
    }

    /* read next RTP packet */
    if (!rt->recvbuf) {
        rt->recvbuf = av_malloc(RECVBUF_SIZE);
        if (!rt->recvbuf)
            return AVERROR(ENOMEM);
    }

    len = read_packet(s, &rtsp_st, first_queue_st, wait_end);
    if (len == AVERROR(EAGAIN) && first_queue_st &&
        rt->transport == RTSP_TRANSPORT_RTP) {
        av_log(s, AV_LOG_WARNING,
                "max delay reached. need to consume packet\n");
        rtsp_st = first_queue_st;
        ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, NULL, 0);
        goto end;
    }
    if (len < 0)
        return len;

    if (rt->transport == RTSP_TRANSPORT_RDT) {
        ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
    } else if (rt->transport == RTSP_TRANSPORT_RTP) {
        ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
        if (rtsp_st->feedback) {
            AVIOContext *pb = NULL;
            if (rt->lower_transport == RTSP_LOWER_TRANSPORT_CUSTOM)
                pb = s->pb;
            ff_rtp_send_rtcp_feedback(rtsp_st->transport_priv, rtsp_st->rtp_handle, pb);
        }
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
                // Make real NTP start time available in AVFormatContext
                if (s->start_time_realtime == AV_NOPTS_VALUE) {
                    s->start_time_realtime = av_rescale (rtpctx->first_rtcp_ntp_time - (NTP_OFFSET << 32), 1000000, 1LL << 32);
                    if (rtpctx->st) {
                        s->start_time_realtime -=
                            av_rescale (rtpctx->rtcp_ts_offset,
                                        (uint64_t) rtpctx->st->time_base.num * 1000000,
                                                   rtpctx->st->time_base.den);
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
    } else if (CONFIG_RTPDEC && rt->ts) {
        ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf, len);
        if (ret >= 0) {
            if (ret < len) {
                rt->recvbuf_len = len;
                rt->recvbuf_pos = ret;
                rt->cur_transport_priv = rt->ts;
                return 1;
            } else {
                ret = 0;
            }
        }
    } else {
        return AVERROR_INVALIDDATA;
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
static int sdp_probe(const AVProbeData *p1)
{
    const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;

    /* we look for a line beginning "c=IN IP" */
    while (p < p_end && *p != '\0') {
        if (sizeof("c=IN IP") - 1 < p_end - p &&
            av_strstart(p, "c=IN IP", NULL))
            return AVPROBE_SCORE_EXTENSION;

        while (p < p_end - 1 && *p != '\n') p++;
        if (++p >= p_end)
            break;
        if (*p == '\r')
            p++;
    }
    return 0;
}

static void append_source_addrs(char *buf, int size, const char *name,
                                int count, struct RTSPSource **addrs)
{
    int i;
    if (!count)
        return;
    av_strlcatf(buf, size, "&%s=%s", name, addrs[0]->addr);
    for (i = 1; i < count; i++)
        av_strlcatf(buf, size, ",%s", addrs[i]->addr);
}

static int sdp_read_header(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    int size, i, err;
    char *content;
    char url[1024];

    if (!ff_network_init())
        return AVERROR(EIO);

    if (s->max_delay < 0) /* Not set by the caller */
        s->max_delay = DEFAULT_REORDERING_DELAY;
    if (rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)
        rt->lower_transport = RTSP_LOWER_TRANSPORT_CUSTOM;

    /* read the whole sdp file */
    /* XXX: better loading */
    content = av_malloc(SDP_MAX_SIZE);
    if (!content)
        return AVERROR(ENOMEM);
    size = avio_read(s->pb, content, SDP_MAX_SIZE - 1);
    if (size <= 0) {
        av_free(content);
        return AVERROR_INVALIDDATA;
    }
    content[size] ='\0';

    err = ff_sdp_parse(s, content);
    av_freep(&content);
    if (err) goto fail;

    /* open each RTP stream */
    for (i = 0; i < rt->nb_rtsp_streams; i++) {
        char namebuf[50];
        rtsp_st = rt->rtsp_streams[i];

        if (!(rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)) {
            AVDictionary *opts = map_to_opts(rt);

            err = getnameinfo((struct sockaddr*) &rtsp_st->sdp_ip,
                              sizeof(rtsp_st->sdp_ip),
                              namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
            if (err) {
                av_log(s, AV_LOG_ERROR, "getnameinfo: %s\n", gai_strerror(err));
                err = AVERROR(EIO);
                av_dict_free(&opts);
                goto fail;
            }
            ff_url_join(url, sizeof(url), "rtp", NULL,
                        namebuf, rtsp_st->sdp_port,
                        "?localport=%d&ttl=%d&connect=%d&write_to_source=%d",
                        rtsp_st->sdp_port, rtsp_st->sdp_ttl,
                        rt->rtsp_flags & RTSP_FLAG_FILTER_SRC ? 1 : 0,
                        rt->rtsp_flags & RTSP_FLAG_RTCP_TO_SOURCE ? 1 : 0);

            append_source_addrs(url, sizeof(url), "sources",
                                rtsp_st->nb_include_source_addrs,
                                rtsp_st->include_source_addrs);
            append_source_addrs(url, sizeof(url), "block",
                                rtsp_st->nb_exclude_source_addrs,
                                rtsp_st->exclude_source_addrs);
            err = ffurl_open_whitelist(&rtsp_st->rtp_handle, url, AVIO_FLAG_READ,
                           &s->interrupt_callback, &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);

            av_dict_free(&opts);

            if (err < 0) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
        }
        if ((err = ff_rtsp_open_transport_ctx(s, rtsp_st)))
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
    .priv_class     = &sdp_demuxer_class,
};
#endif /* CONFIG_SDP_DEMUXER */

#if CONFIG_RTP_DEMUXER
static int rtp_probe(const AVProbeData *p)
{
    if (av_strstart(p->filename, "rtp:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int rtp_read_header(AVFormatContext *s)
{
    uint8_t recvbuf[RTP_MAX_PACKET_LENGTH];
    char host[500], sdp[500];
    int ret, port;
    URLContext* in = NULL;
    int payload_type;
    AVCodecParameters *par = NULL;
    struct sockaddr_storage addr;
    AVIOContext pb;
    socklen_t addrlen = sizeof(addr);
    RTSPState *rt = s->priv_data;

    if (!ff_network_init())
        return AVERROR(EIO);

    ret = ffurl_open_whitelist(&in, s->url, AVIO_FLAG_READ,
                     &s->interrupt_callback, NULL, s->protocol_whitelist, s->protocol_blacklist, NULL);
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

        if (RTP_PT_IS_RTCP(recvbuf[1]))
            continue;

        payload_type = recvbuf[1] & 0x7f;
        break;
    }
    getsockname(ffurl_get_file_handle(in), (struct sockaddr*) &addr, &addrlen);
    ffurl_close(in);
    in = NULL;

    par = avcodec_parameters_alloc();
    if (!par) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (ff_rtp_get_codec_info(par, payload_type)) {
        av_log(s, AV_LOG_ERROR, "Unable to receive RTP payload type %d "
                                "without an SDP file describing it\n",
                                 payload_type);
        goto fail;
    }
    if (par->codec_type != AVMEDIA_TYPE_DATA) {
        av_log(s, AV_LOG_WARNING, "Guessing on RTP content - if not received "
                                  "properly you need an SDP file "
                                  "describing it\n");
    }

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port,
                 NULL, 0, s->url);

    snprintf(sdp, sizeof(sdp),
             "v=0\r\nc=IN IP%d %s\r\nm=%s %d RTP/AVP %d\r\n",
             addr.ss_family == AF_INET ? 4 : 6, host,
             par->codec_type == AVMEDIA_TYPE_DATA  ? "application" :
             par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
             port, payload_type);
    av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", sdp);
    avcodec_parameters_free(&par);

    ffio_init_context(&pb, sdp, strlen(sdp), 0, NULL, NULL, NULL, NULL);
    s->pb = &pb;

    /* sdp_read_header initializes this again */
    ff_network_close();

    rt->media_type_mask = (1 << (AVMEDIA_TYPE_SUBTITLE+1)) - 1;

    ret = sdp_read_header(s);
    s->pb = NULL;
    return ret;

fail:
    avcodec_parameters_free(&par);
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
    .long_name      = NULL_IF_CONFIG_SMALL("RTP input"),
    .priv_data_size = sizeof(RTSPState),
    .read_probe     = rtp_probe,
    .read_header    = rtp_read_header,
    .read_packet    = ff_rtsp_fetch_packet,
    .read_close     = sdp_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &rtp_demuxer_class,
};
#endif /* CONFIG_RTP_DEMUXER */
