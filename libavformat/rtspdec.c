/*
 * RTSP demuxer
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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/random_seed.h"
#include "libavutil/time.h"
#include "avformat.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "rtpproto.h"
#include "rtsp.h"
#include "rdt.h"
#include "url.h"

static const struct RTSPStatusMessage {
    enum RTSPStatusCode code;
    const char *message;
} status_messages[] = {
    { RTSP_STATUS_OK,             "OK"                               },
    { RTSP_STATUS_METHOD,         "Method Not Allowed"               },
    { RTSP_STATUS_BANDWIDTH,      "Not Enough Bandwidth"             },
    { RTSP_STATUS_SESSION,        "Session Not Found"                },
    { RTSP_STATUS_STATE,          "Method Not Valid in This State"   },
    { RTSP_STATUS_AGGREGATE,      "Aggregate operation not allowed"  },
    { RTSP_STATUS_ONLY_AGGREGATE, "Only aggregate operation allowed" },
    { RTSP_STATUS_TRANSPORT,      "Unsupported transport"            },
    { RTSP_STATUS_INTERNAL,       "Internal Server Error"            },
    { RTSP_STATUS_SERVICE,        "Service Unavailable"              },
    { RTSP_STATUS_VERSION,        "RTSP Version not supported"       },
    { 0,                          "NULL"                             }
};

static int rtsp_read_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;

    if (!(rt->rtsp_flags & RTSP_FLAG_LISTEN))
        ff_rtsp_send_cmd_async(s, "TEARDOWN", rt->control_uri, NULL);

    ff_rtsp_close_streams(s);
    ff_rtsp_close_connections(s);
    ff_network_close();
    rt->real_setup = NULL;
    av_freep(&rt->real_setup_cache);
    return 0;
}

static inline int read_line(AVFormatContext *s, char *rbuf, const int rbufsize,
                            int *rbuflen)
{
    RTSPState *rt = s->priv_data;
    int idx       = 0;
    int ret       = 0;
    *rbuflen      = 0;

    do {
        ret = ffurl_read_complete(rt->rtsp_hd, rbuf + idx, 1);
        if (ret <= 0)
            return ret ? ret : AVERROR_EOF;
        if (rbuf[idx] == '\r') {
            /* Ignore */
        } else if (rbuf[idx] == '\n') {
            rbuf[idx] = '\0';
            *rbuflen  = idx;
            return 0;
        } else
            idx++;
    } while (idx < rbufsize);
    av_log(s, AV_LOG_ERROR, "Message too long\n");
    return AVERROR(EIO);
}

static int rtsp_send_reply(AVFormatContext *s, enum RTSPStatusCode code,
                           const char *extracontent, uint16_t seq)
{
    RTSPState *rt = s->priv_data;
    char message[4096];
    int index = 0;
    while (status_messages[index].code) {
        if (status_messages[index].code == code) {
            snprintf(message, sizeof(message), "RTSP/1.0 %d %s\r\n",
                     code, status_messages[index].message);
            break;
        }
        index++;
    }
    if (!status_messages[index].code)
        return AVERROR(EINVAL);
    av_strlcatf(message, sizeof(message), "CSeq: %d\r\n", seq);
    av_strlcatf(message, sizeof(message), "Server: %s\r\n", LIBAVFORMAT_IDENT);
    if (extracontent)
        av_strlcat(message, extracontent, sizeof(message));
    av_strlcat(message, "\r\n", sizeof(message));
    av_dlog(s, "Sending response:\n%s", message);
    ffurl_write(rt->rtsp_hd, message, strlen(message));

    return 0;
}

static inline int check_sessionid(AVFormatContext *s,
                                  RTSPMessageHeader *request)
{
    RTSPState *rt = s->priv_data;
    unsigned char *session_id = rt->session_id;
    if (!session_id[0]) {
        av_log(s, AV_LOG_WARNING, "There is no session-id at the moment\n");
        return 0;
    }
    if (strcmp(session_id, request->session_id)) {
        av_log(s, AV_LOG_ERROR, "Unexpected session-id %s\n",
               request->session_id);
        rtsp_send_reply(s, RTSP_STATUS_SESSION, NULL, request->seq);
        return AVERROR_STREAM_NOT_FOUND;
    }
    return 0;
}

