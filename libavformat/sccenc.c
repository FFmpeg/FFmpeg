/*
 * SCC muxer
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct SCCContext {
    int prev_h, prev_m, prev_s, prev_f;
    int inside;
    int n;
} SCCContext;

static int scc_write_header(AVFormatContext *avf)
{
    SCCContext *scc = avf->priv_data;

    if (avf->nb_streams != 1 ||
        avf->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(avf, AV_LOG_ERROR,
               "SCC supports only a single subtitles stream.\n");
        return AVERROR(EINVAL);
    }
    if (avf->streams[0]->codecpar->codec_id != AV_CODEC_ID_EIA_608) {
        av_log(avf, AV_LOG_ERROR,
               "Unsupported subtitles codec: %s\n",
               avcodec_get_name(avf->streams[0]->codecpar->codec_id));
        return AVERROR(EINVAL);
    }
    avpriv_set_pts_info(avf->streams[0], 64, 1, 1000);
    avio_printf(avf->pb, "Scenarist_SCC V1.0\n");

    scc->prev_h = scc->prev_m = scc->prev_s = scc->prev_f = -1;
    scc->inside = 0;

    return 0;
}

static int scc_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    SCCContext *scc = avf->priv_data;
    int64_t pts = pkt->pts;
    int i, h, m, s, f;

    if (pts == AV_NOPTS_VALUE) {
        av_log(avf, AV_LOG_WARNING,
               "Insufficient timestamps.\n");
        return 0;
    }

    h = (int)(pts / (3600000));
    m = (int)(pts / (60000)) % 60;
    s = (int)(pts /  1000) % 60;
    f = (int)(pts %  1000) / 33;

    for (i = 0; i < pkt->size - 2; i+=3) {
        if (pkt->data[i] == 0xfc && ((pkt->data[i + 1] != 0x80 || pkt->data[i + 2] != 0x80)))
            break;
    }
    if (i >= pkt->size - 2)
        return 0;

    if (!scc->inside && (scc->prev_h != h || scc->prev_m != m || scc->prev_s != s || scc->prev_f != f)) {
        avio_printf(avf->pb, "\n%02d:%02d:%02d:%02d\t", h, m, s, f);
        scc->inside = 1;
    }
    for (i = 0; i < pkt->size; i+=3) {
        if (i + 3 > pkt->size)
            break;

        if (pkt->data[i] != 0xfc || (pkt->data[i + 1] == 0x80 && pkt->data[i + 2] == 0x80))
            continue;
        if (!scc->inside) {
            avio_printf(avf->pb, "\n%02d:%02d:%02d:%02d\t", h, m, s, f);
            scc->inside = 1;
        }
        if (scc->n > 0)
            avio_w8(avf->pb, ' ');
        avio_printf(avf->pb, "%02x%02x", pkt->data[i + 1], pkt->data[i + 2]);
        scc->n++;
    }
    if (scc->inside && (scc->prev_h != h || scc->prev_m != m || scc->prev_s != s || scc->prev_f != f)) {
        avio_w8(avf->pb, '\n');
        scc->n = 0;
        scc->inside = 0;
    }

    scc->prev_h = h;
    scc->prev_m = m;
    scc->prev_s = s;
    scc->prev_f = f;
    return 0;
}

const AVOutputFormat ff_scc_muxer = {
    .name           = "scc",
    .long_name      = NULL_IF_CONFIG_SMALL("Scenarist Closed Captions"),
    .extensions     = "scc",
    .priv_data_size = sizeof(SCCContext),
    .write_header   = scc_write_header,
    .write_packet   = scc_write_packet,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
    .subtitle_codec = AV_CODEC_ID_EIA_608,
};
