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
#include <libavutil/intreadwrite.h>

static int rtsp_write_record(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
             "Range: npt=%0.3f-\r\n",
             (double) 0);
    ff_rtsp_send_cmd(s, "RECORD", rt->control_uri, cmd, reply, NULL);
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

static int tcp_write_packet(AVFormatContext *s, RTSPStream *rtsp_st)
{
    RTSPState *rt = s->priv_data;
    AVFormatContext *rtpctx = rtsp_st->transport_priv;
    uint8_t *buf, *ptr;
    int size;
    uint8_t interleave_header[4];

    size = url_close_dyn_buf(rtpctx->pb, &buf);
    ptr = buf;
    while (size > 4) {
        uint32_t packet_len = AV_RB32(ptr);
        int id;
        ptr += 4;
        size -= 4;
        if (packet_len > size || packet_len < 2)
            break;
        if (ptr[1] >= 200 && ptr[1] <= 204)
            id = rtsp_st->interleaved_max; /* RTCP */
        else
            id = rtsp_st->interleaved_min; /* RTP */
        interleave_header[0] = '$';
        interleave_header[1] = id;
        AV_WB16(interleave_header + 2, packet_len);
        url_write(rt->rtsp_hd, interleave_header, 4);
        url_write(rt->rtsp_hd, ptr, packet_len);
        ptr += packet_len;
        size -= packet_len;
    }
    av_free(buf);
    url_open_dyn_packet_buf(&rtpctx->pb, RTSP_TCP_MAX_PACKET_SIZE);
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
    int ret;

    tcp_fd = url_get_file_handle(rt->rtsp_hd);

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(tcp_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        n = select(tcp_fd + 1, &rfds, NULL, NULL, &tv);
        if (n <= 0)
            break;
        if (FD_ISSET(tcp_fd, &rfds)) {
            RTSPMessageHeader reply;

            /* Don't let ff_rtsp_read_reply handle interleaved packets,
             * since it would block and wait for an RTSP reply on the socket
             * (which may not be coming any time soon) if it handles
             * interleaved packets internally. */
            ret = ff_rtsp_read_reply(s, &reply, NULL, 1);
            if (ret < 0)
                return AVERROR(EPIPE);
            if (ret == 1)
                ff_rtsp_skip_packet(s);
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
    ret = av_write_frame(rtpctx, &local_pkt);
    /* av_write_frame does all the RTP packetization. If using TCP as
     * transport, rtpctx->pb is only a dyn_packet_buf that queues up the
     * packets, so we need to send them out on the TCP connection separately.
     */
    if (!ret && rt->lower_transport == RTSP_LOWER_TRANSPORT_TCP)
        ret = tcp_write_packet(s, rtsp_st);
    return ret;
}

static int rtsp_write_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;

    ff_rtsp_send_cmd_async(s, "TEARDOWN", rt->control_uri, NULL);

    ff_rtsp_close_streams(s);
    url_close(rt->rtsp_hd);
    ff_network_close();
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