static inline int rtsp_read_request(AVFormatContext *s,
                                    RTSPMessageHeader *request,
                                    const char *method)
{
    RTSPState *rt = s->priv_data;
    char rbuf[1024];
    int rbuflen, ret;
    do {
        ret = read_line(s, rbuf, sizeof(rbuf), &rbuflen);
        if (ret)
            return ret;
        if (rbuflen > 1) {
            av_dlog(s, "Parsing[%d]: %s\n", rbuflen, rbuf);
            ff_rtsp_parse_line(request, rbuf, rt, method);
        }
    } while (rbuflen > 0);
    if (request->seq != rt->seq + 1) {
        av_log(s, AV_LOG_ERROR, "Unexpected Sequence number %d\n",
               request->seq);
        return AVERROR(EINVAL);
    }
    if (rt->session_id[0] && strcmp(method, "OPTIONS")) {
        ret = check_sessionid(s, request);
        if (ret)
            return ret;
    }

    return 0;
}

static int rtsp_read_announce(AVFormatContext *s)
{
    RTSPState *rt             = s->priv_data;
    RTSPMessageHeader request = { 0 };
    char sdp[4096];
    int  ret;

    ret = rtsp_read_request(s, &request, "ANNOUNCE");
    if (ret)
        return ret;
    rt->seq++;
    if (strcmp(request.content_type, "application/sdp")) {
        av_log(s, AV_LOG_ERROR, "Unexpected content type %s\n",
               request.content_type);
        rtsp_send_reply(s, RTSP_STATUS_SERVICE, NULL, request.seq);
        return AVERROR_OPTION_NOT_FOUND;
    }
    if (request.content_length && request.content_length < sizeof(sdp) - 1) {
        /* Read SDP */
        if (ffurl_read_complete(rt->rtsp_hd, sdp, request.content_length)
            < request.content_length) {
            av_log(s, AV_LOG_ERROR,
                   "Unable to get complete SDP Description in ANNOUNCE\n");
            rtsp_send_reply(s, RTSP_STATUS_INTERNAL, NULL, request.seq);
            return AVERROR(EIO);
        }
        sdp[request.content_length] = '\0';
        av_log(s, AV_LOG_VERBOSE, "SDP: %s\n", sdp);
        ret = ff_sdp_parse(s, sdp);
        if (ret)
            return ret;
        rtsp_send_reply(s, RTSP_STATUS_OK, NULL, request.seq);
        return 0;
    }
    av_log(s, AV_LOG_ERROR,
           "Content-Length header value exceeds sdp allocated buffer (4KB)\n");
    rtsp_send_reply(s, RTSP_STATUS_INTERNAL,
                    "Content-Length exceeds buffer size", request.seq);
    return AVERROR(EIO);
}

static int rtsp_read_options(AVFormatContext *s)
{
    RTSPState *rt             = s->priv_data;
    RTSPMessageHeader request = { 0 };
    int ret                   = 0;

    /* Parsing headers */
    ret = rtsp_read_request(s, &request, "OPTIONS");
    if (ret)
        return ret;
    rt->seq++;
    /* Send Reply */
    rtsp_send_reply(s, RTSP_STATUS_OK,
                    "Public: ANNOUNCE, PAUSE, SETUP, TEARDOWN, RECORD\r\n",
                    request.seq);
    return 0;
}

