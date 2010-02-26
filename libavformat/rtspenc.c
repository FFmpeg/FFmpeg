/*
 * RTSP muxer
 * Copyright (c) 2010 Martin Storsjo
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

#include "avformat.h"

#include <sys/time.h>
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include "network.h"
#include "rtsp.h"

static int rtsp_write_record(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
             "RECORD %s RTSP/1.0\r\n"
             "Range: npt=%0.3f-\r\n",
             s->filename,
             (double) 0);
    ff_rtsp_send_cmd(s, cmd, reply, NULL);
    if (reply->status_code != RTSP_STATUS_OK)
        return -1;
    rt->state = RTSP_STATE_STREAMING;
    return 0;
}

static int rtsp_write_header(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    int ret;

    ret = ff_rtsp_connect(s);
    if (ret)
        return ret;

    if (rtsp_write_record(s) < 0) {
        ff_rtsp_close_streams(s);
        url_close(rt->rtsp_hd);
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int rtsp_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    RTSPState *rt = s->priv_data;
    RTSPStream *rtsp_st;
    fd_set rfds;
    int n, tcp_fd;
    struct timeval tv;
    AVFormatContext *rtpctx;
    AVPacket local_pkt;

    FD_ZERO(&rfds);
    tcp_fd = url_get_file_handle(rt->rtsp_hd);
    FD_SET(tcp_fd, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(tcp_fd + 1, &rfds, NULL, NULL, &tv);
    if (n > 0) {
        if (FD_ISSET(tcp_fd, &rfds)) {
            RTSPMessageHeader reply;

            if (ff_rtsp_read_reply(s, &reply, NULL, 0) < 0)
                return AVERROR(EPIPE);
            /* XXX: parse message */
            if (rt->state != RTSP_STATE_STREAMING)
                return AVERROR(EPIPE);
        }
    }

    if (pkt->stream_index < 0 || pkt->stream_index >= rt->nb_rtsp_streams)
        return AVERROR_INVALIDDATA;
    rtsp_st = rt->rtsp_streams[pkt->stream_index];
    rtpctx = rtsp_st->transport_priv;

    /* Use a local packet for writing to the chained muxer, otherwise
     * the internal stream_index = 0 becomes visible to the muxer user. */
    local_pkt = *pkt;
    local_pkt.stream_index = 0;
    return av_write_frame(rtpctx, &local_pkt);
}

static int rtsp_write_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
             "TEARDOWN %s RTSP/1.0\r\n",
             s->filename);
    ff_rtsp_send_cmd_async(s, cmd);

    ff_rtsp_close_streams(s);
    url_close(rt->rtsp_hd);
    return 0;
}

AVOutputFormat rtsp_muxer = {
    "rtsp",
    NULL_IF_CONFIG_SMALL("RTSP output format"),
    NULL,
    NULL,
    sizeof(RTSPState),
    CODEC_ID_PCM_MULAW,
    CODEC_ID_NONE,
    rtsp_write_header,
    rtsp_write_packet,
    rtsp_write_close,
    .flags = AVFMT_NOFILE | AVFMT_GLOBALHEADER,
};

