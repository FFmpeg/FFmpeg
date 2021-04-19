/*
 * Tele-typewriter demuxer
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * @file
 * Tele-typewriter demuxer
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"
#include "sauce.h"

static int isansicode(int x)
{
    return x == 0x1B || x == 0x0A || x == 0x0D || (x >= 0x20 && x < 0x7f);
}

static const char tty_extensions[31] = "ans,art,asc,diz,ice,nfo,txt,vt";

typedef struct TtyDemuxContext {
    AVClass *class;
    int chars_per_frame;
    uint64_t fsize;  /**< file size less metadata buffer */
    int width, height; /**< Set by a private option. */
    AVRational framerate; /**< Set by a private option. */
} TtyDemuxContext;

static int read_probe(const AVProbeData *p)
{
    int cnt = 0;

    if (!p->buf_size)
        return 0;

    for (int i = 0; i < 8 && i < p->buf_size; i++)
        cnt += !!isansicode(p->buf[i]);

    if (cnt != 8)
        return 0;

    for (int i = 8; i < p->buf_size; i++)
        cnt += !!isansicode(p->buf[i]);

    return (cnt * 99LL / p->buf_size) * (cnt > 400) *
        !!av_match_ext(p->filename, tty_extensions);
}

/**
 * Parse EFI header
 */
static int efi_read(AVFormatContext *avctx, uint64_t start_pos)
{
    TtyDemuxContext *s = avctx->priv_data;
    AVIOContext *pb = avctx->pb;
    char buf[37];
    int len;

    avio_seek(pb, start_pos, SEEK_SET);
    if (avio_r8(pb) != 0x1A)
        return -1;

#define GET_EFI_META(name,size) \
    len = avio_r8(pb); \
    if (len < 1 || len > size) \
        return -1; \
    if (avio_read(pb, buf, size) == size) { \
        buf[len] = 0; \
        av_dict_set(&avctx->metadata, name, buf, 0); \
    }

    GET_EFI_META("filename", 12)
    GET_EFI_META("title",    36)

    s->fsize = start_pos;
    return 0;
}

static int read_header(AVFormatContext *avctx)
{
    TtyDemuxContext *s = avctx->priv_data;
    int ret = 0;
    AVStream *st = avformat_new_stream(avctx, NULL);

    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    st->codecpar->codec_tag   = 0;
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = AV_CODEC_ID_ANSI;

    st->codecpar->width  = s->width;
    st->codecpar->height = s->height;
    avpriv_set_pts_info(st, 60, s->framerate.den, s->framerate.num);
    st->avg_frame_rate = s->framerate;

    /* simulate tty display speed */
    s->chars_per_frame = FFMAX(av_q2d(st->time_base)*s->chars_per_frame, 1);

    if (avctx->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        s->fsize = avio_size(avctx->pb);
        st->duration = (s->fsize + s->chars_per_frame - 1) / s->chars_per_frame;

        if (ff_sauce_read(avctx, &s->fsize, 0, 0) < 0)
            efi_read(avctx, s->fsize - 51);

        avio_seek(avctx->pb, 0, SEEK_SET);
    }

fail:
    return ret;
}

static int read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    TtyDemuxContext *s = avctx->priv_data;
    int n;

    if (avio_feof(avctx->pb))
        return AVERROR_EOF;

    n = s->chars_per_frame;
    if (s->fsize) {
        // ignore metadata buffer
        uint64_t p = avio_tell(avctx->pb);
        if (p == s->fsize)
            return AVERROR_EOF;
        if (p + s->chars_per_frame > s->fsize)
            n = s->fsize - p;
    }

    pkt->size = av_get_packet(avctx->pb, pkt, n);
    if (pkt->size < 0)
        return pkt->size;
    pkt->stream_index = 0;
    pkt->pts = pkt->pos / s->chars_per_frame;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

#define OFFSET(x) offsetof(TtyDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "chars_per_frame", "", offsetof(TtyDemuxContext, chars_per_frame), AV_OPT_TYPE_INT, {.i64 = 6000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass tty_demuxer_class = {
    .class_name     = "TTY demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_tty_demuxer = {
    .name           = "tty",
    .long_name      = NULL_IF_CONFIG_SMALL("Tele-typewriter"),
    .priv_data_size = sizeof(TtyDemuxContext),
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .extensions     = tty_extensions,
    .priv_class     = &tty_demuxer_class,
    .flags          = AVFMT_GENERIC_INDEX,
};