static int rtsp_read_setup(AVFormatContext *s, char* host, char *controlurl)
{
    RTSPState *rt             = s->priv_data;
    RTSPMessageHeader request = { 0 };
    int ret                   = 0;
    char url[1024];
    RTSPStream *rtsp_st;
    char responseheaders[1024];
    int localport    = -1;
    int transportidx = 0;
    int streamid     = 0;

    ret = rtsp_read_request(s, &request, "SETUP");
    if (ret)
        return ret;
    rt->seq++;
    if (!request.nb_transports) {
        av_log(s, AV_LOG_ERROR, "No transport defined in SETUP\n");
        return AVERROR_INVALIDDATA;
    }
    for (transportidx = 0; transportidx < request.nb_transports;
         transportidx++) {
        if (!request.transports[transportidx].mode_record ||
            (request.transports[transportidx].lower_transport !=
             RTSP_LOWER_TRANSPORT_UDP &&
             request.transports[transportidx].lower_transport !=
             RTSP_LOWER_TRANSPORT_TCP)) {
            av_log(s, AV_LOG_ERROR, "mode=record/receive not set or transport"
                   " protocol not supported (yet)\n");
            return AVERROR_INVALIDDATA;
        }
    }
    if (request.nb_transports > 1)
        av_log(s, AV_LOG_WARNING, "More than one transport not supported, "
               "using first of all\n");
    for (streamid = 0; streamid < rt->nb_rtsp_streams; streamid++) {
        if (!strcmp(rt->rtsp_streams[streamid]->control_url,
                    controlurl))
            break;
    }
    if (streamid == rt->nb_rtsp_streams) {
        av_log(s, AV_LOG_ERROR, "Unable to find requested track\n");
        return AVERROR_STREAM_NOT_FOUND;
    }
    rtsp_st   = rt->rtsp_streams[streamid];
    localport = rt->rtp_port_min;

    if (request.transports[0].lower_transport == RTSP_LOWER_TRANSPORT_TCP) {
        rt->lower_transport = RTSP_LOWER_TRANSPORT_TCP;
        if ((ret = ff_rtsp_open_transport_ctx(s, rtsp_st))) {
            rtsp_send_reply(s, RTSP_STATUS_TRANSPORT, NULL, request.seq);
            return ret;
        }
        rtsp_st->interleaved_min = request.transports[0].interleaved_min;
        rtsp_st->interleaved_max = request.transports[0].interleaved_max;
        snprintf(responseheaders, sizeof(responseheaders), "Transport: "
                 "RTP/AVP/TCP;unicast;mode=receive;interleaved=%d-%d"
                 "\r\n", request.transports[0].interleaved_min,
                 request.transports[0].interleaved_max);
    } else {
        do {
            ff_url_join(url, sizeof(url), "rtp", NULL, host, localport, NULL);
            av_dlog(s, "Opening: %s", url);
            ret = ffurl_open(&rtsp_st->rtp_handle, url, AVIO_FLAG_READ_WRITE,
                             &s->interrupt_callback, NULL);
            if (ret)
                localport += 2;
        } while (ret || localport > rt->rtp_port_max);
        if (localport > rt->rtp_port_max) {
            rtsp_send_reply(s, RTSP_STATUS_TRANSPORT, NULL, request.seq);
            return ret;
        }

        av_dlog(s, "Listening on: %d",
                ff_rtp_get_local_rtp_port(rtsp_st->rtp_handle));
        if ((ret = ff_rtsp_open_transport_ctx(s, rtsp_st))) {
            rtsp_send_reply(s, RTSP_STATUS_TRANSPORT, NULL, request.seq);
            return ret;
        }

        localport = ff_rtp_get_local_rtp_port(rtsp_st->rtp_handle);
        snprintf(responseheaders, sizeof(responseheaders), "Transport: "
                 "RTP/AVP/UDP;unicast;mode=receive;source=%s;"
                 "client_port=%d-%d;server_port=%d-%d\r\n",
                 host, request.transports[0].client_port_min,
                 request.transports[0].client_port_max, localport,
                 localport + 1);
    }

    /* Establish sessionid if not previously set */
    /* Put this in a function? */
    /* RFC 2326: session id must be at least 8 digits */
    while (strlen(rt->session_id) < 8)
        av_strlcatf(rt->session_id, 512, "%u", av_get_random_seed());

    av_strlcatf(responseheaders, sizeof(responseheaders), "Session: %s\r\n",
                rt->session_id);
    /* Send Reply */
    rtsp_send_reply(s, RTSP_STATUS_OK, responseheaders, request.seq);

    rt->state = RTSP_STATE_PAUSED;
    return 0;
}

static int rtsp_read_record(AVFormatContext *s)
{
    RTSPState *rt             = s->priv_data;
    RTSPMessageHeader request = { 0 };
    int ret                   = 0;
    char responseheaders[1024];

    ret = rtsp_read_request(s, &request, "RECORD");
    if (ret)
        return ret;
    ret = check_sessionid(s, &request);
    if (ret)
        return ret;
    rt->seq++;
    snprintf(responseheaders, sizeof(responseheaders), "Session: %s\r\n",
             rt->session_id);
    rtsp_send_reply(s, RTSP_STATUS_OK, responseheaders, request.seq);

    rt->state = RTSP_STATE_STREAMING;
    return 0;
}

