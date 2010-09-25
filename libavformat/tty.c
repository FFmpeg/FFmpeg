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
#include "avformat.h"
#include "sauce.h"
#include <strings.h>

#define LINE_RATE 6000 /* characters per second */

typedef struct {
    int chars_per_frame;
    uint64_t fsize;  /**< file size less metadata buffer */
} TtyDemuxContext;

/**
 * Parse EFI header
 */
static int efi_read(AVFormatContext *avctx, uint64_t start_pos)
{
    TtyDemuxContext *s = avctx->priv_data;
    ByteIOContext *pb = avctx->pb;
    char buf[37];
    int len;

    url_fseek(pb, start_pos, SEEK_SET);
    if (get_byte(pb) != 0x1A)
        return -1;

#define GET_EFI_META(name,size) \
    len = get_byte(pb); \
    if (len < 1 || len > size) \
        return -1; \
    if (get_buffer(pb, buf, size) == size) { \
        buf[len] = 0; \
        av_metadata_set2(&avctx->metadata, name, buf, 0); \
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
    AVStream *st = av_new_stream(avctx, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_tag   = 0;
    st->codec->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id    = CODEC_ID_ANSI;
    if (ap->width)  st->codec->width  = ap->width;
    if (ap->height) st->codec->height = ap->height;

    if (!ap->time_base.num) {
        av_set_pts_info(st, 60, 1, 25);
    } else {
        av_set_pts_info(st, 60, ap->time_base.num, ap->time_base.den);
    }

    /* simulate tty display speed */
    s->chars_per_frame = FFMAX(av_q2d(st->time_base) * (ap->sample_rate ? ap->sample_rate : LINE_RATE), 1);

    if (!url_is_streamed(avctx->pb)) {
        s->fsize = url_fsize(avctx->pb);
        st->duration = (s->fsize + s->chars_per_frame - 1) / s->chars_per_frame;

        if (ff_sauce_read(avctx, &s->fsize, 0, 0) < 0)
            efi_read(avctx, s->fsize - 51);

        url_fseek(avctx->pb, 0, SEEK_SET);
    }

    return 0;
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
        uint64_t p = url_ftell(avctx->pb);
        if (p + s->chars_per_frame > s->fsize)
            n = s->fsize - p;
    }

    pkt->size = av_get_packet(avctx->pb, pkt, n);
    if (pkt->size <= 0)
        return AVERROR(EIO);
    pkt->flags |= PKT_FLAG_KEY;
    return 0;
}

AVInputFormat tty_demuxer = {
    .name           = "tty",
    .long_name      = NULL_IF_CONFIG_SMALL("Tele-typewriter"),
    .priv_data_size = sizeof(TtyDemuxContext),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .extensions     = "ans,art,asc,diz,ice,nfo,txt,vt",
};
