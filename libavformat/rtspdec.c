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
#include "avformat.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "rtsp.h"
#include "rdt.h"
#include "url.h"

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

static int rtsp_probe(AVProbeData *p)
{
    if (av_strstart(p->filename, "rtsp:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int rtsp_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    RTSPState *rt = s->priv_data;
    int ret;

    ret = ff_rtsp_connect(s);
    if (ret)
        return ret;

    rt->real_setup_cache = av_mallocz(2 * s->nb_streams * sizeof(*rt->real_setup_cache));
    if (!rt->real_setup_cache)
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
    ff_rtsp_undo_setup(s);
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

    /* send dummy request to keep TCP connection alive */
    if ((av_gettime() - rt->last_cmd_time) / 1000000 >= rt->timeout / 2) {
        if (rt->server_type == RTSP_SERVER_WMS ||
           (rt->server_type != RTSP_SERVER_REAL &&
            rt->get_parameter_supported)) {
            ff_rtsp_send_cmd_async(s, "GET_PARAMETER", rt->control_uri, NULL);
        } else {
            ff_rtsp_send_cmd_async(s, "OPTIONS", "*", NULL);
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

static int rtsp_read_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;

    ff_rtsp_send_cmd_async(s, "TEARDOWN", rt->control_uri, NULL);

    ff_rtsp_close_streams(s);
    ff_rtsp_close_connections(s);
    ff_network_close();
    rt->real_setup = NULL;
    av_freep(&rt->real_setup_cache);
    return 0;
}

const AVClass rtsp_demuxer_class = {
    .class_name     = "RTSP demuxer",
    .item_name      = av_default_item_name,
    .option         = ff_rtsp_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_rtsp_demuxer = {
    .name           = "rtsp",
    .long_name      = NULL_IF_CONFIG_SMALL("RTSP input format"),
    .priv_data_size = sizeof(RTSPState),
    .read_probe     = rtsp_probe,
    .read_header    = rtsp_read_header,
    .read_packet    = rtsp_read_packet,
    .read_close     = rtsp_read_close,
    .read_seek      = rtsp_read_seek,
    .flags = AVFMT_NOFILE,
    .read_play = rtsp_read_play,
    .read_pause = rtsp_read_pause,
    .priv_class = &rtsp_demuxer_class,
};