static inline int parse_command_line(AVFormatContext *s, const char *line,
                                     int linelen, char *uri, int urisize,
                                     char *method, int methodsize,
                                     enum RTSPMethod *methodcode)
{
    RTSPState *rt = s->priv_data;
    const char *linept, *searchlinept;
    linept = strchr(line, ' ');
    if (!linept) {
        av_log(s, AV_LOG_ERROR, "Error parsing method string\n");
        return AVERROR_INVALIDDATA;
    }
    if (linept - line > methodsize - 1) {
        av_log(s, AV_LOG_ERROR, "Method string too long\n");
        return AVERROR(EIO);
    }
    memcpy(method, line, linept - line);
    method[linept - line] = '\0';
    linept++;
    if (!strcmp(method, "ANNOUNCE"))
        *methodcode = ANNOUNCE;
    else if (!strcmp(method, "OPTIONS"))
        *methodcode = OPTIONS;
    else if (!strcmp(method, "RECORD"))
        *methodcode = RECORD;
    else if (!strcmp(method, "SETUP"))
        *methodcode = SETUP;
    else if (!strcmp(method, "PAUSE"))
        *methodcode = PAUSE;
    else if (!strcmp(method, "TEARDOWN"))
        *methodcode = TEARDOWN;
    else
        *methodcode = UNKNOWN;
    /* Check method with the state  */
    if (rt->state == RTSP_STATE_IDLE) {
        if ((*methodcode != ANNOUNCE) && (*methodcode != OPTIONS)) {
            av_log(s, AV_LOG_ERROR, "Unexpected command in Idle State %s\n",
                   line);
            return AVERROR_PROTOCOL_NOT_FOUND;
        }
    } else if (rt->state == RTSP_STATE_PAUSED) {
        if ((*methodcode != OPTIONS) && (*methodcode != RECORD)
            && (*methodcode != SETUP)) {
            av_log(s, AV_LOG_ERROR, "Unexpected command in Paused State %s\n",
                   line);
            return AVERROR_PROTOCOL_NOT_FOUND;
        }
    } else if (rt->state == RTSP_STATE_STREAMING) {
        if ((*methodcode != PAUSE) && (*methodcode != OPTIONS)
            && (*methodcode != TEARDOWN)) {
            av_log(s, AV_LOG_ERROR, "Unexpected command in Streaming State"
                   " %s\n", line);
            return AVERROR_PROTOCOL_NOT_FOUND;
        }
    } else {
        av_log(s, AV_LOG_ERROR, "Unexpected State [%d]\n", rt->state);
        return AVERROR_BUG;
    }

    searchlinept = strchr(linept, ' ');
    if (searchlinept == NULL) {
        av_log(s, AV_LOG_ERROR, "Error parsing message URI\n");
        return AVERROR_INVALIDDATA;
    }
    if (searchlinept - linept > urisize - 1) {
        av_log(s, AV_LOG_ERROR, "uri string length exceeded buffer size\n");
        return AVERROR(EIO);
    }
    memcpy(uri, linept, searchlinept - linept);
    uri[searchlinept - linept] = '\0';
    if (strcmp(rt->control_uri, uri)) {
        char host[128], path[512], auth[128];
        int port;
        char ctl_host[128], ctl_path[512], ctl_auth[128];
        int ctl_port;
        av_url_split(NULL, 0, auth, sizeof(auth), host, sizeof(host), &port,
                     path, sizeof(path), uri);
        av_url_split(NULL, 0, ctl_auth, sizeof(ctl_auth), ctl_host,
                     sizeof(ctl_host), &ctl_port, ctl_path, sizeof(ctl_path),
                     rt->control_uri);
        if (strcmp(host, ctl_host))
            av_log(s, AV_LOG_INFO, "Host %s differs from expected %s\n",
                   host, ctl_host);
        if (strcmp(path, ctl_path) && *methodcode != SETUP)
            av_log(s, AV_LOG_WARNING, "WARNING: Path %s differs from expected"
                   " %s\n", path, ctl_path);
        if (*methodcode == ANNOUNCE) {
            av_log(s, AV_LOG_INFO,
                   "Updating control URI to %s\n", uri);
            av_strlcpy(rt->control_uri, uri, sizeof(rt->control_uri));
        }
    }

