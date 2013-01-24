/*
 * SubRip subtitle muxer
 * Copyright (c) 2012  Nicolas George <nicolas.george@normalesup.org>
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
#include "internal.h"
#include "libavutil/log.h"
#include "libavutil/intreadwrite.h"

/* TODO: add options for:
   - character encoding;
   - LF / CRLF;
   - byte order mark.
 */

typedef struct SRTContext{
    unsigned index;
} SRTContext;

static int srt_write_header(AVFormatContext *avf)
{
    SRTContext *srt = avf->priv_data;

    if (avf->nb_streams != 1 ||
        avf->streams[0]->codec->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(avf, AV_LOG_ERROR,
               "SRT supports only a single subtitles stream.\n");
        return AVERROR(EINVAL);
    }
    if (avf->streams[0]->codec->codec_id != AV_CODEC_ID_TEXT &&
        avf->streams[0]->codec->codec_id != AV_CODEC_ID_SUBRIP &&
        avf->streams[0]->codec->codec_id != AV_CODEC_ID_SRT) {
        av_log(avf, AV_LOG_ERROR,
               "Unsupported subtitles codec: %s\n",
               avcodec_get_name(avf->streams[0]->codec->codec_id));
        return AVERROR(EINVAL);
    }
    avpriv_set_pts_info(avf->streams[0], 64, 1, 1000);
    srt->index = 1;
    return 0;
}

static int srt_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    SRTContext *srt = avf->priv_data;
    int write_ts = avf->streams[0]->codec->codec_id != AV_CODEC_ID_SRT;

    if (write_ts) {
        int64_t s = pkt->pts, e, d = pkt->duration;
        int size, x1 = -1, y1 = -1, x2 = -1, y2 = -1;
        const uint8_t *p;

        p = av_packet_get_side_data(pkt, AV_PKT_DATA_SUBTITLE_POSITION, &size);
        if (p && size == 16) {
            x1 = AV_RL32(p     );
            y1 = AV_RL32(p +  4);
            x2 = AV_RL32(p +  8);
            y2 = AV_RL32(p + 12);
        }

        if (d <= 0)
            /* For backward compatibility, fallback to convergence_duration. */
            d = pkt->convergence_duration;
        if (s == AV_NOPTS_VALUE || d < 0) {
            av_log(avf, AV_LOG_WARNING,
                   "Insufficient timestamps in event number %d.\n", srt->index);
            return 0;
        }
        e = s + d;
        avio_printf(avf->pb, "%d\n%02d:%02d:%02d,%03d --> %02d:%02d:%02d,%03d",
                       srt->index,
                       (int)(s / 3600000),      (int)(s / 60000) % 60,
                       (int)(s /    1000) % 60, (int)(s %  1000),
                       (int)(e / 3600000),      (int)(e / 60000) % 60,
                       (int)(e /    1000) % 60, (int)(e %  1000));
        if (p)
            avio_printf(avf->pb, "  X1:%03d X2:%03d Y1:%03d Y2:%03d",
                        x1, x2, y1, y2);
        avio_printf(avf->pb, "\n");
    }
    avio_write(avf->pb, pkt->data, pkt->size);
    if (write_ts)
        avio_write(avf->pb, "\n\n", 2);
    avio_flush(avf->pb);
    srt->index++;
    return 0;
}

AVOutputFormat ff_srt_muxer = {
    .name           = "srt",
    .long_name      = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .mime_type      = "application/x-subrip",
    .extensions     = "srt",
    .priv_data_size = sizeof(SRTContext),
    .write_header   = srt_write_header,
    .write_packet   = srt_write_packet,
    .flags          = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
    .subtitle_codec = AV_CODEC_ID_SUBRIP,
};
