/*
 * MD5 encoder (for codec/format testing)
 * Copyright (c) 2009 Reimar Döffinger, based on crcenc (c) 2002 Fabrice Bellard
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

#include "libavutil/md5.h"
#include "avformat.h"
#include "internal.h"

struct MD5Context {
    struct AVMD5 *md5;
};

static void md5_finish(struct AVFormatContext *s, char *buf)
{
    struct MD5Context *c = s->priv_data;
    uint8_t md5[16];
    int i, offset = strlen(buf);
    av_md5_final(c->md5, md5);
    for (i = 0; i < sizeof(md5); i++) {
        snprintf(buf + offset, 3, "%02"PRIx8, md5[i]);
        offset += 2;
    }
    buf[offset] = '\n';
    buf[offset+1] = 0;

    avio_write(s->pb, buf, strlen(buf));
    avio_flush(s->pb);
}

#if CONFIG_MD5_MUXER
static int write_header(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    c->md5 = av_md5_alloc();
    if (!c->md5)
        return AVERROR(ENOMEM);
    av_md5_init(c->md5);
    return 0;
}

static int write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    struct MD5Context *c = s->priv_data;
    av_md5_update(c->md5, pkt->data, pkt->size);
    return 0;
}

static int write_trailer(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    char buf[64] = "MD5=";

    md5_finish(s, buf);

    av_freep(&c->md5);
    return 0;
}

AVOutputFormat ff_md5_muxer = {
    .name              = "md5",
    .long_name         = NULL_IF_CONFIG_SMALL("MD5 testing"),
    .priv_data_size    = sizeof(struct MD5Context),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_FRAMEMD5_MUXER
static int framemd5_write_header(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    c->md5 = av_md5_alloc();
    if (!c->md5)
        return AVERROR(ENOMEM);
    return ff_framehash_write_header(s);
}

static int framemd5_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    struct MD5Context *c = s->priv_data;
    char buf[256];
    av_md5_init(c->md5);
    av_md5_update(c->md5, pkt->data, pkt->size);

    snprintf(buf, sizeof(buf) - 64, "%d, %10"PRId64", %10"PRId64", %8d, %8d, ",
             pkt->stream_index, pkt->dts, pkt->pts, pkt->duration, pkt->size);
    md5_finish(s, buf);
    return 0;
}

static int framemd5_write_trailer(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    av_freep(&c->md5);
    return 0;
}

AVOutputFormat ff_framemd5_muxer = {
    .name              = "framemd5",
    .long_name         = NULL_IF_CONFIG_SMALL("Per-frame MD5 testing"),
    .priv_data_size    = sizeof(struct MD5Context),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = framemd5_write_header,
    .write_packet      = framemd5_write_packet,
    .write_trailer     = framemd5_write_trailer,
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
};
#endif
