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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/hash.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

struct MD5Context {
    const AVClass *avclass;
    struct AVHashContext *hash;
    char *hash_name;
};

static void md5_finish(struct AVFormatContext *s, char *buf)
{
    struct MD5Context *c = s->priv_data;
    uint8_t md5[AV_HASH_MAX_SIZE];
    int i, offset = strlen(buf);
    int len = av_hash_get_size(c->hash);
    av_assert0(len > 0 && len <= sizeof(md5));
    av_hash_final(c->hash, md5);
    for (i = 0; i < len; i++) {
        snprintf(buf + offset, 3, "%02"PRIx8, md5[i]);
        offset += 2;
    }
    buf[offset] = '\n';
    buf[offset+1] = 0;

    avio_write(s->pb, buf, strlen(buf));
    avio_flush(s->pb);
}

#define OFFSET(x) offsetof(struct MD5Context, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption hash_options[] = {
    { "hash", "set hash to use", OFFSET(hash_name), AV_OPT_TYPE_STRING, {.str = "md5"}, 0, 0, ENC },
    { NULL },
};

static const AVClass md5enc_class = {
    .class_name = "hash encoder class",
    .item_name  = av_default_item_name,
    .option     = hash_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_MD5_MUXER
static int write_header(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    int res = av_hash_alloc(&c->hash, c->hash_name);
    if (res < 0)
        return res;
    av_hash_init(c->hash);
    return 0;
}

static int write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    struct MD5Context *c = s->priv_data;
    av_hash_update(c->hash, pkt->data, pkt->size);
    return 0;
}

static int write_trailer(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    char buf[256];
    av_strlcpy(buf, av_hash_get_name(c->hash), sizeof(buf) - 200);
    av_strlcat(buf, "=", sizeof(buf) - 200);

    md5_finish(s, buf);

    av_hash_freep(&c->hash);
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
    .priv_class        = &md5enc_class,
};
#endif

#if CONFIG_FRAMEMD5_MUXER
static int framemd5_write_header(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    int res = av_hash_alloc(&c->hash, c->hash_name);
    if (res < 0)
        return res;
    return ff_framehash_write_header(s);
}

static int framemd5_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    struct MD5Context *c = s->priv_data;
    char buf[256];
    av_hash_init(c->hash);
    av_hash_update(c->hash, pkt->data, pkt->size);

    snprintf(buf, sizeof(buf) - 64, "%d, %10"PRId64", %10"PRId64", %8d, %8d, ",
             pkt->stream_index, pkt->dts, pkt->pts, pkt->duration, pkt->size);
    md5_finish(s, buf);
    return 0;
}

static int framemd5_write_trailer(struct AVFormatContext *s)
{
    struct MD5Context *c = s->priv_data;
    av_hash_freep(&c->hash);
    return 0;
}

static const AVClass framemd5_class = {
    .class_name = "frame hash encoder class",
    .item_name  = av_default_item_name,
    .option     = hash_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_framemd5_muxer = {
    .name              = "framemd5",
    .long_name         = NULL_IF_CONFIG_SMALL("Per-frame MD5 testing"),
    .priv_data_size    = sizeof(struct MD5Context),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = framemd5_write_header,
    .write_packet      = framemd5_write_packet,
    .write_trailer     = framemd5_write_trailer,
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT |
                         AVFMT_TS_NEGATIVE,
    .priv_class        = &framemd5_class,
};
#endif
