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
#include "sauce.h"

typedef struct {
    AVClass *class;
    int chars_per_frame;
    uint64_t fsize;  /**< file size less metadata buffer */
    char *video_size;/**< A string describing video size, set by a private option. */
    char *framerate; /**< Set by a private option. */
} TtyDemuxContext;

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

static int read_header(AVFormatContext *avctx,
                       AVFormatParameters *ap)
{
    TtyDemuxContext *s = avctx->priv_data;
    int width = 0, height = 0, ret = 0;
    AVStream *st = avformat_new_stream(avctx, NULL);
    AVRational framerate;

    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    st->codec->codec_tag   = 0;
    st->codec->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id    = CODEC_ID_ANSI;

    if (s->video_size && (ret = av_parse_video_size(&width, &height, s->video_size)) < 0) {
        av_log (avctx, AV_LOG_ERROR, "Couldn't parse video size.\n");
        goto fail;
    }
    if ((ret = av_parse_video_rate(&framerate, s->framerate)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not parse framerate: %s.\n", s->framerate);
        goto fail;
    }
    st->codec->width  = width;
    st->codec->height = height;
    av_set_pts_info(st, 60, framerate.den, framerate.num);

    /* simulate tty display speed */
    s->chars_per_frame = FFMAX(av_q2d(st->time_base)*s->chars_per_frame, 1);

    if (avctx->pb->seekable) {
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

    if (url_feof(avctx->pb))
        return AVERROR_EOF;

    n = s->chars_per_frame;
    if (s->fsize) {
        // ignore metadata buffer
        uint64_t p = avio_tell(avctx->pb);
        if (p + s->chars_per_frame > s->fsize)
            n = s->fsize - p;
    }

    pkt->size = av_get_packet(avctx->pb, pkt, n);
    if (pkt->size <= 0)
        return AVERROR(EIO);
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

#define OFFSET(x) offsetof(TtyDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "chars_per_frame", "", offsetof(TtyDemuxContext, chars_per_frame), AV_OPT_TYPE_INT, {.dbl = 6000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, DEC },
    { NULL },
};

static const AVClass tty_demuxer_class = {
    .class_name     = "TTY demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_tty_demuxer = {
    .name           = "tty",
    .long_name      = NULL_IF_CONFIG_SMALL("Tele-typewriter"),
    .priv_data_size = sizeof(TtyDemuxContext),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .extensions     = "ans,art,asc,diz,ice,nfo,txt,vt",
    .priv_class     = &tty_demuxer_class,
};
