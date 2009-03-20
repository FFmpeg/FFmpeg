/*
 * Microsoft RTP/ASF support.
 * Copyright (c) 2008 Ronald S. Bultje
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
 * @file libavformat/rtp_asf.c
 * @brief Microsoft RTP/ASF support
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include <libavutil/base64.h>
#include <libavutil/avstring.h>
#include "rtp.h"
#include "rtp_asf.h"
#include "rtsp.h"
#include "asf.h"

void ff_wms_parse_sdp_a_line(AVFormatContext *s, const char *p)
{
    if (av_strstart(p, "pgmpu:data:application/vnd.ms.wms-hdr.asfv1;base64,", &p)) {
        ByteIOContext pb;
        RTSPState *rt = s->priv_data;
        int len = strlen(p) * 6 / 8;
        char *buf = av_mallocz(len);
        av_base64_decode(buf, p, len);

        init_put_byte(&pb, buf, len, 0, NULL, NULL, NULL, NULL);
        if (rt->asf_ctx) {
            av_close_input_stream(rt->asf_ctx);
            rt->asf_ctx = NULL;
        }
        av_open_input_stream(&rt->asf_ctx, &pb, "", &asf_demuxer, NULL);
        av_free(buf);
        rt->asf_ctx->pb = NULL;
    }
}

static int
asfrtp_parse_sdp_line (AVFormatContext *s, int stream_index,
                       PayloadContext *asf, const char *line)
{
    if (av_strstart(line, "stream:", &line)) {
        RTSPState *rt = s->priv_data;

        s->streams[stream_index]->id = strtol(line, NULL, 10);

        if (rt->asf_ctx) {
            int i;

            for (i = 0; i < rt->asf_ctx->nb_streams; i++) {
                if (s->streams[stream_index]->id == rt->asf_ctx->streams[i]->id) {
                    *s->streams[stream_index]->codec =
                        *rt->asf_ctx->streams[i]->codec;
                    rt->asf_ctx->streams[i]->codec->extradata_size = 0;
                    rt->asf_ctx->streams[i]->codec->extradata = NULL;
                    av_set_pts_info(s->streams[stream_index], 32, 1, 1000);
                }
           }
        }
    }

    return 0;
}

#define RTP_ASF_HANDLER(n, s, t) \
RTPDynamicProtocolHandler ff_ms_rtp_ ## n ## _handler = { \
    s, \
    t, \
    CODEC_ID_NONE, \
    asfrtp_parse_sdp_line, \
};

RTP_ASF_HANDLER(asf_pfv, "x-asf-pf",  CODEC_TYPE_VIDEO);
RTP_ASF_HANDLER(asf_pfa, "x-asf-pf",  CODEC_TYPE_AUDIO);