    linept = searchlinept + 1;
    if (!av_strstart(linept, "RTSP/1.0", NULL)) {
        av_log(s, AV_LOG_ERROR, "Error parsing protocol or version\n");
        return AVERROR_PROTOCOL_NOT_FOUND;
    }
    return 0;
}

int ff_rtsp_parse_streaming_commands(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    unsigned char rbuf[4096];
    unsigned char method[10];
    char uri[500];
    int ret;
    int rbuflen               = 0;
    RTSPMessageHeader request = { 0 };
    enum RTSPMethod methodcode;

    ret = read_line(s, rbuf, sizeof(rbuf), &rbuflen);
    if (ret < 0)
        return ret;
    ret = parse_command_line(s, rbuf, rbuflen, uri, sizeof(uri), method,
                             sizeof(method), &methodcode);
    if (ret) {
        av_log(s, AV_LOG_ERROR, "RTSP: Unexpected Command\n");
        return ret;
    }

    ret = rtsp_read_request(s, &request, method);
    if (ret)
        return ret;
    rt->seq++;
    if (methodcode == PAUSE) {
        rt->state = RTSP_STATE_PAUSED;
        ret       = rtsp_send_reply(s, RTSP_STATUS_OK, NULL , request.seq);
        // TODO: Missing date header in response
    } else if (methodcode == OPTIONS) {
        ret = rtsp_send_reply(s, RTSP_STATUS_OK,
                              "Public: ANNOUNCE, PAUSE, SETUP, TEARDOWN, "
                              "RECORD\r\n", request.seq);
    } else if (methodcode == TEARDOWN) {
        rt->state = RTSP_STATE_IDLE;
        ret       = rtsp_send_reply(s, RTSP_STATUS_OK, NULL , request.seq);
        return 0;
    }
    return ret;
}

static int rtsp_read_play(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    int i;
    char cmd[1024];

    av_log(s, AV_LOG_DEBUG, "hello state=%d\n", rt->state);
    rt->nb_byes = 0;

    if (!(rt->server_type == RTSP_SERVER_REAL && rt->need_subscription)) {
        if (rt->transport == RTSP_TRANSPORT_RTP) {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTSPStream *rtsp_st = rt->rtsp_streams[i];
                RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
                if (!rtpctx)
                    continue;
                ff_rtp_reset_packet_queue(rtpctx);
                rtpctx->last_rtcp_ntp_time  = AV_NOPTS_VALUE;
                rtpctx->first_rtcp_ntp_time = AV_NOPTS_VALUE;
                rtpctx->base_timestamp      = 0;
                rtpctx->timestamp           = 0;
                rtpctx->unwrapped_timestamp = 0;
                rtpctx->rtcp_ts_offset      = 0;
            }
        }
        if (rt->state == RTSP_STATE_PAUSED) {
            cmd[0] = 0;
        } else {
            snprintf(cmd, sizeof(cmd),
                     "Range: npt=%"PRId64".%03"PRId64"-\r\n",
                     rt->seek_timestamp / AV_TIME_BASE,
                     rt->seek_timestamp / (AV_TIME_BASE / 1000) % 1000);
        }
        ff_rtsp_send_cmd(s, "PLAY", rt->control_uri, cmd, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            return -1;
        }
        if (rt->transport == RTSP_TRANSPORT_RTP &&
            reply->range_start != AV_NOPTS_VALUE) {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTSPStream *rtsp_st = rt->rtsp_streams[i];
                RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
                AVStream *st = NULL;
                if (!rtpctx || rtsp_st->stream_index < 0)
                    continue;
                st = s->streams[rtsp_st->stream_index];
                rtpctx->range_start_offset =
                    av_rescale_q(reply->range_start, AV_TIME_BASE_Q,
                                 st->time_base);
            }
        }
    }
    rt->state = RTSP_STATE_STREAMING;
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

int ff_rtsp_setup_input_streams(AVFormatContext *s, RTSPMessageHeader *reply)
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

    av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", content);
    /* now we got the SDP description, we parse it */
    ret = ff_sdp_parse(s, (const char *)content);
    av_freep(&content);
    if (ret < 0)
        return ret;

    return 0;
}

