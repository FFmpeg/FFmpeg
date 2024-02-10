/*
 * Session Announcement Protocol (RFC 2974) demuxer
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
#include "demux.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "network.h"
#include "os_support.h"
#include "internal.h"
#include "avio_internal.h"
#include "url.h"
#include "rtpdec.h"
#if HAVE_POLL_H
#include <poll.h>
#endif

struct SAPState {
    URLContext *ann_fd;
    AVFormatContext *sdp_ctx;
    FFIOContext sdp_pb;
    uint16_t hash;
    char *sdp;
    int eof;
};

static int sap_probe(const AVProbeData *p)
{
    if (av_strstart(p->filename, "sap:", NULL))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int sap_read_close(AVFormatContext *s)
{
    struct SAPState *sap = s->priv_data;
    if (sap->sdp_ctx)
        avformat_close_input(&sap->sdp_ctx);
    ffurl_closep(&sap->ann_fd);
    av_freep(&sap->sdp);
    ff_network_close();
    return 0;
}

static int sap_read_header(AVFormatContext *s)
{
    struct SAPState *sap = s->priv_data;
    char host[1024], path[1024], url[1024];
    uint8_t recvbuf[RTP_MAX_PACKET_LENGTH];
    const AVInputFormat *infmt;
    int port;
    int ret, i;

    if (!ff_network_init())
        return AVERROR(EIO);

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port,
                 path, sizeof(path), s->url);
    if (port < 0)
        port = 9875;

    if (!host[0]) {
        /* Listen for announcements on sap.mcast.net if no host was specified */
        av_strlcpy(host, "224.2.127.254", sizeof(host));
    }

    ff_url_join(url, sizeof(url), "udp", NULL, host, port, "?localport=%d",
                port);
    ret = ffurl_open_whitelist(&sap->ann_fd, url, AVIO_FLAG_READ,
                               &s->interrupt_callback, NULL,
                               s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret)
        goto fail;

    while (1) {
        int addr_type, auth_len;
        int pos;

        ret = ffurl_read(sap->ann_fd, recvbuf, sizeof(recvbuf) - 1);
        if (ret == AVERROR(EAGAIN))
            continue;
        if (ret < 0)
            goto fail;
        recvbuf[ret] = '\0'; /* Null terminate for easier parsing */
        if (ret < 8) {
            av_log(s, AV_LOG_WARNING, "Received too short packet\n");
            continue;
        }

        if ((recvbuf[0] & 0xe0) != 0x20) {
            av_log(s, AV_LOG_WARNING, "Unsupported SAP version packet "
                                      "received\n");
            continue;
        }

        if (recvbuf[0] & 0x04) {
            av_log(s, AV_LOG_WARNING, "Received stream deletion "
                                      "announcement\n");
            continue;
        }
        addr_type = recvbuf[0] & 0x10;
        auth_len  = recvbuf[1];
        sap->hash = AV_RB16(&recvbuf[2]);
        pos = 4;
        if (addr_type)
            pos += 16; /* IPv6 */
        else
            pos += 4; /* IPv4 */
        pos += auth_len * 4;
        if (pos + 4 >= ret) {
            av_log(s, AV_LOG_WARNING, "Received too short packet\n");
            continue;
        }
#define MIME "application/sdp"
        if (strcmp(&recvbuf[pos], MIME) == 0) {
            pos += strlen(MIME) + 1;
        } else if (strncmp(&recvbuf[pos], "v=0\r\n", 5) == 0) {
            // Direct SDP without a mime type
        } else {
            av_log(s, AV_LOG_WARNING, "Unsupported mime type %s\n",
                                      &recvbuf[pos]);
            continue;
        }

        sap->sdp = av_strdup(&recvbuf[pos]);
        if (!sap->sdp) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        break;
    }

    av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", sap->sdp);
    ffio_init_read_context(&sap->sdp_pb, sap->sdp, strlen(sap->sdp));

    infmt = av_find_input_format("sdp");
    if (!infmt)
        goto fail;
    sap->sdp_ctx = avformat_alloc_context();
    if (!sap->sdp_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    sap->sdp_ctx->max_delay = s->max_delay;
    sap->sdp_ctx->pb        = &sap->sdp_pb.pub;
    sap->sdp_ctx->interrupt_callback = s->interrupt_callback;

    if ((ret = ff_copy_whiteblacklists(sap->sdp_ctx, s)) < 0)
        goto fail;

    ret = avformat_open_input(&sap->sdp_ctx, "temp.sdp", infmt, NULL);
    if (ret < 0)
        goto fail;
    if (sap->sdp_ctx->ctx_flags & AVFMTCTX_NOHEADER)
        s->ctx_flags |= AVFMTCTX_NOHEADER;
    for (i = 0; i < sap->sdp_ctx->nb_streams; i++) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        st->id = i;
        avcodec_parameters_copy(st->codecpar, sap->sdp_ctx->streams[i]->codecpar);
        st->time_base = sap->sdp_ctx->streams[i]->time_base;
    }

    return 0;

fail:
    sap_read_close(s);
    return ret;
}

static int sap_fetch_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct SAPState *sap = s->priv_data;
    int fd = ffurl_get_file_handle(sap->ann_fd);
    int n, ret;
    struct pollfd p = {fd, POLLIN, 0};
    uint8_t recvbuf[RTP_MAX_PACKET_LENGTH];

    if (sap->eof)
        return AVERROR_EOF;

    while (1) {
        n = poll(&p, 1, 0);
        if (n <= 0 || !(p.revents & POLLIN))
            break;
        ret = ffurl_read(sap->ann_fd, recvbuf, sizeof(recvbuf));
        if (ret >= 8) {
            uint16_t hash = AV_RB16(&recvbuf[2]);
            /* Should ideally check the source IP address, too */
            if (recvbuf[0] & 0x04 && hash == sap->hash) {
                /* Stream deletion */
                sap->eof = 1;
                return AVERROR_EOF;
            }
        }
    }
    ret = av_read_frame(sap->sdp_ctx, pkt);
    if (ret < 0)
        return ret;
    if (s->ctx_flags & AVFMTCTX_NOHEADER) {
        while (sap->sdp_ctx->nb_streams > s->nb_streams) {
            int i = s->nb_streams;
            AVStream *st = avformat_new_stream(s, NULL);
            if (!st) {
                return AVERROR(ENOMEM);
            }
            st->id = i;
            avcodec_parameters_copy(st->codecpar, sap->sdp_ctx->streams[i]->codecpar);
            st->time_base = sap->sdp_ctx->streams[i]->time_base;
        }
    }
    return ret;
}

const FFInputFormat ff_sap_demuxer = {
    .p.name         = "sap",
    .p.long_name    = NULL_IF_CONFIG_SMALL("SAP input"),
    .p.flags        = AVFMT_NOFILE,
    .priv_data_size = sizeof(struct SAPState),
    .read_probe     = sap_probe,
    .read_header    = sap_read_header,
    .read_packet    = sap_fetch_packet,
    .read_close     = sap_read_close,
};
