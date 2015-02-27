/*
 * webp muxer
 * Copyright (c) 2014 Michael Niedermayer
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

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

typedef struct WebpContext{
    AVClass *class;
    int frame_count;
    AVPacket last_pkt;
    int loop;
} WebpContext;

static int webp_write_header(AVFormatContext *s)
{
    AVStream *st;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Only exactly 1 stream is supported\n");
        return AVERROR(EINVAL);
    }
    st = s->streams[0];
    if (st->codec->codec_id != AV_CODEC_ID_WEBP) {
        av_log(s, AV_LOG_ERROR, "Only WebP is supported\n");
        return AVERROR(EINVAL);
    }
    avpriv_set_pts_info(st, 24, 1, 1000);

    avio_write(s->pb, "RIFF\0\0\0\0WEBP", 12);

    return 0;
}

static int flush(AVFormatContext *s, int trailer, int64_t pts)
{
    WebpContext *w = s->priv_data;
    AVStream *st = s->streams[0];

    if (w->last_pkt.size) {
        int skip = 0;
        unsigned flags = 0;
        int vp8x = 0;

        if (AV_RL32(w->last_pkt.data) == AV_RL32("RIFF"))
            skip = 12;
        if (AV_RL32(w->last_pkt.data + skip) == AV_RL32("VP8X")) {
            flags |= w->last_pkt.data[skip + 4 + 4];
            vp8x = 1;
            skip += AV_RL32(w->last_pkt.data + skip + 4) + 8;
        }

        w->frame_count ++;

        if (w->frame_count == 1) {
            if (!trailer) {
                vp8x = 1;
                flags |= 2 + 16;
            }

            if (vp8x) {
                avio_write(s->pb, "VP8X", 4);
                avio_wl32(s->pb, 10);
                avio_w8(s->pb, flags);
                avio_wl24(s->pb, 0);
                avio_wl24(s->pb, st->codec->width - 1);
                avio_wl24(s->pb, st->codec->height - 1);
            }
            if (!trailer) {
                avio_write(s->pb, "ANIM", 4);
                avio_wl32(s->pb, 6);
                avio_wl32(s->pb, 0xFFFFFFFF);
                avio_wl16(s->pb, w->loop);
            }
        }

        if (w->frame_count > trailer) {
            avio_write(s->pb, "ANMF", 4);
            avio_wl32(s->pb, 16 + w->last_pkt.size - skip);
            avio_wl24(s->pb, 0);
            avio_wl24(s->pb, 0);
            avio_wl24(s->pb, st->codec->width - 1);
            avio_wl24(s->pb, st->codec->height - 1);
            if (w->last_pkt.pts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) {
                avio_wl24(s->pb, pts - w->last_pkt.pts);
            } else
                avio_wl24(s->pb, w->last_pkt.duration);
            avio_w8(s->pb, 0);
        }
        avio_write(s->pb, w->last_pkt.data + skip, w->last_pkt.size - skip);
        av_free_packet(&w->last_pkt);
    }

    return 0;
}

static int webp_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebpContext *w = s->priv_data;
    int ret;

    if ((ret = flush(s, 0, pkt->pts)) < 0)
        return ret;

    av_copy_packet(&w->last_pkt, pkt);

    return 0;
}

static int webp_write_trailer(AVFormatContext *s)
{
    unsigned filesize;
    int ret;

    if ((ret = flush(s, 1, AV_NOPTS_VALUE)) < 0)
        return ret;

    filesize = avio_tell(s->pb);
    avio_seek(s->pb, 4, SEEK_SET);
    avio_wl32(s->pb, filesize - 8);

    return 0;
}

#define OFFSET(x) offsetof(WebpContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "loop", "Number of times to loop the output: 0 - infinite loop", OFFSET(loop),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 65535, ENC },
    { NULL },
};

static const AVClass webp_muxer_class = {
    .class_name = "WebP muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = options,
};
AVOutputFormat ff_webp_muxer = {
    .name           = "webp",
    .long_name      = NULL_IF_CONFIG_SMALL("WebP"),
    .extensions     = "webp",
    .priv_data_size = sizeof(WebpContext),
    .video_codec    = AV_CODEC_ID_WEBP,
    .write_header   = webp_write_header,
    .write_packet   = webp_write_packet,
    .write_trailer  = webp_write_trailer,
    .priv_class     = &webp_muxer_class,
    .flags          = AVFMT_VARIABLE_FPS,
};