static int rtsp_listen(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    char host[128], path[512], auth[128];
    char uri[500];
    int port;
    char tcpname[500];
    unsigned char rbuf[4096];
    unsigned char method[10];
    int rbuflen = 0;
    int ret;
    enum RTSPMethod methodcode;

    /* extract hostname and port */
    av_url_split(NULL, 0, auth, sizeof(auth), host, sizeof(host), &port,
                 path, sizeof(path), s->filename);

    /* ff_url_join. No authorization by now (NULL) */
    ff_url_join(rt->control_uri, sizeof(rt->control_uri), "rtsp", NULL, host,
                port, "%s", path);

    if (port < 0)
        port = RTSP_DEFAULT_PORT;

    /* Create TCP connection */
    ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, host, port,
                "?listen&listen_timeout=%d", rt->initial_timeout * 1000);

    if (ret = ffurl_open(&rt->rtsp_hd, tcpname, AVIO_FLAG_READ_WRITE,
                         &s->interrupt_callback, NULL)) {
        av_log(s, AV_LOG_ERROR, "Unable to open RTSP for listening\n");
        return ret;
    }
    rt->state       = RTSP_STATE_IDLE;
    rt->rtsp_hd_out = rt->rtsp_hd;
    for (;;) { /* Wait for incoming RTSP messages */
        ret = read_line(s, rbuf, sizeof(rbuf), &rbuflen);
        if (ret < 0)
            return ret;
        ret = parse_command_line(s, rbuf, rbuflen, uri, sizeof(uri), method,
                                 sizeof(method), &methodcode);
        if (ret) {
            av_log(s, AV_LOG_ERROR, "RTSP: Unexpected Command\n");
            return ret;
        }

        if (methodcode == ANNOUNCE) {
            ret       = rtsp_read_announce(s);
            rt->state = RTSP_STATE_PAUSED;
        } else if (methodcode == OPTIONS) {
            ret = rtsp_read_options(s);
        } else if (methodcode == RECORD) {
            ret = rtsp_read_record(s);
            if (!ret)
                return 0; // We are ready for streaming
        } else if (methodcode == SETUP)
            ret = rtsp_read_setup(s, host, uri);
        if (ret) {
            ffurl_close(rt->rtsp_hd);
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

static int rtsp_probe(AVProbeData *p)
{
    if (av_strstart(p->filename, "rtsp:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int rtsp_read_header(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int ret;

    if (rt->initial_timeout > 0)
        rt->rtsp_flags |= RTSP_FLAG_LISTEN;

    if (rt->rtsp_flags & RTSP_FLAG_LISTEN) {
        ret = rtsp_listen(s);
        if (ret)
            return ret;
    } else {
        ret = ff_rtsp_connect(s);
        if (ret)
            return ret;

        rt->real_setup_cache = !s->nb_streams ? NULL :
            av_mallocz(2 * s->nb_streams * sizeof(*rt->real_setup_cache));
        if (!rt->real_setup_cache && s->nb_streams)
            return AVERROR(ENOMEM);
        rt->real_setup = rt->real_setup_cache + s->nb_streams;

        if (rt->initial_pause) {
            /* do not start immediately */
        } else {
            if (rtsp_read_play(s) < 0) {
                ff_rtsp_close_streams(s);
                ff_rtsp_close_connections(s);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    return 0;
}

int ff_rtsp_tcp_read_packet(AVFormatContext *s, RTSPStream **prtsp_st,
                            uint8_t *buf, int buf_size)
{
    RTSPState *rt = s->priv_data;
    int id, len, i, ret;
    RTSPStream *rtsp_st;

    av_dlog(s, "tcp_read_packet:\n");
redo:
    for (;;) {
        RTSPMessageHeader reply;

        ret = ff_rtsp_read_reply(s, &reply, NULL, 1, NULL);
        if (ret < 0)
            return ret;
        if (ret == 1) /* received '$' */
            break;
        /* XXX: parse message */
        if (rt->state != RTSP_STATE_STREAMING)
            return 0;
    }
    ret = ffurl_read_complete(rt->rtsp_hd, buf, 3);
    if (ret != 3)
        return -1;
    id  = buf[0];
    len = AV_RB16(buf + 1);
    av_dlog(s, "id=%d len=%d\n", id, len);
    if (len > buf_size || len < 8)
        goto redo;
    /* get the data */
    ret = ffurl_read_complete(rt->rtsp_hd, buf, len);
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

static int resetup_tcp(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    char host[1024];
    int port;

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port, NULL, 0,
                 s->filename);
    ff_rtsp_undo_setup(s, 0);
    return ff_rtsp_make_setup_request(s, host, port, RTSP_LOWER_TRANSPORT_TCP,
                                      rt->real_challenge);
}

static int rtsp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RTSPState *rt = s->priv_data;
    int ret;
    RTSPMessageHeader reply1, *reply = &reply1;
    char cmd[1024];

retry:
    if (rt->server_type == RTSP_SERVER_REAL) {
        int i;

        for (i = 0; i < s->nb_streams; i++)
            rt->real_setup[i] = s->streams[i]->discard;

        if (!rt->need_subscription) {
            if (memcmp (rt->real_setup, rt->real_setup_cache,
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

            memcpy(rt->real_setup_cache, rt->real_setup,
                   sizeof(enum AVDiscard) * s->nb_streams);
            rt->last_subscription[0] = 0;

            snprintf(cmd, sizeof(cmd),
                     "Subscribe: ");
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                rule_nr = 0;
                for (r = 0; r < s->nb_streams; r++) {
                    if (s->streams[r]->id == i) {
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

    ret = ff_rtsp_fetch_packet(s, pkt);
    if (ret < 0) {
        if (ret == AVERROR(ETIMEDOUT) && !rt->packets) {
            if (rt->lower_transport == RTSP_LOWER_TRANSPORT_UDP &&
                rt->lower_transport_mask & (1 << RTSP_LOWER_TRANSPORT_TCP)) {
                RTSPMessageHeader reply1, *reply = &reply1;
                av_log(s, AV_LOG_WARNING, "UDP timeout, retrying with TCP\n");
                if (rtsp_read_pause(s) != 0)
                    return -1;
                // TEARDOWN is required on Real-RTSP, but might make
                // other servers close the connection.
                if (rt->server_type == RTSP_SERVER_REAL)
                    ff_rtsp_send_cmd(s, "TEARDOWN", rt->control_uri, NULL,
                                     reply, NULL);
                rt->session_id[0] = '\0';
                if (resetup_tcp(s) == 0) {
                    rt->state = RTSP_STATE_IDLE;
                    rt->need_subscription = 1;
                    if (rtsp_read_play(s) != 0)
                        return -1;
                    goto retry;
                }
            }
        }
        return ret;
    }
    rt->packets++;

    if (!(rt->rtsp_flags & RTSP_FLAG_LISTEN)) {
        /* send dummy request to keep TCP connection alive */
        if ((av_gettime() - rt->last_cmd_time) / 1000000 >= rt->timeout / 2 ||
            rt->auth_state.stale) {
            if (rt->server_type == RTSP_SERVER_WMS ||
                (rt->server_type != RTSP_SERVER_REAL &&
                 rt->get_parameter_supported)) {
                ff_rtsp_send_cmd_async(s, "GET_PARAMETER", rt->control_uri, NULL);
            } else {
                ff_rtsp_send_cmd_async(s, "OPTIONS", rt->control_uri, NULL);
            }
            /* The stale flag should be reset when creating the auth response in
             * ff_rtsp_send_cmd_async, but reset it here just in case we never
             * called the auth code (if we didn't have any credentials set). */
            rt->auth_state.stale = 0;
        }
    }

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

static const AVClass rtsp_demuxer_class = {
    .class_name     = "RTSP demuxer",
    .item_name      = av_default_item_name,
    .option         = ff_rtsp_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_rtsp_demuxer = {
    .name           = "rtsp",
    .long_name      = NULL_IF_CONFIG_SMALL("RTSP input"),
    .priv_data_size = sizeof(RTSPState),
    .read_probe     = rtsp_probe,
    .read_header    = rtsp_read_header,
    .read_packet    = rtsp_read_packet,
    .read_close     = rtsp_read_close,
    .read_seek      = rtsp_read_seek,
    .flags          = AVFMT_NOFILE,
    .read_play      = rtsp_read_play,
    .read_pause     = rtsp_read_pause,
    .priv_class     = &rtsp_demuxer_class,
};
